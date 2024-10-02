/**
 * This is part of the DUNE DAQ Software Suite, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_PLUGINS_RANDOMTRIGGERCANDIDATEMAKER_HPP_
#define TRIGGER_PLUGINS_RANDOMTRIGGERCANDIDATEMAKER_HPP_

#include "trigger/TokenManager.hpp"

#include "appfwk/DAQModule.hpp"
#include "appfwk/ModuleConfiguration.hpp"
#include "confmodel/Connection.hpp"

#include "appmodel/RandomTCMakerConf.hpp"
#include "appmodel/RandomTCMakerModule.hpp"

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
#include "trigger/opmon/randomtcmaker_info.pb.h"

#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace dunedaq {

namespace trigger {

/**
 * @brief RandomTCMakerModule creates TriggerCandidates at regular or
 * Poisson random intervals, based on input from a TimeSync queue or the system
 * clock. The TCs can be fed directly into the MLT.
 */
class RandomTCMakerModule : public dunedaq::appfwk::DAQModule
{
public:
  /**
   * @brief RandomTCMakerModule Constructor
   * @param name Instance name for this RandomTCMakerModule instance
   */
  explicit RandomTCMakerModule(const std::string& name);

  RandomTCMakerModule(const RandomTCMakerModule&) =
    delete; ///< RandomTCMakerModule is not copy-constructible
  RandomTCMakerModule& operator=(const RandomTCMakerModule&) =
    delete; ///< RandomTCMakerModule is not copy-assignable
  RandomTCMakerModule(RandomTCMakerModule&&) =
    delete; ///< RandomTCMakerModule is not move-constructible
  RandomTCMakerModule& operator=(RandomTCMakerModule&&) =
    delete; ///< RandomTCMakerModule is not move-assignable

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
  triggeralgs::TriggerCandidate create_candidate(dfmessages::timestamp_t timestamp);

  // Queue sources and sinks
  std::shared_ptr<iomanager::ReceiverConcept<dfmessages::TimeSync>> m_time_sync_source;
  std::shared_ptr<iomanager::SenderConcept<triggeralgs::TriggerCandidate>> m_trigger_candidate_sink;

  //randomtriggercandidatemaker::Conf m_conf;
  const appmodel::RandomTCMakerConf* m_conf;

  int get_interval(std::mt19937& gen);

  dfmessages::run_number_t m_run_number;

  // Are we in the RUNNING state?
  std::atomic<bool> m_running_flag{ false };
  // Are we in a configured state, ie after conf and before scrap?
  std::atomic<bool> m_configured_flag{ false };

  // OpMon variables
  using metric_counter_type = uint64_t; //decltype(randomtriggercandidatemakerinfo::Info::tc_sent_count);
  std::atomic<metric_counter_type> m_tc_made_count{ 0 };
  std::atomic<metric_counter_type> m_tc_sent_count{ 0 };
  std::atomic<metric_counter_type> m_tc_failed_sent_count{ 0 };
  void print_opmon_stats();
};
} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_PLUGINS_RANDOMTRIGGERCANDIDATEMAKER_HPP_
