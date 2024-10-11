/**
 * @file ModuleLevelTrigger.cpp ModuleLevelTrigger class
 * implementation
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "ModuleLevelTrigger.hpp"

#include "trigger/Issues.hpp"
#include "trigger/LivetimeCounter.hpp"
#include "trigger/Logging.hpp"
#include "trigger/moduleleveltrigger/Nljs.hpp"

#include "appfwk/DAQModuleHelper.hpp"
#include "appfwk/app/Nljs.hpp"
#include "daqdataformats/ComponentRequest.hpp"
#include "dfmessages/TriggerDecision.hpp"
#include "dfmessages/TriggerInhibit.hpp"
#include "dfmessages/Types.hpp"
#include "iomanager/IOManager.hpp"
#include "logging/Logging.hpp"
#include "trgdataformats/Types.hpp"

#include <algorithm>
#include <cassert>
#include <pthread.h>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

using dunedaq::trigger::logging::TLVL_DEBUG_ALL;
using dunedaq::trigger::logging::TLVL_DEBUG_HIGH;
using dunedaq::trigger::logging::TLVL_DEBUG_INFO;
using dunedaq::trigger::logging::TLVL_DEBUG_LOW;
using dunedaq::trigger::logging::TLVL_DEBUG_MEDIUM;
using dunedaq::trigger::logging::TLVL_IMPORTANT;

using namespace std::chrono;

namespace dunedaq {
namespace trigger {

ModuleLevelTrigger::ModuleLevelTrigger(const std::string& name)
  : DAQModule(name)
  , m_last_trigger_number(0)
  , m_run_number(0)
{
  // clang-format off
  register_command("conf",   &ModuleLevelTrigger::do_configure);
  register_command("start",  &ModuleLevelTrigger::do_start);
  register_command("stop",   &ModuleLevelTrigger::do_stop);
  register_command("disable_triggers",  &ModuleLevelTrigger::do_pause);
  register_command("enable_triggers", &ModuleLevelTrigger::do_resume);
  register_command("scrap",  &ModuleLevelTrigger::do_scrap);
  // clang-format on
}

void
ModuleLevelTrigger::init(const nlohmann::json& iniobj)
{

  auto ci = appfwk::connection_index(iniobj, { "trigger_candidate_input", "dfo_inhibit_input", "td_output" });

  m_candidate_input = get_iom_receiver<triggeralgs::TriggerCandidate>(ci["trigger_candidate_input"]);

  m_inhibit_input = get_iom_receiver<dfmessages::TriggerInhibit>(ci["dfo_inhibit_input"]);

  m_td_output_connection = ci["td_output"];
}

void
ModuleLevelTrigger::get_info(opmonlib::InfoCollector& ci, int /*level*/)
{
  moduleleveltriggerinfo::Info i;

  i.tc_received_count = m_tc_received_count.load();
  i.tc_ignored_count = m_tc_ignored_count.load();
  i.td_sent_count = m_td_sent_count.load();
  i.new_td_sent_count = m_new_td_sent_count.exchange(0);
  i.td_sent_tc_count = m_td_sent_tc_count.load();
  i.td_inhibited_count = m_td_inhibited_count.load();
  i.new_td_inhibited_count = m_new_td_inhibited_count.exchange(0);
  i.td_inhibited_tc_count = m_td_inhibited_tc_count.load();
  i.td_paused_count = m_td_paused_count.load();
  i.td_paused_tc_count = m_td_paused_tc_count.load();
  i.td_dropped_count = m_td_dropped_count.load();
  i.td_dropped_tc_count = m_td_dropped_tc_count.load();
  i.td_cleared_count = m_td_cleared_count.load();
  i.td_cleared_tc_count = m_td_cleared_tc_count.load();
  i.td_not_triggered_count = m_td_not_triggered_count.load();
  i.td_not_triggered_tc_count = m_td_not_triggered_tc_count.load();
  i.td_total_count = m_td_total_count.load();
  i.new_td_total_count = m_new_td_total_count.exchange(0);

  // latency
  i.tc_data_vs_system_ms = m_tc_data_vs_system.load();
  i.td_made_vs_ro_window_ms = m_td_made_vs_ro.load();
  i.td_send_vs_ro_start_ms = m_td_send_vs_ro_start.load();
  i.td_send_vs_ro_end_ms = m_td_send_vs_ro_end.load();

  if (m_livetime_counter.get() != nullptr) {
    i.lc_kLive = m_livetime_counter->get_time(LivetimeCounter::State::kLive);
    i.lc_kPaused = m_livetime_counter->get_time(LivetimeCounter::State::kPaused);
    i.lc_kDead = m_livetime_counter->get_time(LivetimeCounter::State::kDead);
  }

  ci.add(i);
}

void
ModuleLevelTrigger::do_configure(const nlohmann::json& confobj)
{
  auto params = confobj.get<moduleleveltrigger::ConfParams>();

  // Get SourceID to geoid map
  dunedaq::hdf5libs::hdf5rawdatafile::SrcIDGeoIDMap src_to_geo_map = params.srcid_geoid_map;
  for (auto const& entry : src_to_geo_map) {
    daqdataformats::SourceID source_id(daqdataformats::SourceID::Subsystem::kDetectorReadout, entry.src_id);
    m_srcid_geoid_map[source_id] = entry.geo_id;
  }

  // Get custom subdetector readout map
  for (auto const& subdet : params.detector_readout_map) {
    dunedaq::detdataformats::DetID::Subdetector detid;
    detid = dunedaq::detdataformats::DetID::string_to_subdetector(subdet.subdetector);
    if (detid == detdataformats::DetID::Subdetector::kUnknown) {
      throw MLTConfigurationProblem(ERS_HERE, get_name(),
          "Unknown Subdetector supplied to MLT subdetector-readout window map");
    }

    if (m_subdetector_readout_window_map.count(detid)) {
      throw MLTConfigurationProblem(ERS_HERE, get_name(),
          "Supplied more than one of the same Subdetector name to MLT subdetector-readout window map");
    }

    m_subdetector_readout_window_map[detid] = std::make_pair(subdet.time_before, subdet.time_after);

    TLOG() << "[MLT] Custom readout map for subdetector: " << subdet.subdetector
           << " time_start: " << subdet.time_before << " time_after: " << subdet.time_after;
  }

  m_mandatory_links.clear();
  for (auto const& link : params.mandatory_links) {
    m_mandatory_links.push_back(
      dfmessages::SourceID{ daqdataformats::SourceID::string_to_subsystem(link.subsystem), link.element });
  }
  m_group_links.clear();
  m_group_links_data = params.groups_links;
  parse_group_links(m_group_links_data);
  print_group_links();
  m_total_group_links = m_group_links.size();
  TLOG_DEBUG(TLVL_DEBUG_INFO) << "[MLT] Total group links: " << m_total_group_links;

  // m_trigger_decision_connection = params.dfo_connection;
  // m_inhibit_connection = params.dfo_busy_connection;

  m_configured_flag.store(true);

  m_tc_merging = params.merge_overlapping_tcs;
  m_ignore_tc_pileup = params.ignore_overlapping_tcs;
  m_buffer_timeout = params.buffer_timeout;
  m_send_timed_out_tds = (m_ignore_tc_pileup) ? false : params.td_out_of_timeout;
  m_td_readout_limit = params.td_readout_limit;
  m_ignored_tc_types = params.ignore_tc;
  m_ignoring_tc_types = (m_ignored_tc_types.size() > 0) ? true : false;
  m_use_readout_map = params.use_readout_map;
  m_use_roi_readout = params.use_roi_readout;
  m_use_bitwords = params.use_bitwords;
  TLOG_DEBUG(TLVL_DEBUG_INFO) << "[MLT] Allow merging: " << m_tc_merging;
  TLOG_DEBUG(TLVL_DEBUG_INFO) << "[MLT] Ignore pileup: " << m_ignore_tc_pileup;
  TLOG_DEBUG(TLVL_DEBUG_INFO) << "[MLT] Buffer timeout: " << m_buffer_timeout;
  TLOG_DEBUG(TLVL_DEBUG_INFO) << "[MLT] Should send timed out TDs: " << m_send_timed_out_tds;
  TLOG_DEBUG(TLVL_DEBUG_INFO) << "[MLT] TD readout limit: " << m_td_readout_limit;
  TLOG_DEBUG(TLVL_DEBUG_INFO) << "[MLT] Use ROI readout?: " << m_use_roi_readout;

  // Latency
  m_use_latency_monit = params.enable_latency_monit;
  m_use_latency_offset = params.use_latency_offset;
  TLOG_DEBUG(TLVL_DEBUG_INFO) << "[MLT] Use latency monitoring?: " << m_use_latency_monit;
  TLOG_DEBUG(TLVL_DEBUG_INFO) << "[MLT] Use latency offset?: " << m_use_latency_offset;

  // ROI map
  if (m_use_roi_readout) {
    m_roi_conf_data = params.roi_conf;
    parse_roi_conf(m_roi_conf_data);
    print_roi_conf(m_roi_conf);
  }

  // Custom readout map
  TLOG_DEBUG(TLVL_DEBUG_INFO) << "[MLT] Use readout map: " << m_use_readout_map;
  if (m_use_readout_map) {
    m_readout_window_map_data = params.td_readout_map;
    parse_readout_map(m_readout_window_map_data);
    print_readout_map(m_readout_window_map);
  }

  // Ignoring TC types
  TLOG_DEBUG(TLVL_DEBUG_INFO) << "[MLT] Ignoring TC types: " << m_ignoring_tc_types;
  if (m_ignoring_tc_types) {
    TLOG_DEBUG(TLVL_DEBUG_INFO) << "[MLT] TC types to ignore: ";
    for (std::vector<int>::iterator it = m_ignored_tc_types.begin(); it != m_ignored_tc_types.end();) {
      TLOG_DEBUG(TLVL_DEBUG_INFO) << *it;
      ++it;
    }
  }

  // Trigger bitwords
  TLOG_DEBUG(TLVL_DEBUG_INFO) << "[MLT] Use bitwords: " << m_use_bitwords;
  if (m_use_bitwords) {
    m_trigger_bitwords_json = params.trigger_bitwords;
    print_bitword_flags(m_trigger_bitwords_json);
    set_trigger_bitwords();
    print_trigger_bitwords(m_trigger_bitwords);
  }
}

void
ModuleLevelTrigger::do_start(const nlohmann::json& startobj)
{
  m_run_number = startobj.value<dunedaq::daqdataformats::run_number_t>("run", 0);

  m_bitword_check = false;
  m_tc_data_vs_system = 0;
  m_td_made_vs_ro = 0;
  m_td_send_vs_ro_start = 0;
  m_td_send_vs_ro_end = 0;
  m_first_tc = true;
  m_initial_offset = 0;
  m_system_time = 0;
  // to convert 62.5MHz clock ticks to ms: 1/62500000 = 0.000000016 <- seconds per tick; 0.000016 <- ms per tick;
  // 16*1e-6 <- sci notation
  m_clock_ticks_to_ms = 16 * 1e-6;

  m_paused.store(true);
  m_running_flag.store(true);
  m_dfo_is_busy.store(false);

  m_livetime_counter.reset(new LivetimeCounter(LivetimeCounter::State::kPaused));

  m_inhibit_input->add_callback(std::bind(&ModuleLevelTrigger::dfo_busy_callback, this, std::placeholders::_1));

  m_send_trigger_decisions_thread = std::thread(&ModuleLevelTrigger::send_trigger_decisions, this);
  pthread_setname_np(m_send_trigger_decisions_thread.native_handle(), get_name().c_str());

  ers::info(TriggerStartOfRun(ERS_HERE, m_run_number));

  veto_bitset.set(6);
}

void
ModuleLevelTrigger::do_stop(const nlohmann::json& /*stopobj*/)
{
  m_running_flag.store(false);
  m_send_trigger_decisions_thread.join();

  // Drop all TDs in vectors at run stage change. Have to do this
  // after joining m_send_trigger_decisions_thread so we don't
  // concurrently access the vectors
  clear_td_vectors();

  m_lc_deadtime = m_livetime_counter->get_time(LivetimeCounter::State::kDead) +
                  m_livetime_counter->get_time(LivetimeCounter::State::kPaused);
  TLOG_DEBUG(TLVL_IMPORTANT) << "[MLT] LivetimeCounter - total deadtime+paused: " << m_lc_deadtime << std::endl;
  m_livetime_counter.reset(); // Calls LivetimeCounter dtor?

  m_inhibit_input->remove_callback();
  ers::info(TriggerEndOfRun(ERS_HERE, m_run_number));
}

void
ModuleLevelTrigger::do_pause(const nlohmann::json& /*pauseobj*/)
{
  // flush all pending TDs at run stop
  flush_td_vectors();

  // Drop all TDs in vetors at run stage change
  clear_td_vectors();

  m_paused.store(true);
  m_livetime_counter->set_state(LivetimeCounter::State::kPaused);
  TLOG() << "[MLT] ******* Triggers PAUSED! in run " << m_run_number << " *********";
  ers::info(TriggerPaused(ERS_HERE));
  TLOG_DEBUG(TLVL_DEBUG_INFO) << "[MLT] TS End: "
                              << std::chrono::duration_cast<std::chrono::microseconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count();
}

void
ModuleLevelTrigger::do_resume(const nlohmann::json& /*resumeobj*/)
{
  ers::info(TriggerActive(ERS_HERE));
  TLOG() << "[MLT] ******* Triggers RESUMED! in run " << m_run_number << " *********";
  m_livetime_counter->set_state(LivetimeCounter::State::kLive);
  m_paused.store(false);
  TLOG_DEBUG(TLVL_DEBUG_INFO) << "[MLT] TS Start: "
                              << std::chrono::duration_cast<std::chrono::microseconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count();
}

void
ModuleLevelTrigger::do_scrap(const nlohmann::json& /*scrapobj*/)
{
  m_mandatory_links.clear();
  m_group_links.clear();
  m_configured_flag.store(false);
}

dfmessages::TriggerDecision
ModuleLevelTrigger::create_decision(const ModuleLevelTrigger::PendingTD& pending_td)
{
  m_earliest_tc_index = get_earliest_tc_index(pending_td);
  TLOG_DEBUG(TLVL_DEBUG_HIGH) << "[MLT] earliest TC index: " << m_earliest_tc_index;

  if (pending_td.contributing_tcs.size() > 1) {
    TLOG_DEBUG(TLVL_DEBUG_LOW) << "[MLT] TD created from " << pending_td.contributing_tcs.size() << " TCs !";
  }

  dfmessages::TriggerDecision decision;
  decision.trigger_number = m_last_trigger_number + 1;
  decision.run_number = m_run_number;
  decision.trigger_timestamp = pending_td.contributing_tcs[m_earliest_tc_index].time_candidate;
  decision.readout_type = dfmessages::ReadoutType::kLocalized;

  decision.trigger_type = static_cast<dfmessages::trigger_type_t>(m_TD_bitword.to_ulong()); // m_trigger_type;

  TLOG_DEBUG(TLVL_DEBUG_MEDIUM) << "[MLT] TC detid: " << pending_td.contributing_tcs[m_earliest_tc_index].detid
                                << ", TC type: "
                                << static_cast<int>(pending_td.contributing_tcs[m_earliest_tc_index].type)
                                << ", TC cont number: " << pending_td.contributing_tcs.size()
                                << ", DECISION trigger type: " << decision.trigger_type
                                << ", DECISION timestamp: " << decision.trigger_timestamp
                                << ", request window begin: " << pending_td.readout_start
                                << ", request window end: " << pending_td.readout_end;

  std::vector<dfmessages::ComponentRequest> requests =
    create_all_decision_requests(m_mandatory_links, pending_td.readout_start, pending_td.readout_end);
  add_requests_to_decision(decision, requests);

  if (!m_use_roi_readout) {
    for (const auto& [key, value] : m_group_links) {
      std::vector<dfmessages::ComponentRequest> group_requests =
        create_all_decision_requests(value, pending_td.readout_start, pending_td.readout_end);
      add_requests_to_decision(decision, group_requests);
    }
  } else { // using ROI readout
    roi_readout_make_requests(decision);
  }

  return decision;
}

void
ModuleLevelTrigger::send_trigger_decisions()
{

  // We get here at start of run, so reset the trigger number
  m_last_trigger_number = 0;

  // OpMon.
  m_tc_received_count.store(0);
  m_tc_ignored_count.store(0);
  m_td_sent_count.store(0);
  m_td_sent_tc_count.store(0);
  m_td_inhibited_count.store(0);
  m_td_inhibited_tc_count.store(0);
  m_td_paused_count.store(0);
  m_td_paused_tc_count.store(0);
  m_td_dropped_count.store(0);
  m_td_dropped_tc_count.store(0);
  m_td_cleared_count.store(0);
  m_td_cleared_tc_count.store(0);
  m_td_not_triggered_count.store(0);
  m_td_not_triggered_tc_count.store(0);
  m_td_total_count.store(0);
  m_lc_kLive.store(0);
  m_lc_kPaused.store(0);
  m_lc_kDead.store(0);

  // New buffering logic here
  while (m_running_flag) {
    std::optional<triggeralgs::TriggerCandidate> tc = m_candidate_input->try_receive(std::chrono::milliseconds(10));
    if (tc.has_value()) {

      if (m_first_tc) {
        if (m_use_latency_monit && m_use_latency_offset) {
          m_initial_offset = (duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()) -
                             (tc->time_start * m_clock_ticks_to_ms);
        }
        m_first_tc = false;
      }

      if (m_use_latency_monit) {
        // Update OpMon Variable(s)
        m_system_time = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
        m_tc_data_vs_system.store(fabs(m_system_time - tc->time_start * m_clock_ticks_to_ms - m_initial_offset));
      }

      if ((m_use_readout_map) && (m_readout_window_map.count(tc->type))) {
        TLOG_DEBUG(TLVL_DEBUG_HIGH) << "[MLT] Got TC of type " << static_cast<int>(tc->type) << ", timestamp "
                                    << tc->time_candidate << ", start/end " << tc->time_start << "/" << tc->time_end
                                    << ", readout start/end "
                                    << tc->time_candidate - m_readout_window_map[tc->type].first << "/"
                                    << tc->time_candidate + m_readout_window_map[tc->type].second;
      } else {
        TLOG_DEBUG(TLVL_DEBUG_HIGH) << "[MLT] Got TC of type " << static_cast<int>(tc->type) << ", timestamp "
                                    << tc->time_candidate << ", start/end " << tc->time_start << "/" << tc->time_end;
      }
      ++m_tc_received_count;

      // Option to ignore TC types (if given by config)
      if (m_ignoring_tc_types == true) {
        TLOG_DEBUG(TLVL_DEBUG_HIGH) << "[MLT] TC type: " << static_cast<int>(tc->type);
        if (check_trigger_type_ignore(static_cast<int>(tc->type)) == true) {
          TLOG_DEBUG(TLVL_DEBUG_HIGH) << "[MLT] ignoring...";
          m_tc_ignored_count++;

          if (m_tc_merging) {
            // Still need to check for overlap with existing TD, if overlaps, include in the TD, but don't extend
            // readout
            std::lock_guard<std::mutex> lock(m_td_vector_mutex);
            add_tc_ignored(*tc);
          }
          continue;
        }
      }

      std::lock_guard<std::mutex> lock(m_td_vector_mutex);
      add_tc(*tc);
      TLOG_DEBUG(TLVL_DEBUG_ALL) << "[MLT] pending tds size: " << m_pending_tds.size();
    } else {
      // The condition to exit the loop is that we've been stopped and
      // there's nothing left on the input queue
      if (!m_running_flag.load()) {
        break;
      }
    }

    std::lock_guard<std::mutex> lock(m_td_vector_mutex);
    auto ready_tds = get_ready_tds(m_pending_tds);
    TLOG_DEBUG(TLVL_DEBUG_ALL) << "[MLT] ready tds: " << ready_tds.size()
                               << ", updated pending tds: " << m_pending_tds.size()
                               << ", sent tds: " << m_sent_tds.size();

    for (std::vector<PendingTD>::iterator it = ready_tds.begin(); it != ready_tds.end();) {

      // Release ready TD if we're not doing tc overlap merging or ignoring
      if (!m_tc_merging && !m_ignore_tc_pileup) {
        call_tc_decision(*it);
        ++it;
        continue;
      }

      // Release ready TD if it doesn't overlap with one we've already sent
      if (!check_overlap_td(*it)) {
        call_tc_decision(*it);
        ++it;
        continue;
      }

      m_earliest_tc_index = get_earliest_tc_index(*it);
      auto const& earliest_tc = it->contributing_tcs[m_earliest_tc_index];
      ers::warning(TCOutOfTimeout(ERS_HERE,
                                  get_name(),
                                  static_cast<int>(earliest_tc.type),
                                  earliest_tc.time_candidate,
                                  it->readout_start,
                                  it->readout_end));

      // Release TD if timed out, if correct option is on
      if (m_send_timed_out_tds) {
        call_tc_decision(*it);
        ++it;
      }

      ++m_td_dropped_count;
      m_td_dropped_tc_count += it->contributing_tcs.size();
      it = ready_tds.erase(it);
      TLOG_DEBUG(TLVL_DEBUG_MEDIUM) << "[MLT] TD overlapping previous TD, dropping!";
    }

    TLOG_DEBUG(TLVL_DEBUG_ALL) << "[MLT] updated sent tds: " << m_sent_tds.size();
  }

  TLOG() << "[MLT] Run " << m_run_number << ": "
         << "Received " << m_tc_received_count << " TCs. Sent " << m_td_sent_count.load() << " TDs consisting of "
         << m_td_sent_tc_count.load() << " TCs. " << m_td_paused_count.load() << " TDs (" << m_td_paused_tc_count.load()
         << " TCs) were created during pause, and " << m_td_inhibited_count.load() << " TDs ("
         << m_td_inhibited_tc_count.load() << " TCs) were inhibited. " << m_td_dropped_count.load() << " TDs ("
         << m_td_dropped_tc_count.load() << " TCs) were dropped. " << m_td_cleared_count.load() << " TDs ("
         << m_td_cleared_tc_count.load() << " TCs) were cleared.";
  if (m_ignoring_tc_types) {
    TLOG() << "Ignored " << m_tc_ignored_count.load() << " TCs.";
  }
  if (m_use_bitwords) {
    TLOG() << "Not triggered (failed bitword check) on " << m_td_not_triggered_count.load() << " TDs consisting of "
           << m_td_not_triggered_tc_count.load() << " TCs.";
  }

  m_lc_kLive_count = m_livetime_counter->get_time(LivetimeCounter::State::kLive);
  m_lc_kPaused_count = m_livetime_counter->get_time(LivetimeCounter::State::kPaused);
  m_lc_kDead_count = m_livetime_counter->get_time(LivetimeCounter::State::kDead);
  m_lc_kLive = m_lc_kLive_count;
  m_lc_kPaused = m_lc_kPaused_count;
  m_lc_kDead = m_lc_kDead_count;

  m_lc_deadtime = m_livetime_counter->get_time(LivetimeCounter::State::kDead) +
                  m_livetime_counter->get_time(LivetimeCounter::State::kPaused);
}

void
ModuleLevelTrigger::call_tc_decision(const ModuleLevelTrigger::PendingTD& pending_td)
{

  m_TD_bitword = get_TD_bitword(pending_td);
  TLOG_DEBUG(TLVL_DEBUG_MEDIUM) << "[MLT] TD has bitword: " << m_TD_bitword << " "
                                << static_cast<dfmessages::trigger_type_t>(m_TD_bitword.to_ulong());
  if (m_use_bitwords) {
    // Check trigger bitwords
    m_bitword_check = check_trigger_bitwords();
  }

  if ((!m_use_bitwords) || (m_bitword_check)) {

    dfmessages::TriggerDecision decision = create_decision(pending_td);

    if ((!m_paused.load() && !m_dfo_is_busy.load())) {

      TLOG_DEBUG(TLVL_DEBUG_LOW)
        << "[MLT] Sending a decision with triggernumber " << decision.trigger_number << " timestamp "
        << decision.trigger_timestamp << " start " << decision.components.back().window_begin << " end "
        << decision.components.back().window_end << " number of links " << decision.components.size()
        << " based on TC of type "
        << static_cast<std::underlying_type_t<decltype(pending_td.contributing_tcs[m_earliest_tc_index].type)>>(
             pending_td.contributing_tcs[m_earliest_tc_index].type);

      try {
        auto td_sender = get_iom_sender<dfmessages::TriggerDecision>(m_td_output_connection);

        if (m_use_latency_monit) {
          // block to update latency TD send vs readout time window start
          m_system_time =
            std::chrono::duration_cast<std::chrono::milliseconds>(system_clock::now().time_since_epoch()).count();
          m_td_send_vs_ro_start.store(
            fabs(m_system_time - decision.components.back().window_begin * m_clock_ticks_to_ms - m_initial_offset));
          m_td_send_vs_ro_end.store(
            fabs(m_system_time - decision.components.back().window_end * m_clock_ticks_to_ms - m_initial_offset));
        }

        td_sender->send(std::move(decision), std::chrono::milliseconds(1));

        m_td_sent_count++;
        m_new_td_sent_count++;
        m_td_sent_tc_count += pending_td.contributing_tcs.size();
        m_last_trigger_number++;
        add_td(pending_td);
      } catch (const ers::Issue& e) {
        ers::error(e);
        TLOG_DEBUG(TLVL_IMPORTANT) << "[MLT] The network is misbehaving: it accepted TD but the send failed for "
                                   << pending_td.contributing_tcs[m_earliest_tc_index].time_candidate;
        m_td_queue_timeout_expired_err_count++;
        m_td_queue_timeout_expired_err_tc_count += pending_td.contributing_tcs.size();
      }

    } else if (m_paused.load()) {
      ++m_td_paused_count;
      m_td_paused_tc_count += pending_td.contributing_tcs.size();
      TLOG_DEBUG(TLVL_IMPORTANT)
        << "[MLT] Triggers are paused. Not sending a TriggerDecision for pending TD with start/end times "
        << pending_td.readout_start << "/" << pending_td.readout_end;
    } else {
      ers::warning(TriggerInhibited(ERS_HERE, m_run_number));
      TLOG_DEBUG(TLVL_IMPORTANT) << "[MLT] The DFO is busy. Not sending a TriggerDecision for candidate timestamp "
                                 << pending_td.contributing_tcs[m_earliest_tc_index].time_candidate;
      m_td_inhibited_count++;
      m_new_td_inhibited_count++;
      m_td_inhibited_tc_count += pending_td.contributing_tcs.size();
    }
    m_td_total_count++;
    m_new_td_total_count++;
  } else { // trigger bitword check
    m_td_not_triggered_count++;
    m_td_not_triggered_tc_count += pending_td.contributing_tcs.size();
  }
}

void
ModuleLevelTrigger::add_tc(const triggeralgs::TriggerCandidate& tc)
{
  bool tc_dealt = false;
  int64_t tc_wallclock_arrived =
    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();

  if (m_tc_merging || m_ignore_tc_pileup) {

    for (std::vector<PendingTD>::iterator it = m_pending_tds.begin(); it != m_pending_tds.end();) {
      // Don't deal with TC here if there's no overlap
      if (!check_overlap(tc, *it)) {
        ++it;
        continue;
      }

      // If overlap and ignoring, we drop the TC and flag it as dealt with.
      if (m_ignore_tc_pileup) {
        m_td_dropped_tc_count++;
        TLOG_DEBUG(TLVL_DEBUG_MEDIUM) << "[MLT] TD overlapping previous TD, dropping!";
        tc_dealt = true;
        break;
      }

      // If we're here, TC merging must be on, in which case we're actually
      // going to merge the TC into the TD.
      it->contributing_tcs.push_back(tc);
      if ((m_use_readout_map) && (m_readout_window_map.count(tc.type))) {
        TLOG_DEBUG(TLVL_DEBUG_LOW) << "[MLT] TC with start/end times "
                                   << tc.time_candidate - m_readout_window_map[tc.type].first << "/"
                                   << tc.time_candidate + m_readout_window_map[tc.type].second
                                   << " overlaps with pending TD with start/end times " << it->readout_start << "/"
                                   << it->readout_end;
        it->readout_start = ((tc.time_candidate - m_readout_window_map[tc.type].first) >= it->readout_start)
                              ? it->readout_start
                              : (tc.time_candidate - m_readout_window_map[tc.type].first);
        it->readout_end = ((tc.time_candidate + m_readout_window_map[tc.type].second) >= it->readout_end)
                            ? (tc.time_candidate + m_readout_window_map[tc.type].second)
                            : it->readout_end;
      } else {
        TLOG_DEBUG(TLVL_DEBUG_LOW) << "[MLT] TC with start/end times " << tc.time_start << "/" << tc.time_end
                                   << " overlaps with pending TD with start/end times " << it->readout_start << "/"
                                   << it->readout_end;
        it->readout_start = (tc.time_start >= it->readout_start) ? it->readout_start : tc.time_start;
        it->readout_end = (tc.time_end >= it->readout_end) ? tc.time_end : it->readout_end;
      }
      it->walltime_expiration = tc_wallclock_arrived + m_buffer_timeout;
      tc_dealt = true;
      break;
    }
  }

  // Don't do anything else if we've dealt with the TC already
  if (tc_dealt) {
    return;
  }

  // Create a new TD out of the TC
  PendingTD td_candidate;
  td_candidate.contributing_tcs.push_back(tc);
  if ((m_use_readout_map) && (m_readout_window_map.count(tc.type))) {
    td_candidate.readout_start = tc.time_candidate - m_readout_window_map[tc.type].first;
    td_candidate.readout_end = tc.time_candidate + m_readout_window_map[tc.type].second;
  } else {
    td_candidate.readout_start = tc.time_start;
    td_candidate.readout_end = tc.time_end;
  }
  td_candidate.walltime_expiration = tc_wallclock_arrived + m_buffer_timeout;
  m_pending_tds.push_back(td_candidate);

  if (m_use_latency_monit) {
    // block to update latency TD made vs readout time window start
    m_system_time =
      std::chrono::duration_cast<std::chrono::milliseconds>(system_clock::now().time_since_epoch()).count();
    m_td_made_vs_ro.store(fabs(m_system_time - td_candidate.readout_start * m_clock_ticks_to_ms - m_initial_offset));
  }
}

void
ModuleLevelTrigger::add_tc_ignored(const triggeralgs::TriggerCandidate& tc)
{
  for (std::vector<PendingTD>::iterator it = m_pending_tds.begin(); it != m_pending_tds.end();) {
    // Don't add the TC if it doesn't overlap
    if (!check_overlap(tc, *it)) {
      ++it;
      continue;
    }

    if ((m_use_readout_map) && (m_readout_window_map.count(tc.type))) {
      TLOG_DEBUG(TLVL_DEBUG_LOW) << "[MLT] !Ignored! TC with start/end times "
                                 << tc.time_candidate - m_readout_window_map[tc.type].first << "/"
                                 << tc.time_candidate + m_readout_window_map[tc.type].second
                                 << " overlaps with pending TD with start/end times " << it->readout_start << "/"
                                 << it->readout_end;
    } else {
      TLOG_DEBUG(TLVL_DEBUG_LOW) << "[MLT] !Ignored! TC with start/end times " << tc.time_start << "/" << tc.time_end
                                 << " overlaps with pending TD with start/end times " << it->readout_start << "/"
                                 << it->readout_end;
    }
    it->contributing_tcs.push_back(tc);
    break;
  }
}

bool
ModuleLevelTrigger::check_overlap(const triggeralgs::TriggerCandidate& tc, const PendingTD& pending_td)
{
  if ((m_use_readout_map) && (m_readout_window_map.count(tc.type))) {
    return !(((tc.time_candidate + m_readout_window_map[tc.type].second) < pending_td.readout_start) ||
             ((tc.time_candidate - m_readout_window_map[tc.type].first > pending_td.readout_end)));
  } else {
    return !((tc.time_end < pending_td.readout_start) || (tc.time_start > pending_td.readout_end));
  }
}

bool
ModuleLevelTrigger::check_overlap_td(const PendingTD& pending_td)
{
  bool overlap;

  for (PendingTD sent_td : m_sent_tds) {
    if ((pending_td.readout_end < sent_td.readout_start) || (pending_td.readout_start > sent_td.readout_end)) {
      overlap = false;
    } else {
      overlap = true;
      TLOG_DEBUG(TLVL_DEBUG_LOW) << "[MLT] Pending TD with start/end " << pending_td.readout_start << "/"
                                 << pending_td.readout_end << " overlaps with sent TD with start/end "
                                 << sent_td.readout_start << "/" << sent_td.readout_end;
      break;
    }
  }
  return overlap;
}

void
ModuleLevelTrigger::add_td(const PendingTD& pending_td)
{
  m_sent_tds.push_back(pending_td);
  while (m_sent_tds.size() > 20) {
    m_sent_tds.erase(m_sent_tds.begin());
  }
}

std::vector<ModuleLevelTrigger::PendingTD>
ModuleLevelTrigger::get_ready_tds(std::vector<PendingTD>& pending_tds)
{
  std::vector<PendingTD> return_tds;
  for (std::vector<PendingTD>::iterator it = pending_tds.begin(); it != pending_tds.end();) {
    auto timestamp_now =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
    if (timestamp_now >= it->walltime_expiration) {
      return_tds.push_back(*it);
      it = pending_tds.erase(it);
    } else if (check_td_readout_length(*it)) { // Also pass on TDs with (too) long readout window
      return_tds.push_back(*it);
      it = pending_tds.erase(it);
    } else {
      ++it;
    }
  }
  return return_tds;
}

int
ModuleLevelTrigger::get_earliest_tc_index(const PendingTD& pending_td)
{
  int earliest_tc_index = -1;
  triggeralgs::timestamp_t earliest_tc_time;
  for (int i = 0; i < static_cast<int>(pending_td.contributing_tcs.size()); i++) {
    if (earliest_tc_index == -1) {
      earliest_tc_time = pending_td.contributing_tcs[i].time_candidate;
      earliest_tc_index = i;
    } else {
      if (pending_td.contributing_tcs[i].time_candidate < earliest_tc_time) {
        earliest_tc_time = pending_td.contributing_tcs[i].time_candidate;
        earliest_tc_index = i;
      }
    }
  }
  return earliest_tc_index;
}

bool
ModuleLevelTrigger::check_td_readout_length(const PendingTD& pending_td)
{
  bool td_too_long = false;
  if (static_cast<int64_t>(pending_td.readout_end - pending_td.readout_start) >= m_td_readout_limit) {
    td_too_long = true;
    TLOG_DEBUG(TLVL_DEBUG_LOW) << "[MLT] Too long readout window: "
                               << (pending_td.readout_end - pending_td.readout_start) << ", sending immediate TD!";
  }
  return td_too_long;
}

void
ModuleLevelTrigger::flush_td_vectors()
{
  TLOG_DEBUG(TLVL_DEBUG_INFO) << "[MLT] Flushing TDs. Size: " << m_pending_tds.size();
  std::lock_guard<std::mutex> lock(m_td_vector_mutex);
  for (PendingTD pending_td : m_pending_tds) {
    call_tc_decision(pending_td);
  }
}

void
ModuleLevelTrigger::clear_td_vectors()
{
  std::lock_guard<std::mutex> lock(m_td_vector_mutex);
  TLOG_DEBUG(TLVL_DEBUG_INFO) << "[MLT] clear_td_vectors() clearing " << m_pending_tds.size() << " pending TDs and "
                              << m_sent_tds.size() << " sent TDs";
  m_td_cleared_count += m_pending_tds.size();
  for (PendingTD pending_td : m_pending_tds) {
    m_td_cleared_tc_count += pending_td.contributing_tcs.size();
  }
  m_pending_tds.clear();
  m_sent_tds.clear();
}

void
ModuleLevelTrigger::dfo_busy_callback(dfmessages::TriggerInhibit& inhibit)
{
  TLOG_DEBUG(TLVL_DEBUG_HIGH) << "[MLT] Received inhibit message with busy status " << inhibit.busy
                              << " and run number " << inhibit.run_number;
  if (inhibit.run_number == m_run_number) {
    TLOG_DEBUG(TLVL_DEBUG_HIGH) << "[MLT] Changing our flag for the DFO busy state from " << m_dfo_is_busy.load()
                                << " to " << inhibit.busy;
    m_dfo_is_busy = inhibit.busy;
    LivetimeCounter::State state = (inhibit.busy) ? LivetimeCounter::State::kDead : LivetimeCounter::State::kLive;
    m_livetime_counter->set_state(state);
  }
}

bool
ModuleLevelTrigger::check_trigger_type_ignore(int tc_type)
{
  bool ignore = false;
  for (std::vector<int>::iterator it = m_ignored_tc_types.begin(); it != m_ignored_tc_types.end();) {
    if (tc_type == *it) {
      ignore = true;
      break;
    }
    ++it;
  }
  return ignore;
}

std::bitset<64>
ModuleLevelTrigger::get_TD_bitword(const PendingTD& ready_td)
{
  // get only unique types
  std::vector<int> tc_types;
  for (auto tc : ready_td.contributing_tcs) {
    tc_types.push_back(static_cast<int>(tc.type));
  }
  tc_types.erase(std::unique(tc_types.begin(), tc_types.end()), tc_types.end());

  // form TD bitword
  std::bitset<64> td_bitword;
  for (auto tc_type : tc_types) {
    td_bitword.set(tc_type);
  }
  return td_bitword;
}

void
ModuleLevelTrigger::print_trigger_bitwords(std::vector<std::bitset<64>> trigger_bitwords)
{
  TLOG_DEBUG(TLVL_DEBUG_INFO) << "[MLT] Configured trigger words:";
  for (auto bitword : trigger_bitwords) {
    TLOG_DEBUG(TLVL_DEBUG_INFO) << bitword;
  }
  return;
}

void
ModuleLevelTrigger::print_bitword_flags(nlohmann::json m_trigger_bitwords_json)
{
  TLOG_DEBUG(TLVL_DEBUG_INFO) << "[MLT] Configured trigger flags:";
  for (auto bitflag : m_trigger_bitwords_json) {
    TLOG_DEBUG(TLVL_DEBUG_INFO) << bitflag;
  }
  return;
}

bool
ModuleLevelTrigger::check_trigger_bitwords()
{
  bool trigger_check = false;

  // Check if m_TD_bitword has any vetoed bits set
  if ((m_TD_bitword & veto_bitset) != 0) {
    TLOG_DEBUG(TLVL_DEBUG_ALL) << "[MLT] TD word vetoed: " << m_TD_bitword;
    return false; // If any vetoed bit is set in m_TD_bitword, reject immediately
  }

  for (auto bitword : m_trigger_bitwords) {
    TLOG_DEBUG(TLVL_DEBUG_ALL) << "[MLT] TD word: " << m_TD_bitword << ", bitword: " << bitword;
    trigger_check = ((m_TD_bitword & bitword) == bitword);
    TLOG_DEBUG(TLVL_DEBUG_ALL) << "[MLT] &: " << (m_TD_bitword & bitword);
    TLOG_DEBUG(TLVL_DEBUG_ALL) << "[MLT] trigger?: " << trigger_check;
    if (trigger_check == true)
      break;
  }
  return trigger_check;
}

void
ModuleLevelTrigger::set_trigger_bitwords()
{
  for (auto flag : m_trigger_bitwords_json) {
    std::bitset<64> temp_bitword;
    for (auto bit : flag) {
      temp_bitword.set(bit);
    }
    m_trigger_bitwords.push_back(temp_bitword);
  }
  return;
}

void
ModuleLevelTrigger::parse_readout_map(const nlohmann::json& data)
{
  for (auto readout_type : data) {
    m_readout_window_map[static_cast<trgdataformats::TriggerCandidateData::Type>(readout_type["candidate_type"])] = {
      readout_type["time_before"], readout_type["time_after"]
    };
  }
  return;
}

void
ModuleLevelTrigger::print_readout_map(std::map<trgdataformats::TriggerCandidateData::Type,
                                               std::pair<triggeralgs::timestamp_t, triggeralgs::timestamp_t>> map)
{
  TLOG_DEBUG(TLVL_DEBUG_INFO) << "[MLT] TD Readout map:";
  for (auto const& [key, val] : map) {
    TLOG_DEBUG(TLVL_DEBUG_INFO) << "[MLT] Type: " << static_cast<int>(key) << ", before: " << val.first
                                << ", after: " << val.second;
  }
  return;
}

void
ModuleLevelTrigger::parse_group_links(const nlohmann::json& data)
{
  for (auto group : data) {
    const nlohmann::json& temp_links_data = group["links"];
    std::vector<dfmessages::SourceID> temp_links;
    for (auto link : temp_links_data) {
      temp_links.push_back(
        dfmessages::SourceID{ daqdataformats::SourceID::string_to_subsystem(link["subsystem"]), link["element"] });
    }
    m_group_links.insert({ group["group"], temp_links });
  }
  return;
}

void
ModuleLevelTrigger::print_group_links()
{
  TLOG_DEBUG(TLVL_DEBUG_INFO) << "[MLT] Group Links:";
  for (auto const& [key, val] : m_group_links) {
    TLOG_DEBUG(TLVL_DEBUG_INFO) << "Group: " << key;
    for (auto const& link : val) {
      TLOG_DEBUG(TLVL_DEBUG_INFO) << link;
    }
  }
  TLOG_DEBUG(TLVL_DEBUG_INFO) << " ";
  return;
}

dfmessages::ComponentRequest
ModuleLevelTrigger::create_request_for_link(dfmessages::SourceID link,
                                            triggeralgs::timestamp_t start,
                                            triggeralgs::timestamp_t end)
{
  dfmessages::ComponentRequest request;
  request.component = link;
  request.window_begin = start;
  request.window_end = end;

  TLOG_DEBUG(TLVL_DEBUG_HIGH) << "[MLT] link: " << link;
  TLOG_DEBUG(TLVL_DEBUG_HIGH) << "[MLT] setting request start: " << request.window_begin;
  TLOG_DEBUG(TLVL_DEBUG_HIGH) << "[MLT] setting request end: " << request.window_end;

  return request;
}

std::vector<dfmessages::ComponentRequest>
ModuleLevelTrigger::create_all_decision_requests(std::vector<dfmessages::SourceID> links,
                                                 triggeralgs::timestamp_t start,
                                                 triggeralgs::timestamp_t end)
{
  std::vector<dfmessages::ComponentRequest> requests;
  for (auto link : links) {
    requests.push_back(create_request_for_link(link, start, end));
  }
  return requests;
}

void
ModuleLevelTrigger::add_requests_to_decision(dfmessages::TriggerDecision& decision,
                                             std::vector<dfmessages::ComponentRequest> requests)
{
  for (auto request : requests) {
    // No custom detector readouts for non-detector readout subsystems
    if (request.component.subsystem != daqdataformats::SourceID::Subsystem::kDetectorReadout) {
      decision.components.push_back(request);
      continue;
    }

    // Get the subdetector id from our map
    dunedaq::detdataformats::DetID::Subdetector detid;
    detid = static_cast<dunedaq::detdataformats::DetID::Subdetector>(m_srcid_geoid_map[request.component].det_id);

    // No custom detector readout if we didn't specify it in the map
    if (!m_subdetector_readout_window_map.count(detid)) {
      decision.components.push_back(request);
      continue;
    }

    // Override readout window with the custom subdetector readout window map
    request.window_begin = decision.trigger_timestamp - m_subdetector_readout_window_map[detid].first;
    request.window_end   = decision.trigger_timestamp + m_subdetector_readout_window_map[detid].second;
    decision.components.push_back(request);
  }
}

void
ModuleLevelTrigger::parse_roi_conf(const nlohmann::json& data)
{
  int counter = 0;
  float run_sum = 0;
  for (auto group : data) {
    roi_group temp_roi_group;
    temp_roi_group.n_links = group["number_of_link_groups"];
    temp_roi_group.prob = group["probability"];
    temp_roi_group.time_window = group["time_window"];
    temp_roi_group.mode = group["groups_selection_mode"];
    m_roi_conf.insert({ counter, temp_roi_group });
    m_roi_conf_ids.push_back(counter);
    m_roi_conf_probs.push_back(group["probability"]);
    run_sum += static_cast<float>(group["probability"]);
    m_roi_conf_probs_c.push_back(run_sum);
    counter++;
  }
  return;
}

void
ModuleLevelTrigger::print_roi_conf(std::map<int, roi_group> roi_conf)
{
  TLOG_DEBUG(TLVL_DEBUG_INFO) << "[MLT] ROI CONF";
  for (const auto& [key, value] : roi_conf) {
    TLOG_DEBUG(TLVL_DEBUG_INFO) << "ID: " << key;
    TLOG_DEBUG(TLVL_DEBUG_INFO) << "n links: " << value.n_links;
    TLOG_DEBUG(TLVL_DEBUG_INFO) << "prob: " << value.prob;
    TLOG_DEBUG(TLVL_DEBUG_INFO) << "time: " << value.time_window;
    TLOG_DEBUG(TLVL_DEBUG_INFO) << "mode: " << value.mode;
  }
  TLOG_DEBUG(TLVL_DEBUG_INFO) << " ";
  return;
}

float
ModuleLevelTrigger::get_random_num_float(float limit)
{
  float rnd = (double)rand() / RAND_MAX;
  return rnd * (limit);
}

int
ModuleLevelTrigger::pick_roi_group_conf()
{
  float rnd_num = get_random_num_float(m_roi_conf_probs_c.back());
  for (int i = 0; i < static_cast<int>(m_roi_conf_probs_c.size()); i++) {
    if (rnd_num < m_roi_conf_probs_c[i]) {
      return i;
    }
  }
  return -1;
}

int
ModuleLevelTrigger::get_random_num_int()
{
  int range = m_total_group_links;
  int rnd = rand() % range;
  return rnd;
}

void
ModuleLevelTrigger::roi_readout_make_requests(dfmessages::TriggerDecision& decision)
{
  // Get configuration at random (weighted)
  int group_pick = pick_roi_group_conf();
  if (group_pick != -1) {
    roi_group this_group = m_roi_conf[m_roi_conf_ids[group_pick]];
    std::vector<dfmessages::SourceID> links;

    // If mode is random, pick groups to request at random
    if (this_group.mode == "kRandom") {
      TLOG_DEBUG(TLVL_DEBUG_HIGH) << "[MLT] RAND";
      std::set<int> groups;
      while (static_cast<int>(groups.size()) < this_group.n_links) {
        groups.insert(get_random_num_int());
      }
      for (auto r_id : groups) {
        links.insert(links.end(), m_group_links[r_id].begin(), m_group_links[r_id].end());
      }
      // Otherwise, read sequntially by IDs, starting at 0
    } else {
      TLOG_DEBUG(TLVL_DEBUG_HIGH) << "[MLT] SEQ";
      int r_id = 0;
      while (r_id < this_group.n_links) {
        links.insert(links.end(), m_group_links[r_id].begin(), m_group_links[r_id].end());
        r_id++;
      }
    }

    TLOG_DEBUG(TLVL_DEBUG_HIGH) << "[MLT] TD timestamp: " << decision.trigger_timestamp;
    TLOG_DEBUG(TLVL_DEBUG_HIGH) << "[MLT] group window: " << this_group.time_window;

    // Once the components are prepared, create requests and append them to decision
    std::vector<dfmessages::ComponentRequest> requests = create_all_decision_requests(
      links, decision.trigger_timestamp - this_group.time_window, decision.trigger_timestamp + this_group.time_window);
    add_requests_to_decision(decision, requests);
    links.clear();
  }
  return;
}

} // namespace trigger
} // namespace dunedaq

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::ModuleLevelTrigger)

// Local Variables:
// c-basic-offset: 2
// End:
