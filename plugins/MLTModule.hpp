/**
 * @file MLTModule.hpp
 *
 * MLTModule is a DAQModule that generates trigger decisions
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
#include "trigger/Latency.hpp"
#include "trigger/opmon/moduleleveltrigger_info.pb.h"
#include "trigger/opmon/latency_info.pb.h"

#include "appfwk/DAQModule.hpp"

#include "appmodel/MLTModule.hpp"
#include "appmodel/MLTConf.hpp"
#include "appmodel/TCReadoutMap.hpp"
#include "appmodel/ROIGroupConf.hpp"
#include "appmodel/SourceIDConf.hpp"
#include "appmodel/LatencyMonitoringConf.hpp"

#include "confmodel/Connection.hpp"

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
 * @brief MLTModule is the last level of the data selection
 * system, which reads in trigger candidates and sends trigger
 * decisions, subject to availability of TriggerDecisionTokens
 */
class MLTModule : public dunedaq::appfwk::DAQModule
{
public:
  /**
   * @brief MLTModule Constructor
   * @param name Instance name for this MLTModule instance
   */
  explicit MLTModule(const std::string& name);

  MLTModule(const MLTModule&) = delete;            ///< MLTModule is not copy-constructible
  MLTModule& operator=(const MLTModule&) = delete; ///< MLTModule is not copy-assignable
  MLTModule(MLTModule&&) = delete;                 ///< MLTModule is not move-constructible
  MLTModule& operator=(MLTModule&&) = delete;      ///< MLTModule is not move-assignable

  void init(std::shared_ptr<appfwk::ModuleConfiguration> mcfg) override;
  void generate_opmon_data() override;

private:
  // Commands
  void do_start(const nlohmann::json& obj);
  void do_stop(const nlohmann::json& obj);
  void do_pause(const nlohmann::json& obj);
  void do_resume(const nlohmann::json& obj);

  void trigger_decisions_callback(dfmessages::TriggerDecision& decision);
  void dfo_busy_callback(dfmessages::TriggerInhibit& inhibit);

  // Queue sources and sinks
  std::shared_ptr<iomanager::ReceiverConcept<dfmessages::TriggerDecision>> m_decision_input;
  std::shared_ptr<iomanager::SenderConcept<dfmessages::TriggerDecision>> m_decision_output;
  std::shared_ptr<iomanager::ReceiverConcept<dfmessages::TriggerInhibit>> m_inhibit_input;

  /* TD requests
  std::vector<daqdataformats::SourceID> m_mandatory_links;
  std::map<int, std::vector<daqdataformats::SourceID>> m_group_links;
  nlohmann::json m_group_links_data;
  int m_total_group_links;
  void parse_group_links(const nlohmann::json& data);
  void print_group_links();
  dfmessages::ComponentRequest create_request_for_link(daqdataformats::SourceID link,
                                                       triggeralgs::timestamp_t start,
                                                       triggeralgs::timestamp_t end);
  std::vector<dfmessages::ComponentRequest> create_all_decision_requests(std::vector<daqdataformats::SourceID> links,
                                                                         triggeralgs::timestamp_t start,
                                                                         triggeralgs::timestamp_t end);
  void add_requests_to_decision(dfmessages::TriggerDecision& decision,
                                std::vector<dfmessages::ComponentRequest> requests);
  */
  /* ROI
  bool m_use_roi_readout;
  struct roi_group
  {
    int n_links;
    float prob;
    triggeralgs::timestamp_t time_window;
    std::string mode;
  };
  std::map<int, roi_group> m_roi_conf;
  std::vector<const appmodel::ROIGroupConf*> m_roi_conf_data;
  void parse_roi_conf(const std::vector<const appmodel::ROIGroupConf*>& data);
  void print_roi_conf(std::map<int, roi_group> roi_conf);
  std::vector<int> m_roi_conf_ids;
  std::vector<float> m_roi_conf_probs;
  std::vector<float> m_roi_conf_probs_c;
  float get_random_num_float(float limit);
  int get_random_num_int();
  int pick_roi_group_conf();
  void roi_readout_make_requests(dfmessages::TriggerDecision& decision);

  int m_repeat_trigger_count{ 1 };
  */
  // paused state, in which we don't send triggers
  std::atomic<bool> m_paused;
  std::atomic<bool> m_dfo_is_busy;
  //std::atomic<bool> m_hsi_passthrough;
  //std::atomic<bool> m_tc_merging;

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

  /* New buffering
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
  */
  /* Bitwords logic
  bool m_use_bitwords;
  nlohmann::json m_trigger_bitwords_json;
  bool m_bitword_check;
  std::bitset<16> m_TD_bitword;
  std::vector<std::bitset<16>> m_trigger_bitwords;
  std::bitset<16> get_TD_bitword(const PendingTD& ready_td);
  void print_trigger_bitwords(std::vector<std::bitset<16>> trigger_bitwords);
  bool check_trigger_bitwords();
  void print_bitword_flags(nlohmann::json m_trigger_bitwords_json);
  void set_trigger_bitwords();
  void set_trigger_bitwords(const std::vector<std::string>& _bitwords);
  */
  /* Readout map config
  bool m_use_readout_map;
  std::vector<const appmodel::TCReadoutMap*>  m_readout_window_map_data;
  std::map<trgdataformats::TriggerCandidateData::Type, std::pair<triggeralgs::timestamp_t, triggeralgs::timestamp_t>>
    m_readout_window_map;
  void parse_readout_map(const std::vector<const appmodel::TCReadoutMap*>& data);
  void print_readout_map(std::map<trgdataformats::TriggerCandidateData::Type,
                                  std::pair<triggeralgs::timestamp_t, triggeralgs::timestamp_t>> map);
  */
  /* Create the next trigger decision
  dfmessages::TriggerDecision create_decision(const PendingTD& pending_td);
  dfmessages::trigger_type_t m_trigger_type_shifted;
  */
  /* Optional list of TC types to ignore
  std::vector<unsigned int> m_ignored_tc_types;
  bool m_ignoring_tc_types;
  bool check_trigger_type_ignore(unsigned int tc_type);
  */
  // Opmon variables
  using metric_counter_type = uint64_t ; //decltype(moduleleveltriggerinfo::Info::tc_received_count);
  std::atomic<metric_counter_type> m_td_msg_received_count{ 0 };
  std::atomic<metric_counter_type> m_td_sent_count{ 0 };
  std::atomic<metric_counter_type> m_td_total_count{ 0 };
  // DFO state related
  std::atomic<metric_counter_type> m_td_inhibited_count{ 0 };
  std::atomic<metric_counter_type> m_td_paused_count{ 0 };
  std::atomic<metric_counter_type> m_td_queue_timeout_expired_err_count{ 0 };
  // livetime related
  std::atomic<metric_counter_type> m_lc_kLive{ 0 };
  std::atomic<metric_counter_type> m_lc_kPaused{ 0 };
  std::atomic<metric_counter_type> m_lc_kDead{ 0 };
  bool m_lc_started = false;

  // Struct for per TC stats
  struct TDData {
    std::atomic<metric_counter_type> received{ 0 };
    std::atomic<metric_counter_type> sent{ 0 };
    std::atomic<metric_counter_type> failed_send{ 0 };
    std::atomic<metric_counter_type> paused{ 0 };
    std::atomic<metric_counter_type> inhibited{ 0 };
  };
  static std::set<trgdataformats::TriggerCandidateData::Type> unpack_types( const dfmessages::trigger_type_t& t) {
    std::set<trgdataformats::TriggerCandidateData::Type> results;
    if (t == dfmessages::TypeDefaults::s_invalid_trigger_type)
      return results;
    const std::bitset<64> bits(t);
    for( size_t i = 0; i < bits.size(); ++i ) {
      if ( bits[i] ) results.insert((trgdataformats::TriggerCandidateData::Type)i);
    }
    return results;
  }

  std::map<dunedaq::trgdataformats::TriggerCandidateData::Type, TDData> m_trigger_counters;

  std::mutex m_trigger_mutex;
  TDData & get_trigger_counter(trgdataformats::TriggerCandidateData::Type type) {
    auto it = m_trigger_counters.find(type);
    if (it != m_trigger_counters.end()) return it->second;
    
    std::lock_guard<std::mutex> guard(m_trigger_mutex);
    return m_trigger_counters[type];
  }

  // Create an instance of the Latency class
  std::atomic<bool> m_latency_monitoring{ false };
  dunedaq::trigger::Latency m_latency_instance;
  dunedaq::trigger::Latency m_latency_requests_instance{dunedaq::trigger::Latency::TimeUnit::Microseconds};
  std::atomic<metric_counter_type> m_latency_in{ 0 };
  std::atomic<metric_counter_type> m_latency_out{ 0 };
  std::atomic<metric_counter_type> m_latency_window_start{ 0 };
  std::atomic<metric_counter_type> m_latency_window_end{ 0 };

  void print_opmon_stats();
};
} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_PLUGINS_MODULELEVELTRIGGER_HPP_

// Local Variables:
// c-basic-offset: 2
// End:
