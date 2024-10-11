/**
 * This is part of the DUNE DAQ Software Suite, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_PLUGINS_CUSTOMTRIGGERCANDIDATEMAKER_HPP_
#define TRIGGER_PLUGINS_CUSTOMTRIGGERCANDIDATEMAKER_HPP_

#include "trigger/TokenManager.hpp"

#include "appmodel/CustomTCMaker.hpp"
#include "appmodel/CustomTCMakerConf.hpp"
#include "confmodel/Connection.hpp"

#include "appfwk/ModuleConfiguration.hpp"
#include "appfwk/DAQModule.hpp"
#include "daqdataformats/SourceID.hpp"
#include "dfmessages/TimeSync.hpp"
#include "dfmessages/TriggerDecision.hpp"
#include "dfmessages/TriggerDecisionToken.hpp"
#include "dfmessages/TriggerInhibit.hpp"
#include "dfmessages/Types.hpp"
#include "iomanager/Receiver.hpp"
#include "iomanager/Sender.hpp"
#include "utilities/TimestampEstimator.hpp"
#include "triggeralgs/TriggerCandidate.hpp"
#include "trigger/Latency.hpp"
#include "trigger/opmon/customtcmaker_info.pb.h"
#include "trigger/opmon/latency_info.pb.h"

#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace dunedaq {

namespace trigger {

/**
 * @brief CustomTCMaker creates TriggerCandidates of specified types at configurable intervals, based on
 * input from a TimeSync queue or the system clock. The TCs can be fed directly into the MLT.
 */
class CustomTCMaker : public dunedaq::appfwk::DAQModule
{
public:
  /**
   * @brief CustomTCMaker Constructor
   * @param name Instance name for this CustomTCMaker instance
   */
  explicit CustomTCMaker(const std::string& name);

  CustomTCMaker(const CustomTCMaker&) =
    delete; ///< CustomTCMaker is not copy-constructible
  CustomTCMaker& operator=(const CustomTCMaker&) =
    delete; ///< CustomTCMaker is not copy-assignable
  CustomTCMaker(CustomTCMaker&&) =
    delete; ///< CustomTCMaker is not move-constructible
  CustomTCMaker& operator=(CustomTCMaker&&) =
    delete; ///< CustomTCMaker is not move-assignable

  void init(std::shared_ptr<appfwk::ModuleConfiguration> mcfg) override;
  void generate_opmon_data() override;

private:
  // Commands
  void do_configure(const nlohmann::json& obj);
  void do_start(const nlohmann::json& obj);
  void do_stop(const nlohmann::json& obj);
  void do_scrap(const nlohmann::json& obj);

  void send_trigger_candidates();
  std::thread m_send_trigger_candidates_thread;

  std::unique_ptr<utilities::TimestampEstimatorBase> m_timestamp_estimator;

  // Create the next trigger decision
  triggeralgs::TriggerCandidate create_candidate(dfmessages::timestamp_t timestamp, int tc_type);

  // Queue sources and sinks
  std::shared_ptr<iomanager::ReceiverConcept<dfmessages::TimeSync>> m_time_sync_source;
  std::shared_ptr<iomanager::SenderConcept<triggeralgs::TriggerCandidate>> m_trigger_candidate_sink;

  // Config parameters
  const appmodel::CustomTCMakerConf* m_conf;
  std::vector<std::pair<int, long int>> m_tc_settings;
  void print_config();

  // Logic for timestamps
  uint64_t m_sorting_size_limit;
  std::vector<std::pair<int, dfmessages::timestamp_t>> m_tc_timestamps;
  dfmessages::timestamp_t m_initial_timestamp;
  dfmessages::timestamp_t m_next_trigger_timestamp;
  int m_next_trigger_type;
  std::map<int, dfmessages::timestamp_t> m_last_timestamps_of_type;
  std::vector<std::pair<int, dfmessages::timestamp_t>> get_initial_timestamps(
    dfmessages::timestamp_t initial_timestamp);
  std::vector<std::pair<int, dfmessages::timestamp_t>> get_next_timestamps(
    std::map<int, dfmessages::timestamp_t> last_timestamps);
  std::vector<std::pair<int, dfmessages::timestamp_t>> get_next_ts_of_type(int tc_type,
                                                                           long int tc_interval,
                                                                           dfmessages::timestamp_t last_ts_of_type);
  void print_timestamps_vector(std::vector<std::pair<int, dfmessages::timestamp_t>> timestamps);
  std::map<int, int> m_tc_sent_count_type;
  void print_final_tc_counts(std::map<int, int> counts);

  // Are we in the RUNNING state?
  std::atomic<bool> m_running_flag{ false };
  // Are we in a configured state, ie after conf and before scrap?
  std::atomic<bool> m_configured_flag{ false };

  // OpMon variables
  using metric_counter_type = uint64_t;
  std::atomic<metric_counter_type> m_tc_made_count{ 0 };
  std::atomic<metric_counter_type> m_tc_sent_count{ 0 };
  std::atomic<metric_counter_type> m_tc_failed_sent_count{ 0 };
  void print_opmon_stats();

  // Create an instance of the Latency class
  std::atomic<bool> m_latency_monitoring{ false };
  dunedaq::trigger::Latency m_latency_instance;
  std::atomic<metric_counter_type> m_latency_out{ 0 };
};
} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_PLUGINS_CUSTOMTRIGGERCANDIDATEMAKER_HPP_
