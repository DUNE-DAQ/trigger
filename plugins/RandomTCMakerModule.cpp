/**
 * @file RandomTCMakerModule.cpp RandomTCMakerModule class
 * implementation
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "RandomTCMakerModule.hpp"

#include "trigger/Issues.hpp"

#include "daqdataformats/ComponentRequest.hpp"
#include "trgdataformats/Types.hpp"
#include "dfmessages/TimeSync.hpp"
#include "dfmessages/TriggerDecision.hpp"
#include "dfmessages/TriggerInhibit.hpp"
#include "dfmessages/Types.hpp"
#include "iomanager/IOManager.hpp"
#include "logging/Logging.hpp"
#include "utilities/TimestampEstimator.hpp"
#include "utilities/TimestampEstimatorSystem.hpp"
#include "triggeralgs/TriggerCandidate.hpp"

#include <algorithm>
#include <cassert>
#include <pthread.h>
#include <random>
#include <string>
#include <vector>

namespace dunedaq {

namespace trigger {

RandomTCMakerModule::RandomTCMakerModule(const std::string& name)
  : DAQModule(name)
  , m_time_sync_source(nullptr)
  , m_trigger_candidate_sink(nullptr)
  , m_run_number(0)
{
  register_command("conf", &RandomTCMakerModule::do_configure);
  register_command("start", &RandomTCMakerModule::do_start);
  register_command("stop_trigger_sources", &RandomTCMakerModule::do_stop);
  register_command("scrap", &RandomTCMakerModule::do_scrap);
  register_command("change_rate", &RandomTCMakerModule::do_change_trigger_rate);
}

void
RandomTCMakerModule::init(std::shared_ptr<appfwk::ModuleConfiguration> mcfg)
{
  auto mtrg = mcfg->module<appmodel::RandomTCMakerModule>(get_name());

  for(auto con: mtrg->get_outputs()){
    TLOG() << "TC sink is " << con->class_name() << "@" << con->UID();
    m_trigger_candidate_sink =
        get_iom_sender<triggeralgs::TriggerCandidate>(con->UID());
  }
  for(auto con: mtrg->get_inputs()) {
  // Get the time sync source
     m_time_sync_source = get_iom_receiver<dfmessages::TimeSync>(con->UID());
  }
  m_conf = mtrg->get_configuration();

  // Get the TC out configuration
  m_tc_readout = m_conf->get_tc_readout();

  m_latency_monitoring.store( m_conf->get_latency_monitoring() );

  // Get the clock speed from detector configuration
  m_clock_speed_hz = mcfg->configuration_manager()->session()->get_detector_configuration()->get_clock_speed_hz();
  m_trigger_rate_hz = m_conf->get_trigger_rate_hz();
  TLOG() << "Clock speed is: " << m_clock_speed_hz;
  TLOG() << "Output trigger rate is: " << m_trigger_rate_hz;
}

void
RandomTCMakerModule::generate_opmon_data()
{
  opmon::RandomTCMakerInfo info;

  info.set_tc_made_count( m_tc_made_count.load() );
  info.set_tc_sent_count( m_tc_sent_count.load() );
  info.set_tc_failed_sent_count( m_tc_failed_sent_count.load() );

  this->publish(std::move(info));

  if ( m_latency_monitoring.load() && m_running_flag.load() ) {
    opmon::TriggerLatencyStandalone lat_info;

    lat_info.set_latency_out( m_latency_instance.get_latency_out() );

    this->publish(std::move(lat_info));
  }
}

void
RandomTCMakerModule::do_configure(const nlohmann::json& /*obj*/)
{
  //m_conf = obj.get<randomtriggercandidatemaker::Conf>();
}

void
RandomTCMakerModule::do_start(const nlohmann::json& obj)
{
  m_run_number = obj.value<dunedaq::daqdataformats::run_number_t>("run", 0);

  m_running_flag.store(true);

  // OpMon.
  m_tc_made_count.store(0);
  m_tc_sent_count.store(0);
  m_tc_failed_sent_count.store(0);

  std::string timestamp_method = m_conf->get_timestamp_method();
  if (timestamp_method == "kTimeSync") {
    TLOG_DEBUG(0) << "Creating TimestampEstimator";
    m_timestamp_estimator.reset(new utilities::TimestampEstimator(m_run_number, m_clock_speed_hz));
    m_time_sync_source->add_callback(std::bind(&utilities::TimestampEstimator::timesync_callback<dfmessages::TimeSync>,
          reinterpret_cast<utilities::TimestampEstimator*>(m_timestamp_estimator.get()),
          std::placeholders::_1));
  }
  else if(timestamp_method == "kSystemClock"){
    TLOG_DEBUG(0) << "Creating TimestampEstimatorSystem";
    m_timestamp_estimator.reset(new utilities::TimestampEstimatorSystem(m_clock_speed_hz));
  }
  else{
    // TODO: write some error message
  }

  // Check if the trigger rate was changed in the starting parameters, and set it if so
  auto start_params = obj.get<rcif::cmd::StartParams>();
  if (start_params.trigger_rate > 0) {
    TLOG() << " Changing rate: trigger_rate "
      << start_params.trigger_rate;
    m_trigger_rate_hz = start_params.trigger_rate;
  }

  m_send_trigger_candidates_thread = std::thread(&RandomTCMakerModule::send_trigger_candidates, this);
  pthread_setname_np(m_send_trigger_candidates_thread.native_handle(), "random-tc-maker");
}

void
RandomTCMakerModule::do_stop(const nlohmann::json& /*obj*/)
{
  m_running_flag.store(false);

  m_send_trigger_candidates_thread.join();

  m_time_sync_source->remove_callback();
  m_timestamp_estimator.reset(nullptr); // Calls TimestampEstimator dtor

  print_opmon_stats();
}

void
RandomTCMakerModule::do_scrap(const nlohmann::json& /*obj*/)
{
  m_configured_flag.store(false);
}


void
RandomTCMakerModule::do_change_trigger_rate(const nlohmann::json& obj)
{
  auto change_rate_params = obj.get<rcif::cmd::ChangeRateParams>();

  TLOG() << "Changing trigger rate from " << m_trigger_rate_hz << " to " << change_rate_params.trigger_rate;

  m_trigger_rate_hz = change_rate_params.trigger_rate;
}

triggeralgs::TriggerCandidate
RandomTCMakerModule::create_candidate(dfmessages::timestamp_t timestamp)
{
  triggeralgs::TriggerCandidate candidate;
  candidate.time_start = (timestamp - m_tc_readout->get_time_before());
  candidate.time_end = (timestamp + m_tc_readout->get_time_after());
  candidate.time_candidate = timestamp;
  candidate.detid = { 0 };
  candidate.type = static_cast<triggeralgs::TriggerCandidate::Type>(m_tc_readout->get_candidate_type());

  // Throw error if unknown TC type
  if (candidate.type == triggeralgs::TriggerCandidate::Type::kUnknown) {
            throw InvalidConfiguration(ERS_HERE);
  }

  // TODO: Originally kHSIEventToTriggerCandidate
  candidate.algorithm = triggeralgs::TriggerCandidate::Algorithm::kCustom;

  return candidate;
}

int
RandomTCMakerModule::get_interval(std::mt19937& gen)
{
  std::string time_distribution = m_conf->get_time_distribution();

  int interval = m_trigger_rate_hz * m_clock_speed_hz;

  if( time_distribution == "kUniform"){
    return interval;
  }
  else if(time_distribution == "kPoisson"){
    std::exponential_distribution<double> d(1.0 / interval);
    return static_cast<int>(0.5 + d(gen));
  }
  else{
    TLOG_DEBUG(1) << get_name() << " unknown distribution! Using kUniform.";
  }
  return interval;
}

void
RandomTCMakerModule::send_trigger_candidates()
{
  // OpMon.
  m_tc_sent_count.store(0);

  std::mt19937 gen(m_run_number);
  // Wait for there to be a valid timestamp estimate before we start
  if (m_timestamp_estimator->wait_for_valid_timestamp(m_running_flag) ==
      utilities::TimestampEstimatorBase::kInterrupted) {
    return;
  }

  dfmessages::timestamp_t initial_timestamp = m_timestamp_estimator->get_timestamp_estimate();
  dfmessages::timestamp_t next_trigger_timestamp = initial_timestamp;
  TLOG_DEBUG(1) << get_name() << " initial timestamp estimate is " << initial_timestamp;

  while (m_running_flag.load()) {
    if (m_timestamp_estimator->wait_for_timestamp(next_trigger_timestamp, m_running_flag) ==
        utilities::TimestampEstimatorBase::kInterrupted) {
      break;
    }
    next_trigger_timestamp = m_timestamp_estimator->get_timestamp_estimate();
    triggeralgs::TriggerCandidate candidate = create_candidate(next_trigger_timestamp);

    m_tc_made_count++;

    TLOG_DEBUG(1) << get_name() << " at timestamp " << m_timestamp_estimator->get_timestamp_estimate()
                  << ", pushing a candidate with timestamp " << candidate.time_candidate;

    if (m_latency_monitoring.load()) m_latency_instance.update_latency_out( candidate.time_candidate );
    try{
      m_trigger_candidate_sink->send(std::move(candidate), std::chrono::milliseconds(10));
      m_tc_sent_count++;
    } catch (const ers::Issue& e) {
      ers::error(e);
      m_tc_failed_sent_count++;
    }

    next_trigger_timestamp += get_interval(gen);
  }
}

void
RandomTCMakerModule::print_opmon_stats()
{
  TLOG() << "RandomTCMaker opmon counters summary:";
  TLOG() << "------------------------------";
  TLOG() << "Made TCs: \t\t" << m_tc_made_count;
  TLOG() << "Sent TCs: \t\t" << m_tc_sent_count;
  TLOG() << "Failed to send TCs: \t" << m_tc_failed_sent_count;
  TLOG();
}

} // namespace trigger
} // namespace dunedaq

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::RandomTCMakerModule)

// Local Variables:
// c-basic-offset: 2
// End:
