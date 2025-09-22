/**
 * @file PreconfiguredTriggerModule.hpp Declarations for Preconfigured Trigger Module
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_PLUGINS_PRECONFIGUREDTRIGGERMODULE_HPP_
#define TRIGGER_PLUGINS_PRECONFIGUREDTRIGGERMODULE_HPP_

#include "appfwk/DAQModule.hpp"
#include "triggeralgs/TriggerCandidate.hpp"
#include "appmodel/PreconfiguredTriggerModuleConf.hpp"
#include "utilities/WorkerThread.hpp"
#include "dfmessages/Types.hpp"
#include "iomanager/IOManager.hpp"
#include "trigger/Latency.hpp"

namespace dunedaq::trigger {
class PreconfiguredTriggerModule : public appfwk::DAQModule
{
public:
  explicit PreconfiguredTriggerModule(const std::string& module_name);
  PreconfiguredTriggerModule(const PreconfiguredTriggerModule&) =
    delete; ///< PreconfiguredTriggerModule is not copy-constructible
  PreconfiguredTriggerModule& operator=(const PreconfiguredTriggerModule&) =
    delete; ///< PreconfiguredTriggerModule is not copy-assignable
  PreconfiguredTriggerModule(PreconfiguredTriggerModule&&) =
    delete; ///< PreconfiguredTriggerModule is not move-constructible
  PreconfiguredTriggerModule& operator=(PreconfiguredTriggerModule&&) =
    delete; ///< PreconfiguredTriggerModule is not move-assignable

  void init(std::shared_ptr<appfwk::ConfigurationManager> cfg) override;
  void generate_opmon_data() override;

private:
  using TCType = triggeralgs::TriggerCandidate::Type;
  // Commands
  void do_enable_triggers(const CommandData_t& cmd);
  void do_disable_triggers(const CommandData_t& cmd);

  triggeralgs::TriggerCandidate create_candidate(dfmessages::timestamp_t time_start, dfmessages::timestamp_t time_end);
  void send_trigger_candidates(std::atomic<bool>&);
  utilities::WorkerThread m_send_trigger_candidates_thread;

  // Configuration
  const appmodel::PreconfiguredTriggerModuleConf* m_conf;
  std::chrono::milliseconds m_wait_time;
  /// @brief Output TC type
  TCType m_tcout_type;

  // Runtime
  std::shared_ptr<iomanager::SenderConcept<triggeralgs::TriggerCandidate>> m_trigger_sender;

  // Monitoring
  using metric_counter_type = uint64_t; // decltype(randomtriggercandidatemakerinfo::Info::tc_sent_count);
  std::atomic<metric_counter_type> m_tc_made_count{ 0 };
  std::atomic<metric_counter_type> m_tc_sent_count{ 0 };
  std::atomic<metric_counter_type> m_tc_failed_sent_count{ 0 };

  // Create an instance of the Latency class
  std::atomic<bool> m_latency_monitoring{ false };
  dunedaq::trigger::Latency m_latency_instance;
};
} // namespace dunedaq::trigger

#endif // TRIGGER_PLUGINS_PRECONFIGUREDTRIGGERMODULE_HPP_
