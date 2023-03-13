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

/** UPDATE THIS !!!
 * @brief CustomTriggerCandidateMaker creates TriggerCandidates at regular or
 * Poisson random intervals, based on input from a TimeSync queue or the system
 * clock. The TCs can be fed directly into the MLT.
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
  triggeralgs::TriggerCandidate create_candidate(dfmessages::timestamp_t timestamp);

  // Queue sources and sinks
  std::shared_ptr<iomanager::SenderConcept<triggeralgs::TriggerCandidate>> m_trigger_candidate_sink;

  // Config parameters
  customtriggercandidatemaker::Conf m_conf;
  std::vector<int> m_tc_types;
  std::vector<long int> m_tc_intervals;
  void print_config();

  int get_interval(std::mt19937& gen);

  dfmessages::run_number_t m_run_number;

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
