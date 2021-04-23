/**
 * @file RandomTriggerCandidateMaker.cpp RandomTriggerCandidateMaker class
 * implementation
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "RandomTriggerCandidateMaker.hpp"

#include "dataformats/ComponentRequest.hpp"

#include "dfmessages/TimeSync.hpp"
#include "dfmessages/TriggerDecision.hpp"
#include "dfmessages/TriggerInhibit.hpp"
#include "dfmessages/Types.hpp"
#include "triggeralgs/TriggerCandidate.hpp"
#include "logging/Logging.hpp"

#include "trigger/Issues.hpp"
#include "trigger/TimestampEstimator.hpp"
#include "trigger/TimestampEstimatorSystem.hpp"

#include "appfwk/app/Nljs.hpp"
#include "appfwk/DAQModuleHelper.hpp"
#include "triggeralgs/TriggerCandidateType.hpp"

#include <algorithm>
#include <cassert>
#include <pthread.h>
#include <random>
#include <string>
#include <vector>

namespace dunedaq {
namespace trigger {

RandomTriggerCandidateMaker::RandomTriggerCandidateMaker(const std::string& name)
  : DAQModule(name)
  , m_time_sync_source(nullptr)
  , m_trigger_candidate_sink(nullptr)
  , m_run_number(0)
{
  register_command("conf", &RandomTriggerCandidateMaker::do_configure);
  register_command("start", &RandomTriggerCandidateMaker::do_start);
  register_command("stop", &RandomTriggerCandidateMaker::do_stop);
  register_command("scrap", &RandomTriggerCandidateMaker::do_scrap);
}

void
RandomTriggerCandidateMaker::init(const nlohmann::json& obj)
{
  m_time_sync_source.reset(new appfwk::DAQSource<dfmessages::TimeSync>(appfwk::queue_inst(obj,"time_sync_source")));
  m_trigger_candidate_sink.reset(new appfwk::DAQSink<triggeralgs::TriggerCandidate>(appfwk::queue_inst(obj, "trigger_candidate_sink")));
}

void
RandomTriggerCandidateMaker::get_info(opmonlib::InfoCollector& /*ci*/, int /*level*/)
{
}

void
RandomTriggerCandidateMaker::do_configure(const nlohmann::json& obj)
{
  m_conf = obj.get<randomtriggercandidatemaker::ConfParams>();
}

void
RandomTriggerCandidateMaker::do_start(const nlohmann::json& obj)
{
  m_run_number = obj.value<dunedaq::dataformats::run_number_t>("run", 0);

  m_running_flag.store(true);

  switch (m_conf.timestamp_method) {
    case randomtriggercandidatemaker::timestamp_estimation::kTimeSync:
      TLOG_DEBUG(0) << "Creating TimestampEstimator";
      m_timestamp_estimator.reset(new TimestampEstimator(m_time_sync_source, m_conf.clock_frequency_hz));
      break;
    case randomtriggercandidatemaker::timestamp_estimation::kSystemClock:
      TLOG_DEBUG(0) << "Creating TimestampEstimatorSystem";
      m_timestamp_estimator.reset(new TimestampEstimatorSystem(m_conf.clock_frequency_hz));
      break;
  }

  m_send_trigger_candidates_thread = std::thread(&RandomTriggerCandidateMaker::send_trigger_candidates, this);
  pthread_setname_np(m_send_trigger_candidates_thread.native_handle(), "random-tc-maker");
}

void
RandomTriggerCandidateMaker::do_stop(const nlohmann::json& /*obj*/)
{
  m_running_flag.store(false);

  m_send_trigger_candidates_thread.join();

  m_timestamp_estimator.reset(nullptr); // Calls TimestampEstimator dtor
}

void
RandomTriggerCandidateMaker::do_scrap(const nlohmann::json& /*obj*/)
{
  m_configured_flag.store(false);
}

triggeralgs::TriggerCandidate
RandomTriggerCandidateMaker::create_candidate(dfmessages::timestamp_t timestamp)
{
  triggeralgs::TriggerCandidate candidate;
  candidate.time_start = timestamp;
  candidate.time_end = timestamp;
  candidate.time_candidate = timestamp;
  candidate.detid = { 0 };
  candidate.type = triggeralgs::TriggerCandidateType::kRandom;
  candidate.algorithm = 0;
  candidate.version = 0;

  return candidate;
}

int
RandomTriggerCandidateMaker::get_interval(std::mt19937 &gen)
{
  switch (m_conf.time_distribution) {
    case randomtriggercandidatemaker::distribution_type::kUniform:
      return m_conf.trigger_interval_ticks;
    case randomtriggercandidatemaker::distribution_type::kPoisson:
      std::exponential_distribution<double> d(1.0/m_conf.trigger_interval_ticks);
      return (int)(0.5+d(gen));
  }
}

void
RandomTriggerCandidateMaker::send_trigger_candidates()
{
  std::mt19937 gen(m_run_number);
  // Wait for there to be a valid timestamp estimate before we start
  if (m_timestamp_estimator->wait_for_valid_timestamp(m_running_flag) == TimestampEstimatorBase::kInterrupted) {
    return;
  }

  dfmessages::timestamp_t initial_timestamp = m_timestamp_estimator->get_timestamp_estimate();
  dfmessages::timestamp_t next_trigger_timestamp = initial_timestamp + get_interval(gen);
  TLOG_DEBUG(1) << get_name() << " initial timestamp estimate is " << initial_timestamp 
                << ", next_trigger_timestamp is " << next_trigger_timestamp;

  while (m_running_flag.load()) {
    if (m_timestamp_estimator->wait_for_timestamp(next_trigger_timestamp, m_running_flag) ==
        TimestampEstimatorBase::kInterrupted) {
      break;
    }

    triggeralgs::TriggerCandidate candidate = create_candidate(next_trigger_timestamp);

    TLOG_DEBUG(1) << get_name() << " at timestamp " << m_timestamp_estimator->get_timestamp_estimate()
                  << ", pushing a candidate with timestamp " << candidate.time_candidate;
    m_trigger_candidate_sink->push(candidate, std::chrono::milliseconds(10));

    next_trigger_timestamp += get_interval(gen);
  }
}

} // namespace trigger
} // namespace dunedaq

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::RandomTriggerCandidateMaker)

// Local Variables:
// c-basic-offset: 2
// End:
