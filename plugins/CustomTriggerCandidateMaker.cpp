/**
 * @file CustomTriggerCandidateMaker.cpp CustomTriggerCandidateMaker class
 * implementation
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "CustomTriggerCandidateMaker.hpp"

#include "trigger/Issues.hpp"

#include "appfwk/DAQModuleHelper.hpp"
#include "appfwk/app/Nljs.hpp"
#include "daqdataformats/ComponentRequest.hpp"
#include "detdataformats/trigger/Types.hpp"
#include "dfmessages/TimeSync.hpp"
#include "dfmessages/TriggerDecision.hpp"
#include "dfmessages/TriggerInhibit.hpp"
#include "dfmessages/Types.hpp"
#include "iomanager/IOManager.hpp"
#include "logging/Logging.hpp"
#include "timinglibs/TimestampEstimator.hpp"
#include "timinglibs/TimestampEstimatorSystem.hpp"
#include "triggeralgs/TriggerCandidate.hpp"

#include <algorithm>
#include <cassert>
#include <pthread.h>
#include <random>
#include <string>
#include <vector>

bool
sortbysec(const std::pair<int, dunedaq::dfmessages::timestamp_t> &a, const std::pair<int, dunedaq::dfmessages::timestamp_t> &b)
{
  return (a.second < b.second);
}

namespace dunedaq {
namespace trigger {

CustomTriggerCandidateMaker::CustomTriggerCandidateMaker(const std::string& name)
  : DAQModule(name)
  , m_trigger_candidate_sink(nullptr)
  , m_run_number(0)
{
  register_command("conf", &CustomTriggerCandidateMaker::do_configure);
  register_command("start", &CustomTriggerCandidateMaker::do_start);
  register_command("stop", &CustomTriggerCandidateMaker::do_stop);
  register_command("scrap", &CustomTriggerCandidateMaker::do_scrap);
}

void
CustomTriggerCandidateMaker::init(const nlohmann::json& obj)
{
  m_trigger_candidate_sink = get_iom_sender<triggeralgs::TriggerCandidate>(appfwk::connection_uid(obj, "trigger_candidate_sink"));
}

void
CustomTriggerCandidateMaker::get_info(opmonlib::InfoCollector& ci, int /*level*/)
{
  customtriggercandidatemakerinfo::Info i;

  i.tc_sent_count = m_tc_sent_count.load();

  ci.add(i);
}

void
CustomTriggerCandidateMaker::do_configure(const nlohmann::json& obj)
{
  m_conf = obj.get<customtriggercandidatemaker::Conf>();
  for (int i=0; i<static_cast<int>(m_conf.trigger_types.size()); i++) {
    std::pair<int, long int> temp_pair{m_conf.trigger_types[i], m_conf.trigger_intervals[i]}; 
    m_tc_settings.push_back(temp_pair);
  }
  print_config();
}

void
CustomTriggerCandidateMaker::do_start(const nlohmann::json& obj)
{
  m_run_number = obj.value<dunedaq::daqdataformats::run_number_t>("run", 0);

  m_running_flag.store(true);

  switch (m_conf.timestamp_method) {
    case customtriggercandidatemaker::timestamp_estimation::kTimeSync:
      TLOG_DEBUG(0) << "Creating TimestampEstimator";
      m_timestamp_estimator.reset(new timinglibs::TimestampEstimator(m_conf.clock_frequency_hz));
      break;
    case customtriggercandidatemaker::timestamp_estimation::kSystemClock:
      TLOG_DEBUG(0) << "Creating TimestampEstimatorSystem";
      m_timestamp_estimator.reset(new timinglibs::TimestampEstimatorSystem(m_conf.clock_frequency_hz));
      break;
  }
  
  m_send_trigger_candidates_thread = std::thread(&CustomTriggerCandidateMaker::send_trigger_candidates, this);
  pthread_setname_np(m_send_trigger_candidates_thread.native_handle(), "custom-tc-maker");

}

