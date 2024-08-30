/**
 * @file TCProcessor.hpp TPC TP specific Task based raw processor
 *
 * This is part of the DUNE DAQ , copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#include "trigger/TCProcessor.hpp" // NOLINT(build/include)

#include "iomanager/Sender.hpp"
#include "logging/Logging.hpp"

#include "datahandlinglibs/FrameErrorRegistry.hpp"
#include "datahandlinglibs/DataHandlingIssues.hpp"
#include "datahandlinglibs/ReadoutLogging.hpp"
#include "datahandlinglibs/models/IterableQueueModel.hpp"
#include "datahandlinglibs/utils/ReusableThread.hpp"

#include "trigger/TCWrapper.hpp"
#include "triggeralgs/TriggerCandidate.hpp"

#include "appmodel/TCDataProcessor.hpp"
#include "appmodel/TriggerDataHandlerModule.hpp"

using dunedaq::datahandlinglibs::logging::TLVL_BOOKKEEPING;
using dunedaq::datahandlinglibs::logging::TLVL_TAKE_NOTE;

// THIS SHOULDN'T BE HERE!!!!! But it is necessary.....
DUNE_DAQ_TYPESTRING(dunedaq::trigger::TCWrapper, "TriggerCandidate")

namespace dunedaq {
namespace trigger {

TCProcessor::TCProcessor(std::unique_ptr<datahandlinglibs::FrameErrorRegistry>& error_registry, bool post_processing_enabled)
  : datahandlinglibs::TaskRawDataProcessorModel<TCWrapper>(error_registry, post_processing_enabled)
{
}

TCProcessor::~TCProcessor()
{}

void
TCProcessor::start(const nlohmann::json& args)
{
  m_running_flag.store(true);
  m_send_trigger_decisions_thread = std::thread(&TCProcessor::send_trigger_decisions, this);
  pthread_setname_np(m_send_trigger_decisions_thread.native_handle(), "mlt-dec"); // TODO: originally mlt-trig-dec

  // Reset stats
  m_new_tds = 0;
  m_tds_dropped = 0;
  inherited::start(args);
}

void
TCProcessor::stop(const nlohmann::json& args)
{
  inherited::stop(args);
  m_running_flag.store(false);
  m_send_trigger_decisions_thread.join();

  // Drop all TDs in vectors at run stage change. Have to do this
  // after joining m_send_trigger_decisions_thread so we don't
  // concurrently access the vectors
  clear_td_vectors();

}

void
TCProcessor::conf(const appmodel::DataHandlerModule* cfg)
{
  auto mtrg = cfg->cast<appmodel::TriggerDataHandlerModule>();	
  if (mtrg == nullptr) {
    throw(InvalidConfiguration(ERS_HERE));
  }
  for (auto output : mtrg->get_outputs()) {
   try {
      if (output->get_data_type() == "TriggerDecision") {
         m_td_sink = get_iom_sender<dfmessages::TriggerDecision>(output->UID());
      }
    } catch (const ers::Issue& excpt) {
      ers::error(datahandlinglibs::ResourceQueueError(ERS_HERE, "td", "DefaultRequestHandlerModel", excpt));
    }
  }

  auto dp = mtrg->get_module_configuration()->get_data_processor();
  auto proc_conf = dp->cast<appmodel::TCDataProcessor>();

  // Add all Source IDs to mandatoy links for now...
  for(auto const& link : mtrg->get_mandatory_source_ids()){
    m_mandatory_links.push_back(
        dfmessages::SourceID{
        daqdataformats::SourceID::string_to_subsystem(link->get_subsystem()),
        link->get_sid()});
  }  
  for(auto const& link : mtrg->get_enabled_source_ids()){
    m_mandatory_links.push_back(
        dfmessages::SourceID{
        daqdataformats::SourceID::string_to_subsystem(link->get_subsystem()),
        link->get_sid()});
  }


    // TODO: Group links!
  //m_group_links_data = conf->get_groups_links();
  parse_group_links(m_group_links_data);
  print_group_links();
  m_total_group_links = m_group_links.size();
  TLOG_DEBUG(3) << "Total group links: " << m_total_group_links;

  m_hsi_passthrough = proc_conf->get_hsi_trigger_type_passthrough();
  m_tc_merging        = proc_conf->get_merge_overlapping_tcs();
  m_buffer_timeout    = proc_conf->get_buffer_timeout();
  m_send_timed_out_tds = proc_conf->get_td_out_of_timeout();
  m_td_readout_limit  = proc_conf->get_td_readout_limit();
  m_ignored_tc_types = proc_conf->get_ignore_tc();
  m_ignoring_tc_types = (m_ignored_tc_types.size() > 0) ? true : false;
  m_use_readout_map   = proc_conf->get_use_readout_map();
  m_use_roi_readout   = proc_conf->get_use_roi_readout();
  m_use_bitwords      = proc_conf->get_use_bitwords();
  TLOG_DEBUG(3) << "Allow merging: " << m_tc_merging;
  TLOG_DEBUG(3) << "Buffer timeout: " << m_buffer_timeout;
  TLOG_DEBUG(3) << "Should send timed out TDs: " << m_send_timed_out_tds;
  TLOG_DEBUG(3) << "TD readout limit: " << m_td_readout_limit;
  TLOG_DEBUG(3) << "Use ROI readout?: " << m_use_roi_readout;

  // ROI map
  if(m_use_roi_readout){
    m_roi_conf_data = proc_conf->get_roi_group_conf();
    parse_roi_conf(m_roi_conf_data);
    print_roi_conf(m_roi_conf);
  }

  // Custom readout map
  TLOG_DEBUG(3) << "Use readout map: " << m_use_readout_map;
  if(m_use_readout_map){
    m_readout_window_map_data = proc_conf->get_tc_readout_map();
    parse_readout_map(m_readout_window_map_data);
    print_readout_map(m_readout_window_map);
  }

  // Ignoring TC types
  TLOG_DEBUG(3) << "Ignoring TC types: " << m_ignoring_tc_types;
  if(m_ignoring_tc_types){
    TLOG_DEBUG(3) << "TC types to ignore: ";
    for (std::vector<unsigned int>::iterator it = m_ignored_tc_types.begin(); it != m_ignored_tc_types.end();) {
      TLOG_DEBUG(3) << *it;
      ++it;
    }
  }

  // Trigger bitwords
  TLOG_DEBUG(3) << "Use bitwords: " << m_use_bitwords;
  if(m_use_bitwords){
    std::vector<std::string> bitwords = proc_conf->get_trigger_bitwords();
    // TODO: Print_bitword_flags(m_trigger_bitwords)
    set_trigger_bitwords(bitwords);
    print_trigger_bitwords(m_trigger_bitwords);
  }
  inherited::add_postprocess_task(std::bind(&TCProcessor::make_td, this, std::placeholders::_1));

  inherited::conf(mtrg);
}

// void
// TCProcessor::get_info(opmonlib::InfoCollector& ci, int level)
// {

//   inherited::get_info(ci, level);
//   //ci.add(info);
// }


/**
 * Pipeline Stage 2.: put valid TCs in a vector for grouping and forming of TDs
 * */
void
TCProcessor::make_td(const TCWrapper* tcw)
{
	
  auto tc = tcw->candidate;

  if ( (m_use_readout_map) && (m_readout_window_map.count(tc.type)) ) {
    TLOG_DEBUG(3) << "Got TC of type " << static_cast<int>(tc.type) << ", timestamp " << tc.time_candidate
		  << ", start/end " << tc.time_start << "/" << tc.time_end << ", readout start/end "
		  << tc.time_candidate - m_readout_window_map[tc.type].first << "/"
		  << tc.time_candidate + m_readout_window_map[tc.type].second;
  } else {
    TLOG_DEBUG(3) << "Got TC of type " << static_cast<int>(tc.type) << ", timestamp " << tc.time_candidate
		  << ", start/end " << tc.time_start << "/" << tc.time_end;
  }

  // Option to ignore TC types (if given by config)
  if (m_ignoring_tc_types == true && check_trigger_type_ignore(static_cast<unsigned int>(tc.type)) == true) {
    TLOG_DEBUG(3) << " Ignore TC type: " << static_cast<unsigned int>(tc.type);
    m_tc_ignored_count++;

	/*FIXME: comment out this block: if a TC is to be ignored it shall just be ignored! 
	  if (m_tc_merging) {
	    // Still need to check for overlap with existing TD, if overlaps, include in the TD, but don't extend
	    // readout
	    std::lock_guard<std::mutex> lock(m_td_vector_mutex);
	    add_tc_ignored(*tc);
	  }
	  */
  }
  else {
    std::lock_guard<std::mutex> lock(m_td_vector_mutex);
    add_tc(tc);
    TLOG_DEBUG(10) << "pending tds size: " << m_pending_tds.size();
  }
  return;
}

dfmessages::TriggerDecision
TCProcessor::create_decision(const PendingTD& pending_td)
{
  m_earliest_tc_index = get_earliest_tc_index(pending_td);
  TLOG_DEBUG(5) << "earliest TC index: " << m_earliest_tc_index;

  if (pending_td.contributing_tcs.size() > 1) {
    TLOG_DEBUG(5) << "!!! TD created from " << pending_td.contributing_tcs.size() << " TCs !!!";
  }

  dfmessages::TriggerDecision decision;
  decision.trigger_number = 0; // filled by MLT
  decision.run_number = 0; // filled by MLT
  decision.trigger_timestamp = pending_td.contributing_tcs[m_earliest_tc_index].time_candidate;
  decision.readout_type = dfmessages::ReadoutType::kLocalized;

  if (m_hsi_passthrough == true) {
    if (pending_td.contributing_tcs[m_earliest_tc_index].type == triggeralgs::TriggerCandidate::Type::kTiming) {
      decision.trigger_type = pending_td.contributing_tcs[m_earliest_tc_index].detid & 0xff;
    } else {
      m_trigger_type_shifted = (static_cast<int>(pending_td.contributing_tcs[m_earliest_tc_index].type) << 8);
      decision.trigger_type = m_trigger_type_shifted;
    }
  } else {
    decision.trigger_type = 1; // m_trigger_type;
  }

  TLOG_DEBUG(3) << "HSI passthrough: " << m_hsi_passthrough
                << ", TC detid: " << pending_td.contributing_tcs[m_earliest_tc_index].detid
                << ", TC type: " << static_cast<int>(pending_td.contributing_tcs[m_earliest_tc_index].type)
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
TCProcessor::send_trigger_decisions() {

 while (m_running_flag) {
    std::lock_guard<std::mutex> lock(m_td_vector_mutex);
    auto ready_tds = get_ready_tds(m_pending_tds);
    TLOG_DEBUG(10) << "ready tds: " << ready_tds.size() << ", updated pending tds: " << m_pending_tds.size();

    for (std::vector<PendingTD>::iterator it = ready_tds.begin(); it != ready_tds.end();) {
        call_tc_decision(*it);
        ++it;
    }
 }
}

void
TCProcessor::call_tc_decision(const TCProcessor::PendingTD& pending_td)
{

  if (m_use_bitwords) {
    // Check trigger bitwords
    m_TD_bitword = get_TD_bitword(pending_td);
    m_bitword_check = check_trigger_bitwords();
  }

  if ((!m_use_bitwords) || (m_bitword_check)) {

    dfmessages::TriggerDecision decision = create_decision(pending_td);
    auto tn = decision.trigger_number;
    auto td_ts = decision.trigger_timestamp;

    if(!m_td_sink->try_send(std::move(decision), iomanager::Sender::s_no_block)) {
        ers::warning(TDDropped(ERS_HERE, tn, td_ts));
        m_tds_dropped++;
    }
    else {
	m_new_tds++;
    }
  } 
}


void
TCProcessor::add_tc(const triggeralgs::TriggerCandidate tc)
{
  bool added_to_existing = false;
  int64_t tc_wallclock_arrived =
    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();

  if (m_tc_merging) {

    for (std::vector<PendingTD>::iterator it = m_pending_tds.begin(); it != m_pending_tds.end();) {
      if (check_overlap(tc, *it)) {
        it->contributing_tcs.push_back(tc);
        if ( (m_use_readout_map) && (m_readout_window_map.count(tc.type)) ){
          TLOG_DEBUG(3) << "TC with start/end times " << tc.time_candidate - m_readout_window_map[tc.type].first << "/"
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
          TLOG_DEBUG(3) << "TC with start/end times " << tc.time_start << "/" << tc.time_end
                        << " overlaps with pending TD with start/end times " << it->readout_start << "/"
                        << it->readout_end;
          it->readout_start = (tc.time_start >= it->readout_start) ? it->readout_start : tc.time_start;
          it->readout_end = (tc.time_end >= it->readout_end) ? tc.time_end : it->readout_end;
        }
        it->walltime_expiration = tc_wallclock_arrived + m_buffer_timeout;
        added_to_existing = true;
        break;
      }
      ++it;
    }
  }

  if (!added_to_existing) {
    PendingTD td_candidate;
    td_candidate.contributing_tcs.push_back(tc);
    if ( (m_use_readout_map) && (m_readout_window_map.count(tc.type)) ){
      td_candidate.readout_start = tc.time_candidate - m_readout_window_map[tc.type].first;
      td_candidate.readout_end = tc.time_candidate + m_readout_window_map[tc.type].second;
    } else {
      td_candidate.readout_start = tc.time_start;
      td_candidate.readout_end = tc.time_end;
    }
    td_candidate.walltime_expiration = tc_wallclock_arrived + m_buffer_timeout;
    m_pending_tds.push_back(td_candidate);
  }
  return;
}

void
TCProcessor::add_tc_ignored(const triggeralgs::TriggerCandidate tc)
{
  for (std::vector<PendingTD>::iterator it = m_pending_tds.begin(); it != m_pending_tds.end();) {
    if (check_overlap(tc, *it)) {
      if ( (m_use_readout_map) && (m_readout_window_map.count(tc.type)) ) {
        TLOG_DEBUG(3) << "!Ignored! TC with start/end times " << tc.time_candidate - m_readout_window_map[tc.type].first
                      << "/" << tc.time_candidate + m_readout_window_map[tc.type].second
                      << " overlaps with pending TD with start/end times " << it->readout_start << "/"
                      << it->readout_end;
      } else {
        TLOG_DEBUG(3) << "!Ignored! TC with start/end times " << tc.time_start << "/" << tc.time_end
                      << " overlaps with pending TD with start/end times " << it->readout_start << "/"
                      << it->readout_end;
      }
      it->contributing_tcs.push_back(tc);
      break;
    }
    ++it;
  }
  return;
}

bool
TCProcessor::check_overlap(const triggeralgs::TriggerCandidate& tc, const PendingTD& pending_td)
{
  if ( (m_use_readout_map) && (m_readout_window_map.count(tc.type)) ) {
    return !(((tc.time_candidate + m_readout_window_map[tc.type].second) < pending_td.readout_start) ||
             ((tc.time_candidate - m_readout_window_map[tc.type].first > pending_td.readout_end)));
  } else {
    return !((tc.time_end < pending_td.readout_start) || (tc.time_start > pending_td.readout_end));
  }
}

std::vector<TCProcessor::PendingTD>
TCProcessor::get_ready_tds(std::vector<PendingTD>& pending_tds)
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
TCProcessor::get_earliest_tc_index(const PendingTD& pending_td)
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

bool TCProcessor::check_td_readout_length(const PendingTD& pending_td)
{
  bool td_too_long = false;
  if (static_cast<int64_t>(pending_td.readout_end - pending_td.readout_start) >= m_td_readout_limit) {
    td_too_long = true;
    TLOG_DEBUG(3) << "Too long readout window: " << (pending_td.readout_end - pending_td.readout_start)
                  << ", sending immediate TD!";
  }
  return td_too_long;
}

void
TCProcessor::clear_td_vectors()
{
  std::lock_guard<std::mutex> lock(m_td_vector_mutex);
  m_pending_tds.clear();
}
bool
TCProcessor::check_trigger_type_ignore(unsigned int tc_type)
{
  bool ignore = false;
  for (std::vector<unsigned int>::iterator it = m_ignored_tc_types.begin(); it != m_ignored_tc_types.end();) {
    if (tc_type == *it) {
      ignore = true;
      break;
    }
    ++it;
  }
  return ignore;
}

std::bitset<16>
TCProcessor::get_TD_bitword(const PendingTD& ready_td)
{
  // get only unique types
  std::vector<int> tc_types;
  for (auto tc : ready_td.contributing_tcs) {
    tc_types.push_back(static_cast<int>(tc.type));
  }
  tc_types.erase(std::unique(tc_types.begin(), tc_types.end()), tc_types.end());

  // form TD bitword
  std::bitset<16> td_bitword = 0b0000000000000000;
  for (auto tc_type : tc_types) {
    td_bitword.set(tc_type);
  }
  return td_bitword;
}

void
TCProcessor::print_trigger_bitwords(std::vector<std::bitset<16>> trigger_bitwords)
{
  TLOG_DEBUG(3) << "Configured trigger words:";
  for (auto bitword : trigger_bitwords) {
    TLOG_DEBUG(3) << bitword;
  }
  return;
}

void
TCProcessor::print_bitword_flags(nlohmann::json m_trigger_bitwords_json)
{
  TLOG_DEBUG(3) << "Configured trigger flags:";
  for (auto bitflag : m_trigger_bitwords_json) {
    TLOG_DEBUG(3) << bitflag;
  }
  return;
}

bool
TCProcessor::check_trigger_bitwords()
{
  bool trigger_check = false;
  for (auto bitword : m_trigger_bitwords) {
    TLOG_DEBUG(15) << "TD word: " << m_TD_bitword << ", bitword: " << bitword;
    trigger_check = ((m_TD_bitword & bitword) == bitword);
    TLOG_DEBUG(15) << "&: " << (m_TD_bitword & bitword);
    TLOG_DEBUG(15) << "trigger?: " << trigger_check;
    if (trigger_check == true)
      break;
  }
  return trigger_check;
}

void
TCProcessor::set_trigger_bitwords()
{
  for (auto flag : m_trigger_bitwords_json) {
    std::bitset<16> temp_bitword = 0b0000000000000000;
    for (auto bit : flag) {
      temp_bitword.set(bit);
    }
    m_trigger_bitwords.push_back(temp_bitword);
  }
  return;
}

void
TCProcessor::set_trigger_bitwords(const std::vector<std::string>& /*_bitwords*/)
{
  TLOG_DEBUG() << "Warning, bitwords not implemented with OKS (for now) and won't be used!";
  m_use_bitwords = false;
}

void
TCProcessor::parse_readout_map(const std::vector<const appmodel::TCReadoutMap*>& data)
{
  for (auto readout_type : data) {
    m_readout_window_map[static_cast<trgdataformats::TriggerCandidateData::Type>(readout_type->get_candidate_type())] = {
      readout_type->get_time_before(), readout_type->get_time_after()
    };
  }
  return;
}
void
TCProcessor::print_readout_map(std::map<trgdataformats::TriggerCandidateData::Type,
                                               std::pair<triggeralgs::timestamp_t, triggeralgs::timestamp_t>> map)
{
  TLOG_DEBUG(3) << "MLT TD Readout map:";
  for (auto const& [key, val] : map) {
    TLOG_DEBUG(3) << "Type: " << static_cast<int>(key) << ", before: " << val.first << ", after: " << val.second;
  }
  return;
}

void
TCProcessor::parse_group_links(const nlohmann::json& data)
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
TCProcessor::print_group_links()
{
  TLOG_DEBUG(3) << "MLT Group Links:";
  for (auto const& [key, val] : m_group_links) {
    TLOG_DEBUG(3) << "Group: " << key;
    for (auto const& link : val) {
      TLOG_DEBUG(3) << link;
    }
  }
  TLOG_DEBUG(3) << " ";
  return;
}
dfmessages::ComponentRequest
TCProcessor::create_request_for_link(dfmessages::SourceID link,
                                            triggeralgs::timestamp_t start,
                                            triggeralgs::timestamp_t end)
{
  dfmessages::ComponentRequest request;
  request.component = link;
  request.window_begin = start;
  request.window_end = end;

  TLOG_DEBUG(10) << "setting request start: " << request.window_begin;
  TLOG_DEBUG(10) << "setting request end: " << request.window_end;

  return request;
}

std::vector<dfmessages::ComponentRequest>
TCProcessor::create_all_decision_requests(std::vector<dfmessages::SourceID> links,
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
TCProcessor::add_requests_to_decision(dfmessages::TriggerDecision& decision,
                                             std::vector<dfmessages::ComponentRequest> requests)
{
  for (auto request : requests) {
    decision.components.push_back(request);
  }
}

void
TCProcessor::parse_roi_conf(const std::vector<const appmodel::ROIGroupConf*>& data)
{
  int counter = 0;
  float run_sum = 0;
  for (auto group : data) {
    roi_group temp_roi_group;
    temp_roi_group.n_links = group->get_number_of_link_groups();
    temp_roi_group.prob         = group->get_probability();
    temp_roi_group.time_window  = group->get_time_window();
    temp_roi_group.mode         = group->get_groups_selection_mode();
    m_roi_conf.insert({ counter, temp_roi_group });
    m_roi_conf_ids.push_back(counter);
    m_roi_conf_probs.push_back(group->get_probability());
    run_sum += static_cast<float>(group->get_probability());
    m_roi_conf_probs_c.push_back(run_sum);
    counter++;
  }
  return;
}

void
TCProcessor::print_roi_conf(std::map<int, roi_group> roi_conf)
{
  TLOG_DEBUG(3) << "ROI CONF";
  for (const auto& [key, value] : roi_conf) {
    TLOG_DEBUG(3) << "ID: " << key;
    TLOG_DEBUG(3) << "n links: " << value.n_links;
    TLOG_DEBUG(3) << "prob: " << value.prob;
    TLOG_DEBUG(3) << "time: " << value.time_window;
    TLOG_DEBUG(3) << "mode: " << value.mode;
  }
  TLOG_DEBUG(3) << " ";
  return;
}

float
TCProcessor::get_random_num_float(float limit)
{
  float rnd = (double)rand() / RAND_MAX;
  return rnd * (limit);
}

int
TCProcessor::pick_roi_group_conf()
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
TCProcessor::get_random_num_int()
{
  int range = m_total_group_links;
  int rnd = rand() % range;
  return rnd;
}
void
TCProcessor::roi_readout_make_requests(dfmessages::TriggerDecision& decision)
{
  // Get configuration at random (weighted)
  int group_pick = pick_roi_group_conf();
  if (group_pick != -1) {
    roi_group this_group = m_roi_conf[m_roi_conf_ids[group_pick]];
    std::vector<dfmessages::SourceID> links;

    // If mode is random, pick groups to request at random
    if (this_group.mode == "kRandom") {
      TLOG_DEBUG(10) << "RAND";
      std::set<int> groups;
      while (static_cast<int>(groups.size()) < this_group.n_links) {
        groups.insert(get_random_num_int());
      }
      for (auto r_id : groups) {
        links.insert(links.end(), m_group_links[r_id].begin(), m_group_links[r_id].end());
      }
      // Otherwise, read sequntially by IDs, starting at 0
    } else {
      TLOG_DEBUG(10) << "SEQ";
      int r_id = 0;
      while (r_id < this_group.n_links) {
        links.insert(links.end(), m_group_links[r_id].begin(), m_group_links[r_id].end());
        r_id++;
      }
    }

    TLOG_DEBUG(10) << "TD timestamp: " << decision.trigger_timestamp;
    TLOG_DEBUG(10) << "group window: " << this_group.time_window;

    // Once the components are prepared, create requests and append them to decision
    std::vector<dfmessages::ComponentRequest> requests =
      create_all_decision_requests(links, decision.trigger_timestamp - this_group.time_window,
                                   decision.trigger_timestamp + this_group.time_window);
    add_requests_to_decision(decision, requests);
    links.clear();
  }
  return;
}


} // namespace fdreadoutlibs
} // namespace dunedaq
