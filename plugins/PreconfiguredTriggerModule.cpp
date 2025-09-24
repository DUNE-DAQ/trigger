/**
 * @file PreconfiguredTriggerModule.cpp Implementation of a Preconfigured Trigger
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "PreconfiguredTriggerModule.hpp"

#include "appmodel/PreconfiguredTriggerModule.hpp"
#include "appmodel/PreconfiguredTriggerModuleConf.hpp"
#include "appmodel/PreconfiguredTriggerModuleTrigger.hpp"
#include "appmodel/SourceIDConf.hpp"
#include "trigger/opmon/latency_info.pb.h"
#include "trigger/opmon/randomtcmaker_info.pb.h"
#include "trigger/TriggerCandidate_serialization.hpp"
#include "appmodel/TCReadoutMap.hpp"

namespace dunedaq::trigger {
PreconfiguredTriggerModule::PreconfiguredTriggerModule(const std::string& module_name)
  : appfwk::DAQModule(module_name)
  , m_send_trigger_candidates_thread(
      std::bind(&PreconfiguredTriggerModule::send_trigger_candidates, this, std::placeholders::_1))
{
  register_command("enable_triggers", &PreconfiguredTriggerModule::do_enable_triggers);
}

void
PreconfiguredTriggerModule::init(std::shared_ptr<appfwk::ConfigurationManager> cfg)
{
  auto mdal = cfg->get_dal<appmodel::PreconfiguredTriggerModule>(get_name());

  if (!mdal) {
    throw appfwk::CommandFailed(ERS_HERE, "init", get_name(), "Unable to retrieve configuration object");
  }

  auto iom = iomanager::IOManager::get();
  for (auto con : mdal->get_outputs()) {
    if (con->get_data_type() == datatype_to_string<triggeralgs::TriggerCandidate>()) {
      m_trigger_sender = iom->get_sender<triggeralgs::TriggerCandidate>(con->UID());
    }
  }

  if (m_trigger_sender == nullptr) {
    throw appfwk::MissingConnection(ERS_HERE, get_name(), "TriggerCandidate", "output");
  }

  m_conf = mdal->get_configuration();

  m_latency_monitoring.store(m_conf->get_latency_monitoring());
  m_wait_time = std::chrono::milliseconds(m_conf->get_wait_time_ms());

  // Get the TC out configuration
  const appmodel::TCReadoutMap* tc_readout = m_conf->get_tc_readout();
  m_tcout_type =
    static_cast<TCType>(dunedaq::trgdataformats::string_to_trigger_candidate_type(tc_readout->get_tc_type_name()));
}

void
PreconfiguredTriggerModule::generate_opmon_data()
{
  opmon::RandomTCMakerInfo info;

  info.set_tc_made_count(m_tc_made_count.load());
  info.set_tc_sent_count(m_tc_sent_count.load());
  info.set_tc_failed_sent_count(m_tc_failed_sent_count.load());

  this->publish(std::move(info));

  if (m_latency_monitoring.load()) {
    opmon::TriggerLatencyStandalone lat_info;

    lat_info.set_latency_out(m_latency_instance.get_latency_out());

    this->publish(std::move(lat_info));
  }
}

void
PreconfiguredTriggerModule::do_enable_triggers(const CommandData_t& /*cmd*/)
{
  m_send_trigger_candidates_thread.start_working_thread("PCTRIG");
}
void
PreconfiguredTriggerModule::do_disable_triggers(const CommandData_t& /*cmd*/)
{
  m_send_trigger_candidates_thread.stop_working_thread();
}



triggeralgs::TriggerCandidate
PreconfiguredTriggerModule::create_candidate(dfmessages::timestamp_t time_start, dfmessages::timestamp_t time_end)
{
  triggeralgs::TriggerCandidate candidate;
  candidate.time_start = time_start;
  candidate.time_end = time_end;
  candidate.time_candidate = time_start;
  candidate.detid = { 0 };
  candidate.type = m_tcout_type;

  // TODO: Originally kHSIEventToTriggerCandidate
  candidate.algorithm = triggeralgs::TriggerCandidate::Algorithm::kCustom;

  return candidate;
}

void
PreconfiguredTriggerModule::send_trigger_candidates(std::atomic<bool>& running_flag)
{
  auto triggers = m_conf->get_triggers();
  for (auto& trigger : triggers) {
    if (!running_flag.load())
      break;

    auto candidate = create_candidate(trigger->get_timestamp_start(), trigger->get_timestamp_end());

    m_tc_made_count++;

    if (m_latency_monitoring.load())
      m_latency_instance.update_latency_out(candidate.time_candidate);
    
    try {
      m_trigger_sender->send(std::move(candidate), std::chrono::milliseconds(10));
      m_tc_sent_count++;
    } catch (const ers::Issue& e) {
      ers::error(e);
      m_tc_failed_sent_count++;
    }

    std::this_thread::sleep_for(m_wait_time);
  }
}

} // namespace dunedaq::trigger

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::PreconfiguredTriggerModule)