void
CustomTriggerCandidateMaker::do_stop(const nlohmann::json& /*obj*/)
{
  m_running_flag.store(false);

  m_send_trigger_candidates_thread.join();

  m_timestamp_estimator.reset(nullptr); // Calls TimestampEstimator dtor
}

void
CustomTriggerCandidateMaker::do_scrap(const nlohmann::json& /*obj*/)
{
  m_configured_flag.store(false);
}

triggeralgs::TriggerCandidate
CustomTriggerCandidateMaker::create_candidate(dfmessages::timestamp_t timestamp, int tc_type)
{
  triggeralgs::TriggerCandidate candidate;
  candidate.time_start = timestamp;
  candidate.time_end = timestamp;
  candidate.time_candidate = timestamp;
  candidate.detid = { 0 };
  candidate.type = static_cast<dunedaq::detdataformats::trigger::TriggerCandidateData::Type>(tc_type);
  candidate.algorithm = triggeralgs::TriggerCandidate::Algorithm::kUnknown;

  return candidate;
}

void
CustomTriggerCandidateMaker::send_trigger_candidates()
{
  // OpMon.
  m_tc_sent_count.store(0);

  // Wait for there to be a valid timestamp estimate before we start
  TLOG_DEBUG(3) << "CTCM: waiting for valid timestamp ...";
  if ((m_timestamp_estimator->wait_for_valid_timestamp(m_running_flag)) == 
    timinglibs::TimestampEstimatorBase::kInterrupted) {
    return;
  }

  dfmessages::timestamp_t initial_timestamp = m_timestamp_estimator->get_timestamp_estimate();

  std::vector<std::pair<int, dfmessages::timestamp_t>> tc_timestamps = get_initial_timestamps(initial_timestamp);
  print_timestamps_vector(tc_timestamps);
  std::vector<std::pair<int, dfmessages::timestamp_t>> tc_timestamps_old = tc_timestamps;

  dfmessages::timestamp_t next_trigger_timestamp = tc_timestamps.front().second;
  TLOG_DEBUG(1) << get_name() << " initial timestamp estimate is " << initial_timestamp
                << ", next_trigger_timestamp is " << next_trigger_timestamp;

  while (m_running_flag.load()) {
    TLOG_DEBUG(3) << "CTCM: waiting for next timestamp ...";
    if ((m_timestamp_estimator->wait_for_timestamp(next_trigger_timestamp, m_running_flag)) ==
        timinglibs::TimestampEstimatorBase::kInterrupted) {
      break;
    }

    triggeralgs::TriggerCandidate candidate = create_candidate(next_trigger_timestamp, tc_timestamps.front().first);

    TLOG_DEBUG(1) << get_name() << " at timestamp " << m_timestamp_estimator->get_timestamp_estimate()
                  << ", pushing a candidate with timestamp " << candidate.time_candidate;
    m_trigger_candidate_sink->send(std::move(candidate), std::chrono::milliseconds(10));
    m_tc_sent_count++;

    tc_timestamps.erase(tc_timestamps.begin());
    next_trigger_timestamp = tc_timestamps.front().second;
    if (tc_timestamps.size() < 1){
      TLOG_DEBUG(3) << "Need next timestamps!";
      tc_timestamps.clear();
      tc_timestamps = get_next_timestamps(tc_timestamps_old);
    } else if (tc_timestamps.size() < 1) {
      // make error here
    }
  }
}

std::vector<std::pair<int, dfmessages::timestamp_t>>
CustomTriggerCandidateMaker::get_initial_timestamps(dfmessages::timestamp_t initial_timestamp)
{
  TLOG_DEBUG(3) << "GIT, init ts: " << initial_timestamp;
  std::vector<std::pair<int, dfmessages::timestamp_t>> initial_timestamps;
  for (int i=0; i<static_cast<int>(m_conf.trigger_types.size()); i++) { 
    // Round up to the next multiple of trigger_interval_ticks
    // also adding little offset so we don't start all at once
    dfmessages::timestamp_t next_trigger_timestamp = ((initial_timestamp+i*5000) / m_tc_settings[i].second + 1) * m_tc_settings[i].second;
    std::pair<int, dfmessages::timestamp_t> initial_pair{ m_tc_settings[i].first, next_trigger_timestamp };
    initial_timestamps.push_back( initial_pair );
    TLOG_DEBUG(3) << "GIT TS pair, type: " << m_tc_settings[i].first << ", inter: " << m_tc_settings[i].second 
    << ", ts: " << next_trigger_timestamp;
  }
  sort(initial_timestamps.begin(), initial_timestamps.end(), sortbysec);
  return initial_timestamps;
}

std::vector<std::pair<int, dfmessages::timestamp_t>>
CustomTriggerCandidateMaker::get_next_timestamps(std::vector<std::pair<int, dfmessages::timestamp_t>> last_timestamps)
{
  std::vector<std::pair<int, dfmessages::timestamp_t>> next_timestamps;
  for(auto it = m_tc_settings.begin(); it != m_tc_settings.end(); it++){
    dfmessages::timestamp_t last_ts_of_type = get_last_ts_of_type(it->first, last_timestamps);
    std::vector<std::pair<int, dfmessages::timestamp_t>> next_ts_of_type = get_next_ts_of_type(it->first, it->second, last_ts_of_type);
    next_timestamps.insert(next_timestamps.end(), next_ts_of_type.begin(), next_ts_of_type.end());
  }
  sort(next_timestamps.begin(), next_timestamps.end(), sortbysec);
  TLOG_DEBUG(3) << "New next ts (all):";
  print_timestamps_vector(next_timestamps);
  next_timestamps.resize(10);
  TLOG_DEBUG(3) << "New next ts (10):";
  print_timestamps_vector(next_timestamps);
  return next_timestamps;
}

dfmessages::timestamp_t
CustomTriggerCandidateMaker::get_last_ts_of_type(int tc_type, std::vector<std::pair<int, dfmessages::timestamp_t>> last_timestamps)
{
  for(auto r_it = last_timestamps.rbegin(); r_it != last_timestamps.rend(); r_it++){
    if (r_it->first == tc_type){
      TLOG_DEBUG(3) << "Last TS type: " << tc_type << " is: " << r_it->second;
      return r_it->second;
    } else {
      // make error here
    }
  }
}

std::vector<std::pair<int, dfmessages::timestamp_t>> 
CustomTriggerCandidateMaker::get_next_ts_of_type(int tc_type, long int tc_interval, dfmessages::timestamp_t last_ts_of_type)
{
  std::vector<std::pair<int, dfmessages::timestamp_t>> next_ts_of_type;
  for(int i=0; i<10; i++){
    dfmessages::timestamp_t next_trigger_timestamp = ((last_ts_of_type) / tc_interval + 1) * tc_interval;
    std::pair<int, dfmessages::timestamp_t> next_pair{ tc_type, next_trigger_timestamp };
    next_ts_of_type.push_back( next_pair );
    last_ts_of_type = next_trigger_timestamp;
  }
  TLOG_DEBUG(3) << "Next 10 ts for type: " << tc_type << ":";
  print_timestamps_vector(next_ts_of_type);
  return next_ts_of_type;
}

void
CustomTriggerCandidateMaker::print_config()
{
  TLOG_DEBUG(3) << "CTCM Trigger types and intervals to use: ";
  for (auto it = m_tc_settings.begin(); it != m_tc_settings.end(); it++) {
    TLOG_DEBUG(3) << "TC type: " << it->first << ", interval: " << it->second;
  }
  return;
}

void
CustomTriggerCandidateMaker::print_timestamps_vector(std::vector<std::pair<int, dfmessages::timestamp_t>> timestamps)
{
  TLOG_DEBUG(3) << "Next timestamps:";
  for (auto it = timestamps.begin(); it != timestamps.end(); it++) {
    TLOG_DEBUG(3) << "TC type: " << it->first << ", timestamp: " << it->second;
  }
  return;
}

} // namespace trigger
} // namespace dunedaq

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::CustomTriggerCandidateMaker)

// Local Variables:
// c-basic-offset: 2
// End:
