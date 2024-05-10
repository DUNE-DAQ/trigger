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
#include "dfmessages/TriggerDecision.hpp"
#include "dfmessages/TriggerDecisionToken.hpp"
#include "dfmessages/TriggerInhibit.hpp"
#include "dfmessages/Types.hpp"
#include "iomanager/Receiver.hpp"
#include "trgdataformats/TriggerCandidateData.hpp"
#include "trgdataformats/Types.hpp"
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

  // TD requests
  std::vector<dfmessages::SourceID> m_mandatory_links;
  std::map<int, std::vector<dfmessages::SourceID>> m_group_links;
  nlohmann::json m_group_links_data;
  int m_total_group_links;
  void parse_group_links(const nlohmann::json& data);
  void print_group_links();
  dfmessages::ComponentRequest create_request_for_link(dfmessages::SourceID link,
                                                       triggeralgs::timestamp_t start,
                                                       triggeralgs::timestamp_t end);
  std::vector<dfmessages::ComponentRequest> create_all_decision_requests(std::vector<dfmessages::SourceID> links,
                                                                         triggeralgs::timestamp_t start,
                                                                         triggeralgs::timestamp_t end);
  void add_requests_to_decision(dfmessages::TriggerDecision& decision,
                                std::vector<dfmessages::ComponentRequest> requests);

  // ROI
  bool m_use_roi_readout;
  struct roi_group
  {
    int n_links;
    float prob;
    triggeralgs::timestamp_t time_window;
    std::string mode;
  };
  std::map<int, roi_group> m_roi_conf;
  nlohmann::json m_roi_conf_data;
  void parse_roi_conf(const nlohmann::json& data);
  void print_roi_conf(std::map<int, roi_group> roi_conf);
  std::vector<int> m_roi_conf_ids;
  std::vector<float> m_roi_conf_probs;
  std::vector<float> m_roi_conf_probs_c;
  float get_random_num_float(float limit);
  int get_random_num_int();
  int pick_roi_group_conf();
  void roi_readout_make_requests(dfmessages::TriggerDecision& decision);

  int m_repeat_trigger_count{ 1 };

  // paused state, in which we don't send triggers
  std::atomic<bool> m_paused;
  std::atomic<bool> m_dfo_is_busy;
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
  void call_tc_decision(const PendingTD& pending_td);
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

  // Bitwords logic
  bool m_use_bitwords;
  nlohmann::json m_trigger_bitwords_json;
  bool m_bitword_check;
  std::bitset<64> m_TD_bitword;
  std::vector<std::bitset<64>> m_trigger_bitwords;
  std::bitset<64> get_TD_bitword(const PendingTD& ready_td);
  void print_trigger_bitwords(std::vector<std::bitset<64>> trigger_bitwords);
  bool check_trigger_bitwords();
  void print_bitword_flags(nlohmann::json m_trigger_bitwords_json);
  void set_trigger_bitwords();

  // Readout map config
  bool m_use_readout_map;
  nlohmann::json m_readout_window_map_data;
  std::map<trgdataformats::TriggerCandidateData::Type, std::pair<triggeralgs::timestamp_t, triggeralgs::timestamp_t>>
    m_readout_window_map;
  void parse_readout_map(const nlohmann::json& data);
  void print_readout_map(std::map<trgdataformats::TriggerCandidateData::Type,
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
  std::atomic<metric_counter_type> m_td_not_triggered_count{ 0 };
  std::atomic<metric_counter_type> m_td_not_triggered_tc_count{ 0 };
  std::atomic<metric_counter_type> m_td_total_count{ 0 };
  std::atomic<metric_counter_type> m_new_td_total_count{ 0 };
  std::atomic<metric_counter_type> m_td_queue_timeout_expired_err_count{ 0 };
  std::atomic<metric_counter_type> m_td_queue_timeout_expired_err_tc_count{ 0 };
  std::atomic<metric_counter_type> m_lc_kLive{ 0 };
  std::atomic<metric_counter_type> m_lc_kPaused{ 0 };
  std::atomic<metric_counter_type> m_lc_kDead{ 0 };
  std::atomic<uint64_t>            m_tc_data_vs_system;
  std::atomic<uint64_t>            m_td_made_vs_ro;
  std::atomic<uint64_t>            m_td_send_vs_ro;

  // Latency
  std::atomic<bool>                m_first_tc;
  std::atomic<uint64_t>            m_initial_offset;
};
} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_PLUGINS_MODULELEVELTRIGGER_HPP_

// Local Variables:
// c-basic-offset: 2
// End:
