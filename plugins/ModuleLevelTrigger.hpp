/**
 * @file ModuleLevelTrigger.hpp
 *
 * ModuleLevelTrigger is a DAQModule that generates trigger decisions
 * for standalone tests. It receives information on the current time and the
 * availability of the DF to absorb data and forms decisions at a configurable
 * rate and with configurable size.
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_PLUGINS_MODULELEVELTRIGGER_HPP_
#define TRIGGER_PLUGINS_MODULELEVELTRIGGER_HPP_

#include "trigger/Issues.hpp"
#include "trigger/LivetimeCounter.hpp"
#include "trigger/TokenManager.hpp"
#include "trigger/moduleleveltriggerinfo/InfoNljs.hpp"

#include "appfwk/DAQModule.hpp"
#include "daqdataformats/SourceID.hpp"
#include "detdataformats/trigger/TriggerCandidateData.hpp"
#include "detdataformats/trigger/Types.hpp"
#include "dfmessages/TimeSync.hpp"
#include "dfmessages/TriggerDecision.hpp"
#include "dfmessages/TriggerDecisionToken.hpp"
#include "dfmessages/TriggerInhibit.hpp"
#include "dfmessages/Types.hpp"
#include "iomanager/Receiver.hpp"
#include "timinglibs/TimestampEstimator.hpp"
#include "triggeralgs/TriggerCandidate.hpp"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace dunedaq {

namespace trigger {

/**
 * @brief ModuleLevelTrigger is the last level of the data selection
 * system, which reads in trigger candidates and sends trigger
 * decisions, subject to availability of TriggerDecisionTokens
 */
class ModuleLevelTrigger : public dunedaq::appfwk::DAQModule
{
public:
  /**
   * @brief ModuleLevelTrigger Constructor
   * @param name Instance name for this ModuleLevelTrigger instance
   */
  explicit ModuleLevelTrigger(const std::string& name);

  ModuleLevelTrigger(const ModuleLevelTrigger&) = delete;            ///< ModuleLevelTrigger is not copy-constructible
  ModuleLevelTrigger& operator=(const ModuleLevelTrigger&) = delete; ///< ModuleLevelTrigger is not copy-assignable
  ModuleLevelTrigger(ModuleLevelTrigger&&) = delete;                 ///< ModuleLevelTrigger is not move-constructible
  ModuleLevelTrigger& operator=(ModuleLevelTrigger&&) = delete;      ///< ModuleLevelTrigger is not move-assignable

  void init(const nlohmann::json& iniobj) override;
  void get_info(opmonlib::InfoCollector& ci, int level) override;

private:
  // Commands
  void do_configure(const nlohmann::json& obj);
  void do_start(const nlohmann::json& obj);
  void do_stop(const nlohmann::json& obj);
  void do_pause(const nlohmann::json& obj);
  void do_resume(const nlohmann::json& obj);
  void do_scrap(const nlohmann::json& obj);

  void send_trigger_decisions();
  std::thread m_send_trigger_decisions_thread;

  void dfo_busy_callback(dfmessages::TriggerInhibit& inhibit);

  // Queue sources and sinks
  std::shared_ptr<iomanager::ReceiverConcept<triggeralgs::TriggerCandidate>> m_candidate_input;
  std::shared_ptr<iomanager::ReceiverConcept<dfmessages::TriggerInhibit>> m_inhibit_input;
  std::string m_td_output_connection;

  std::vector<dfmessages::SourceID> m_links;

  int m_repeat_trigger_count{ 1 };

  // paused state, in which we don't send triggers
  std::atomic<bool> m_paused;
  std::atomic<bool> m_dfo_is_busy;
  std::atomic<bool> m_hsi_passthrough;
  std::atomic<bool> m_tc_merging;

  dfmessages::trigger_number_t m_last_trigger_number;

  dfmessages::run_number_t m_run_number;

  // Are we in the RUNNING state?
  std::atomic<bool> m_running_flag{ false };
  // Are we in a configured state, ie after conf and before scrap?
  std::atomic<bool> m_configured_flag{ false };

  // LivetimeCounter
  std::shared_ptr<LivetimeCounter> m_livetime_counter;
  LivetimeCounter::state_time_t m_lc_kLive_count;
  LivetimeCounter::state_time_t m_lc_kPaused_count;
  LivetimeCounter::state_time_t m_lc_kDead_count;
  LivetimeCounter::state_time_t m_lc_deadtime;

  // New buffering
  struct PendingTD
  {
    std::vector<triggeralgs::TriggerCandidate> contributing_tcs;
    triggeralgs::timestamp_t readout_start;
    triggeralgs::timestamp_t readout_end;
    int64_t walltime_expiration;
  };
  std::vector<PendingTD> m_pending_tds;
  std::vector<PendingTD> m_sent_tds;
  std::mutex m_td_vector_mutex;

  void add_tc(const triggeralgs::TriggerCandidate& tc);
  void add_td(const PendingTD& pending_td);
  void add_tc_ignored(const triggeralgs::TriggerCandidate& tc);
  void call_tc_decision(const PendingTD& pending_td, bool override_flag = false);
  bool check_overlap(const triggeralgs::TriggerCandidate& tc, const PendingTD& pending_td);
  bool check_overlap_td(const PendingTD& pending_td);
  bool check_td_readout_length(const PendingTD&);
  void clear_td_vectors();
  void flush_td_vectors();
  std::vector<PendingTD> get_ready_tds(std::vector<PendingTD>& pending_tds);
  int64_t m_buffer_timeout;
  int64_t m_td_readout_limit;
  std::atomic<bool> m_send_timed_out_tds;
  int m_earliest_tc_index;
  int get_earliest_tc_index(const PendingTD& pending_td);

  // Readout map config
  bool m_use_readout_map;
  nlohmann::json m_readout_window_map_data;
  std::map<detdataformats::trigger::TriggerCandidateData::Type,
           std::pair<triggeralgs::timestamp_t, triggeralgs::timestamp_t>>
    m_readout_window_map;
  void parse_readout_map(const nlohmann::json& data);
  void print_readout_map(std::map<detdataformats::trigger::TriggerCandidateData::Type,
                                  std::pair<triggeralgs::timestamp_t, triggeralgs::timestamp_t>> map);

  // Create the next trigger decision
  dfmessages::TriggerDecision create_decision(const PendingTD& pending_td);
  dfmessages::trigger_type_t m_trigger_type_shifted;

  // Optional list of TC types to ignore
  std::vector<int> m_ignored_tc_types;
  bool m_ignoring_tc_types;
  bool check_trigger_type_ignore(int tc_type);

  // Opmon variables
  using metric_counter_type = decltype(moduleleveltriggerinfo::Info::tc_received_count);
  std::atomic<metric_counter_type> m_tc_received_count{ 0 };
  std::atomic<metric_counter_type> m_tc_ignored_count{ 0 };
  std::atomic<metric_counter_type> m_td_sent_count{ 0 };
  std::atomic<metric_counter_type> m_new_td_sent_count{ 0 };
  std::atomic<metric_counter_type> m_td_sent_tc_count{ 0 };
  std::atomic<metric_counter_type> m_td_inhibited_count{ 0 };
  std::atomic<metric_counter_type> m_new_td_inhibited_count{ 0 };
  std::atomic<metric_counter_type> m_td_inhibited_tc_count{ 0 };
  std::atomic<metric_counter_type> m_td_paused_count{ 0 };
  std::atomic<metric_counter_type> m_td_paused_tc_count{ 0 };
  std::atomic<metric_counter_type> m_td_dropped_count{ 0 };
  std::atomic<metric_counter_type> m_td_dropped_tc_count{ 0 };
  std::atomic<metric_counter_type> m_td_cleared_count{ 0 };
  std::atomic<metric_counter_type> m_td_cleared_tc_count{ 0 };
  std::atomic<metric_counter_type> m_td_total_count{ 0 };
  std::atomic<metric_counter_type> m_new_td_total_count{ 0 };
  std::atomic<metric_counter_type> m_td_queue_timeout_expired_err_count{ 0 };
  std::atomic<metric_counter_type> m_td_queue_timeout_expired_err_tc_count{ 0 };
  std::atomic<metric_counter_type> m_lc_kLive{ 0 };
  std::atomic<metric_counter_type> m_lc_kPaused{ 0 };
  std::atomic<metric_counter_type> m_lc_kDead{ 0 };
};
} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_PLUGINS_MODULELEVELTRIGGER_HPP_

// Local Variables:
// c-basic-offset: 2
// End:
