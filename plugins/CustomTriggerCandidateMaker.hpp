/**
 * This is part of the DUNE DAQ Software Suite, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_PLUGINS_CUSTOMTRIGGERCANDIDATEMAKER_HPP_
#define TRIGGER_PLUGINS_CUSTOMTRIGGERCANDIDATEMAKER_HPP_

#include "trigger/TokenManager.hpp"
#include "trigger/customtriggercandidatemaker/Nljs.hpp"
#include "trigger/customtriggercandidatemakerinfo/InfoNljs.hpp"

#include "appfwk/DAQModule.hpp"
#include "daqdataformats/SourceID.hpp"
#include "dfmessages/TimeSync.hpp"
#include "dfmessages/TriggerDecision.hpp"
#include "dfmessages/TriggerDecisionToken.hpp"
#include "dfmessages/TriggerInhibit.hpp"
#include "dfmessages/Types.hpp"
#include "iomanager/Receiver.hpp"
#include "iomanager/Sender.hpp"
#include "timinglibs/TimestampEstimator.hpp"
#include "triggeralgs/TriggerCandidate.hpp"

#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace dunedaq {

namespace trigger {

/**
 * @brief CustomTriggerCandidateMaker creates TriggerCandidates of specified types at configurable intervals, based on
 * input from a TimeSync queue or the system clock. The TCs can be fed directly into the MLT.
 */
class CustomTriggerCandidateMaker : public dunedaq::appfwk::DAQModule
{
public:
  /**
   * @brief CustomTriggerCandidateMaker Constructor
   * @param name Instance name for this CustomTriggerCandidateMaker instance
   */
  explicit CustomTriggerCandidateMaker(const std::string& name);

  CustomTriggerCandidateMaker(const CustomTriggerCandidateMaker&) =
    delete; ///< CustomTriggerCandidateMaker is not copy-constructible
  CustomTriggerCandidateMaker& operator=(const CustomTriggerCandidateMaker&) =
    delete; ///< CustomTriggerCandidateMaker is not copy-assignable
  CustomTriggerCandidateMaker(CustomTriggerCandidateMaker&&) =
    delete; ///< CustomTriggerCandidateMaker is not move-constructible
  CustomTriggerCandidateMaker& operator=(CustomTriggerCandidateMaker&&) =
    delete; ///< CustomTriggerCandidateMaker is not move-assignable

  void init(const nlohmann::json& iniobj) override;
  void get_info(opmonlib::InfoCollector& ci, int level) override;

private:
  // Commands
  void do_configure(const nlohmann::json& obj);
  void do_start(const nlohmann::json& obj);
  void do_stop(const nlohmann::json& obj);
  void do_scrap(const nlohmann::json& obj);

  void send_trigger_candidates();
  std::thread m_send_trigger_candidates_thread;

  std::unique_ptr<timinglibs::TimestampEstimatorBase> m_timestamp_estimator;

  // Create the next trigger decision
  triggeralgs::TriggerCandidate create_candidate(dfmessages::timestamp_t timestamp, int tc_type);

  // Queue sources and sinks
  std::shared_ptr<iomanager::SenderConcept<triggeralgs::TriggerCandidate>> m_trigger_candidate_sink;

  // Config parameters
  customtriggercandidatemaker::Conf m_conf;
  std::vector<std::pair<int, long int>> m_tc_settings;
  void print_config();

  // Logic for timestamps
  int sorting_size_limit;
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
  using metric_counter_type = decltype(customtriggercandidatemakerinfo::Info::tc_sent_count);
  std::atomic<metric_counter_type> m_tc_sent_count{ 0 };
};
} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_PLUGINS_CUSTOMTRIGGERCANDIDATEMAKER_HPP_
