/**
 * @file CustomTCMaker.cpp CustomTCMaker class
 * implementation
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "CustomTCMaker.hpp"

#include "trigger/Issues.hpp"

#include "appfwk/app/Nljs.hpp"
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
#include "rcif/cmd/Nljs.hpp"

#include <algorithm>
#include <cassert>
#include <pthread.h>
#include <random>
#include <string>
#include <vector>

// This is used to sort vector of pairs based on the value (ie second element) of the pair
bool
sortbysec(const std::pair<int, dunedaq::dfmessages::timestamp_t>& a,
          const std::pair<int, dunedaq::dfmessages::timestamp_t>& b)
{
  return (a.second < b.second);
}

namespace dunedaq {

namespace trigger {

CustomTCMaker::CustomTCMaker(const std::string& name)
  : DAQModule(name)
  , m_time_sync_source(nullptr)
  , m_trigger_candidate_sink(nullptr)
{
  register_command("conf", &CustomTCMaker::do_configure);
  register_command("start", &CustomTCMaker::do_start);
  register_command("stop", &CustomTCMaker::do_stop);
  register_command("scrap", &CustomTCMaker::do_scrap);
}

void
CustomTCMaker::init(std::shared_ptr<appfwk::ModuleConfiguration> mcfg)
{

  auto mtrg = mcfg->module<appmodel::CustomTCMaker>(get_name());
  try {
    // Get the outputs
    for(auto con: mtrg->get_outputs()){
        m_trigger_candidate_sink = get_iom_sender<triggeralgs::TriggerCandidate>(con->UID());
    }
  } catch (const ers::Issue& excpt) {
    throw dunedaq::trigger::InvalidQueueFatalError(ERS_HERE, get_name(), "input/output", excpt);
  }

  m_time_sync_source = get_iom_receiver<dfmessages::TimeSync>(".*");

  m_conf = mtrg->get_configuration();

  // Parsing configuration object into map
  for (int i = 0; i < static_cast<int>(m_conf->get_trigger_types().size()); i++) {
    std::pair<int, long int> temp_pair{ m_conf->get_trigger_types()[i], m_conf->get_trigger_intervals()[i] };
    m_tc_settings.push_back(temp_pair);
  }

  print_config();

  // This parameter controls how many new timestamps are calculated when needed
  // Currently precalculates events for the next 60 seconds
  m_sorting_size_limit = 60 * m_conf->get_clock_frequency_hz();

  m_latency_monitoring.store( m_conf->get_latency_monitoring() );
}

//void
//CustomTCMaker::init(const nlohmann::json& obj)
//{
//  // TODO: delete, reimplement as oks
//  //m_time_sync_source = get_iom_receiver<dfmessages::TimeSync>(".*");
//  //m_trigger_candidate_sink =
//  //  get_iom_sender<triggeralgs::TriggerCandidate>(appfwk::connection_uid(obj, "trigger_candidate_sink"));
//}

void 
CustomTCMaker::generate_opmon_data()
{
  opmon::CustomTCMakerInfo info;

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
CustomTCMaker::do_configure(const nlohmann::json& /*obj*/)
{
  //m_conf = obj.get<customtriggercandidatemaker::Conf>();

  //// Parsing configuration object into map
  //for (int i = 0; i < static_cast<int>(m_conf->trigger_types.size()); i++) {
  //  std::pair<int, long int> temp_pair{ m_conf->trigger_types[i], m_conf->trigger_intervals[i] };
  //  m_tc_settings.push_back(temp_pair);
  //}

  //print_config();

  //// This parameter controls how many new timestamps are calculated when needed
  //// Currently precalculates events for the next 60 seconds
  //m_sorting_size_limit = 60 * m_conf0>clock_frequency_hz;
}

void
CustomTCMaker::do_start(const nlohmann::json& obj)
{
  m_running_flag.store(true);

  // OpMon.
  m_tc_made_count.store(0);
  m_tc_sent_count.store(0);
  m_tc_failed_sent_count.store(0);

  auto start_params = obj.get<rcif::cmd::StartParams>();

  std::string timestamp_method = m_conf->get_timestamp_method();
  if(timestamp_method == "kTimeSync") {
    TLOG_DEBUG(0) << "Creating TimestampEstimator";
    m_timestamp_estimator.reset(new utilities::TimestampEstimator(start_params.run, m_conf->get_clock_frequency_hz()));
    m_time_sync_source->add_callback(std::bind(&utilities::TimestampEstimator::timesync_callback<dfmessages::TimeSync>,
          reinterpret_cast<utilities::TimestampEstimator*>(m_timestamp_estimator.get()),
          std::placeholders::_1));
  }
  else if(timestamp_method == "kSystemClock"){
    TLOG_DEBUG(0) << "Creating TimestampEstimatorSystem";
    m_timestamp_estimator.reset(new utilities::TimestampEstimatorSystem(m_conf->get_clock_frequency_hz()));
  }
  else{
    // do some error message here
  }

  //switch (m_conf->get_timestamp_method()) {
  //  case "kTimeSync":
  //    TLOG_DEBUG(0) << "Creating TimestampEstimator";
  //    m_timestamp_estimator.reset(new utilities::TimestampEstimator(start_params.run, m_conf->get_clock_frequency_hz()));
  //    m_time_sync_source->add_callback(std::bind(&utilities::TimestampEstimator::timesync_callback<dfmessages::TimeSync>,
  //                                               reinterpret_cast<utilities::TimestampEstimator*>(m_timestamp_estimator.get()),
  //                                               std::placeholders::_1));
  //    break;
  //  case "kSystemClock":
  //    TLOG_DEBUG(0) << "Creating TimestampEstimatorSystem";
  //    m_timestamp_estimator.reset(new utilities::TimestampEstimatorSystem(m_conf->get_clock_frequency_hz()));
  //    break;
  //}

  m_send_trigger_candidates_thread = std::thread(&CustomTCMaker::send_trigger_candidates, this);
  pthread_setname_np(m_send_trigger_candidates_thread.native_handle(), "custom-tc-maker");
}

void
CustomTCMaker::do_stop(const nlohmann::json& /*obj*/)
{
  m_running_flag.store(false);

  m_send_trigger_candidates_thread.join();

  m_time_sync_source->remove_callback();
  m_timestamp_estimator.reset(nullptr); // Calls TimestampEstimator dtor

  print_opmon_stats();
  // Prints final counts of each used TC type
  print_final_tc_counts(m_tc_sent_count_type);
}

void
CustomTCMaker::do_scrap(const nlohmann::json& /*obj*/)
{
  m_configured_flag.store(false);
}

// This creates TCs of given type at the provided timestamp
// The algo used is kCustom
// Other parameters are default
triggeralgs::TriggerCandidate
CustomTCMaker::create_candidate(dfmessages::timestamp_t timestamp, int tc_type)
{
  triggeralgs::TriggerCandidate candidate;
  candidate.time_start = (timestamp - 1000);
  candidate.time_end = (timestamp + 1000);
  candidate.time_candidate = timestamp;
  candidate.detid = { 0 };
  candidate.type = static_cast<dunedaq::trgdataformats::TriggerCandidateData::Type>(tc_type);
  candidate.algorithm = triggeralgs::TriggerCandidate::Algorithm::kCustom;

  return candidate;
}

void
CustomTCMaker::send_trigger_candidates()
{
  // OpMon.
  m_tc_sent_count.store(0);

  // Wait for there to be a valid timestamp estimate before we start
  TLOG_DEBUG(3) << "CTCM: waiting for valid timestamp ...";
  if ((m_timestamp_estimator->wait_for_valid_timestamp(m_running_flag)) ==
      utilities::TimestampEstimatorBase::kInterrupted) {
    return;
  }

  m_initial_timestamp = m_timestamp_estimator->get_timestamp_estimate();

  m_tc_timestamps = get_initial_timestamps(m_initial_timestamp);
  //print_timestamps_vector(m_tc_timestamps);

  m_next_trigger_timestamp = m_tc_timestamps.front().second;
  m_next_trigger_type = m_tc_timestamps.front().first;

  TLOG_DEBUG(1) << get_name() << " initial timestamp estimate is " << m_initial_timestamp
                << ", next_trigger_timestamp is " << m_next_trigger_timestamp;

  while (m_running_flag.load()) {
    TLOG_DEBUG(3) << "CTCM: waiting for next timestamp ...";
    if ((m_timestamp_estimator->wait_for_timestamp(m_next_trigger_timestamp, m_running_flag)) ==
        utilities::TimestampEstimatorBase::kInterrupted) {
    }

    triggeralgs::TriggerCandidate candidate = create_candidate(m_next_trigger_timestamp, m_tc_timestamps.front().first);
    m_tc_made_count++;

    TLOG_DEBUG(1) << get_name() << " at timestamp " << m_timestamp_estimator->get_timestamp_estimate()
                  << ", pushing a candidate with timestamp " << candidate.time_candidate;

    if (m_latency_monitoring.load()) m_latency_instance.update_latency_out( candidate.time_candidate );
    try {
      m_trigger_candidate_sink->send(std::move(candidate), std::chrono::milliseconds(10));
      m_tc_sent_count++;
      m_tc_sent_count_type[m_tc_timestamps.front().first] += 1;
    } catch (const ers::Issue& e) {
      ers::error(e);
      m_tc_failed_sent_count++;
    }

    // Need to record last used TS for calculation of next ones
    m_last_timestamps_of_type[m_tc_timestamps.front().first] = m_tc_timestamps.front().second;

    // Progress the vector of next TCs
    m_tc_timestamps.erase(m_tc_timestamps.begin());

    // Generate new timestamps for each source
    if (m_tc_timestamps.size() < 1) {
      TLOG_DEBUG(3) << "Need next timestamps!";
      m_tc_timestamps.clear();
      m_tc_timestamps = get_next_timestamps(m_last_timestamps_of_type);
    }

    // Set new TS and type
    m_next_trigger_timestamp = m_tc_timestamps.front().second;
    m_next_trigger_type = m_tc_timestamps.front().first;

    // Should not happen
    if (m_tc_timestamps.size() == 0) {
      ers::error(TCTimestampsSizeError(ERS_HERE, get_name(), m_tc_timestamps.size()));
    }
  }
}

// This function generates initial TS for each TC type
// Small offset is applied so we don't start with all at once
// Also rounds up to the next multiple of the interval ticks
// The returning vector is sorted by TS
std::vector<std::pair<int, dfmessages::timestamp_t>>
CustomTCMaker::get_initial_timestamps(dfmessages::timestamp_t initial_timestamp)
{
  TLOG_DEBUG(3) << "GIT, init ts: " << initial_timestamp;
  std::vector<std::pair<int, dfmessages::timestamp_t>> initial_timestamps;
  for (int i = 0; i < static_cast<int>(m_conf->get_trigger_types().size()); i++) {
    dfmessages::timestamp_t next_trigger_timestamp =
      ((initial_timestamp + i * 5000) / m_tc_settings[i].second + 1) * m_tc_settings[i].second;
    std::pair<int, dfmessages::timestamp_t> initial_pair{ m_tc_settings[i].first, next_trigger_timestamp };
    initial_timestamps.push_back(initial_pair);
    m_last_timestamps_of_type[i] = next_trigger_timestamp;
    TLOG_DEBUG(3) << "GIT TS pair, type: " << m_tc_settings[i].first << ", inter: " << m_tc_settings[i].second
                  << ", ts: " << next_trigger_timestamp;
  }
  std::sort(initial_timestamps.begin(), initial_timestamps.end(), sortbysec);
  return initial_timestamps;
}

// This function generates next {m_sorting_size_limit} timestamps for each TC type
// Then merges to final vector and sorts it by value
std::vector<std::pair<int, dfmessages::timestamp_t>>
CustomTCMaker::get_next_timestamps(std::map<int, dfmessages::timestamp_t> last_timestamps)
{
  std::vector<std::pair<int, dfmessages::timestamp_t>> next_timestamps;
  for (auto it = m_tc_settings.begin(); it != m_tc_settings.end(); it++) {
    std::vector<std::pair<int, dfmessages::timestamp_t>> next_ts_of_type =
      get_next_ts_of_type(it->first, it->second, last_timestamps[it->first]);
    next_timestamps.insert(next_timestamps.end(), next_ts_of_type.begin(), next_ts_of_type.end());
  }
  sort(
    next_timestamps.begin(), next_timestamps.end(), sortbysec);
  //TLOG_DEBUG(3) << "New next ts (all):";
  //print_timestamps_vector(next_timestamps);
  return next_timestamps;
}

// This function calculates next {m_sorting_size_limit} TS for a given TC type
// Uses the configurable interval and the last TS of this type as base
std::vector<std::pair<int, dfmessages::timestamp_t>>
CustomTCMaker::get_next_ts_of_type(int tc_type,
                                                 long int tc_interval,
                                                 dfmessages::timestamp_t last_ts_of_type)
{
  std::vector<std::pair<int, dfmessages::timestamp_t>> next_ts_of_type;
  dfmessages::timestamp_t ts_limit = last_ts_of_type + m_sorting_size_limit;
  dfmessages::timestamp_t loop_ts = last_ts_of_type;
  while (loop_ts < ts_limit) {
    dfmessages::timestamp_t next_trigger_timestamp = ((loop_ts) / tc_interval + 1) * tc_interval;
    std::pair<int, dfmessages::timestamp_t> next_pair{ tc_type, next_trigger_timestamp };
    next_ts_of_type.push_back(next_pair);
    loop_ts = next_trigger_timestamp;
  }
  //TLOG_DEBUG(3) << "Next calculated ts for type: " << tc_type << ":";
  //TLOG_DEBUG(3) << print_timestamps_vector(next_ts_of_type);
  return next_ts_of_type;
}

void
CustomTCMaker::print_config()
{
  TLOG_DEBUG(3) << "CTCM Trigger types and intervals to use: ";
  for (auto it = m_tc_settings.begin(); it != m_tc_settings.end(); it++) {
    TLOG_DEBUG(3) << "TC type: " << it->first << ", interval: " << it->second;
  }
  return;
}

void
CustomTCMaker::print_timestamps_vector(std::vector<std::pair<int, dfmessages::timestamp_t>> timestamps)
{
  TLOG_DEBUG(3) << "Next timestamps:";
  for (auto it = timestamps.begin(); it != timestamps.end(); it++) {
    TLOG_DEBUG(3) << "TC type: " << it->first << ", timestamp: " << it->second;
  }
  return;
}

void
CustomTCMaker::print_opmon_stats()
{
  TLOG() << "CustomTCMaker opmon counters summary:";
  TLOG() << "------------------------------";
  TLOG() << "Made TCs: \t\t" << m_tc_made_count;
  TLOG() << "Sent TCs: \t\t" << m_tc_sent_count;
  TLOG() << "Failed to send TCs: \t" << m_tc_failed_sent_count;
  TLOG();
}

void
CustomTCMaker::print_final_tc_counts(std::map<int, int> counts)
{
  TLOG_DEBUG(3) << "CTCM final counts:";
  for (auto it = m_tc_settings.begin(); it != m_tc_settings.end(); it++) {
    TLOG_DEBUG(3) << "TC type: " << it->first << ", interval: " << it->second << ", count: " << counts[it->first];
  }
  return;
}

} // namespace trigger
} // namespace dunedaq

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::CustomTCMaker)

// Local Variables:
// c-basic-offset: 2
// End:
