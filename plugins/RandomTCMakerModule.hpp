/**
 * This is part of the DUNE DAQ Software Suite, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_PLUGINS_RANDOMTRIGGERCANDIDATEMAKER_HPP_
#define TRIGGER_PLUGINS_RANDOMTRIGGERCANDIDATEMAKER_HPP_

#include "trigger/TokenManager.hpp"

#include "appfwk/ConfigurationManager.hpp"
#include "appfwk/DAQModule.hpp"
#include "confmodel/Connection.hpp"
#include "confmodel/DetectorConfig.hpp"
#include "confmodel/Session.hpp"

#include "appmodel/RandomTCMakerConf.hpp"
#include "appmodel/RandomTCMakerModule.hpp"
#include "appmodel/TCReadoutMap.hpp"

#include "daqdataformats/SourceID.hpp"
#include "dfmessages/TimeSync.hpp"
#include "dfmessages/TriggerDecision.hpp"
#include "dfmessages/TriggerDecisionToken.hpp"
#include "dfmessages/TriggerInhibit.hpp"
#include "dfmessages/Types.hpp"
#include "iomanager/Receiver.hpp"
#include "iomanager/Sender.hpp"
#include "trigger/Latency.hpp"
#include "trigger/opmon/latency_info.pb.h"
#include "trigger/opmon/randomtcmaker_info.pb.h"
#include "triggeralgs/TriggerCandidate.hpp"
#include "utilities/TimestampEstimatorBase.hpp"

#include "rcif/cmd/Nljs.hpp"

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

  RandomTCMakerModule(const RandomTCMakerModule&) = delete; ///< RandomTCMakerModule is not copy-constructible
  RandomTCMakerModule& operator=(const RandomTCMakerModule&) = delete; ///< RandomTCMakerModule is not copy-assignable
  RandomTCMakerModule(RandomTCMakerModule&&) = delete;            ///< RandomTCMakerModule is not move-constructible
  RandomTCMakerModule& operator=(RandomTCMakerModule&&) = delete; ///< RandomTCMakerModule is not move-assignable

  void init(std::shared_ptr<appfwk::ConfigurationManager> mcfg) override;
  void generate_opmon_data() override;

private:
  using TCType = triggeralgs::TriggerCandidate::Type;
  // Commands
  void do_configure(const CommandData_t& obj);
  void do_start(const CommandData_t& obj);
  void do_stop(const CommandData_t& obj);
  void do_scrap(const CommandData_t& obj);

  /**
   * @brief Command function to change the output trigger rate
   *
   * @param obj descriprice json object with the ChangeRateParams
   */
  void do_change_trigger_rate(const CommandData_t& obj);

  void send_trigger_candidates();
  std::thread m_send_trigger_candidates_thread;

  std::unique_ptr<utilities::TimestampEstimatorBase> m_timestamp_estimator;

  // Create the next trigger decision
  triggeralgs::TriggerCandidate create_candidate(dfmessages::timestamp_t timestamp);

  // Queue sources and sinks
  std::shared_ptr<iomanager::ReceiverConcept<dfmessages::TimeSync>> m_time_sync_source;
  std::shared_ptr<iomanager::SenderConcept<triggeralgs::TriggerCandidate>> m_trigger_candidate_sink;

  // confs
  const dunedaq::appmodel::RandomTCMakerModule* m_mtrg;
  const appmodel::RandomTCMakerConf* m_conf;

  /// @brief Output TC type
  TCType m_tcout_type;

  /// @brief Output candidate central time, based off trigger timestamp
  dfmessages::timestamp_t m_tcout_time_offset_ts;
  /// @brief Output candidate start time, based off trigger timestamp
  dfmessages::timestamp_t m_tcout_time_before_ts;
  /// @brief Output candidate end time, based off trigger timestamp
  dfmessages::timestamp_t m_tcout_time_after_ts;

  /// @brief Clock speed in hz, taken from detector configuration
  uint64_t m_clock_speed_hz;

  /// @brief Output trigger rate in hz
  std::atomic<float> m_trigger_rate_hz{ 0 };

  uint64_t get_interval(std::mt19937& gen);

  dfmessages::run_number_t m_run_number;

  // Are we in the RUNNING state?
  std::atomic<bool> m_running_flag{ false };

  // OpMon variables
  using metric_counter_type = uint64_t; // decltype(randomtriggercandidatemakerinfo::Info::tc_sent_count);
  std::atomic<metric_counter_type> m_tc_made_count{ 0 };
  std::atomic<metric_counter_type> m_tc_sent_count{ 0 };
  std::atomic<metric_counter_type> m_tc_failed_sent_count{ 0 };
  void print_opmon_stats();

  // Create an instance of the Latency class
  std::atomic<bool> m_latency_monitoring{ false };
  dunedaq::trigger::Latency m_latency_instance;
};
} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_PLUGINS_RANDOMTRIGGERCANDIDATEMAKER_HPP_
