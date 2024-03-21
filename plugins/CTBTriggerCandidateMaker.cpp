/**
 * @file CTBTriggerCandidateMaker.cpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "CTBTriggerCandidateMaker.hpp"

#include "appfwk/DAQModuleHelper.hpp"
#include "trgdataformats/Types.hpp"
#include "trigger/TriggerCandidate_serialization.hpp"
#include "iomanager/IOManager.hpp"
#include "rcif/cmd/Nljs.hpp"

#include <regex>
#include <string>

namespace dunedaq {
namespace trigger {

CTBTriggerCandidateMaker::CTBTriggerCandidateMaker(const std::string& name)
  : DAQModule(name)
  , m_output_queue(nullptr)
  , m_queue_timeout(100)
{

  register_command("conf", &CTBTriggerCandidateMaker::do_conf);
  register_command("start", &CTBTriggerCandidateMaker::do_start);
  register_command("stop", &CTBTriggerCandidateMaker::do_stop);
  register_command("scrap", &CTBTriggerCandidateMaker::do_scrap);
}

triggeralgs::TriggerCandidate
CTBTriggerCandidateMaker::HSIEventToTriggerCandidate(const dfmessages::HSIEvent& data)
{
  triggeralgs::TriggerCandidate candidate;
  if (m_hsi_passthrough == true) {
    TLOG_DEBUG(3) << "HSI passthrough applied, modified readout window is set";
    candidate.time_start = data.timestamp - m_hsi_pt_before;
    candidate.time_end = data.timestamp + m_hsi_pt_after;
  } else {
    if (m_detid_offsets_map.count(data.signal_map)) {
      // clang-format off
      candidate.time_start = data.timestamp - m_detid_offsets_map[data.signal_map].first;  // time_start
      candidate.time_end   = data.timestamp + m_detid_offsets_map[data.signal_map].second; // time_end,
      // clang-format on    
    } else {
      throw dunedaq::trigger::SignalTypeError(ERS_HERE, get_name(), data.signal_map);
    }
  }
  candidate.time_candidate = data.timestamp;
  // throw away bits 31-16 of header, that's OK for now
  candidate.detid = { static_cast<triggeralgs::detid_t>(data.signal_map) }; // NOLINT(build/unsigned)
  candidate.type = triggeralgs::TriggerCandidate::Type::kCTB;

  candidate.algorithm = triggeralgs::TriggerCandidate::Algorithm::kHSIEventToTriggerCandidate;
  candidate.inputs = {};

  return candidate;
}


void
CTBTriggerCandidateMaker::do_conf(const nlohmann::json& config)
{
  auto params = config.get<dunedaq::trigger::ctbtriggercandidatemaker::Conf>();
  m_detid_offsets_map[params.s0.signal_type] = { params.s0.time_before, params.s0.time_after };
  m_detid_offsets_map[params.s1.signal_type] = { params.s1.time_before, params.s1.time_after };
  m_detid_offsets_map[params.s2.signal_type] = { params.s2.time_before, params.s2.time_after };
  m_hsi_passthrough = params.hsi_trigger_type_passthrough;
  m_hsi_pt_before = params.s0.time_before;
  m_hsi_pt_after = params.s0.time_after;
  m_prescale = params.prescale;
  m_prescale_flag = (m_prescale > 1) ? true : false;
  TLOG_DEBUG(2) << get_name() + " configured.";
  if (m_prescale_flag){
    TLOG(2) << "Running with prescale at: " << m_prescale;
  }
}

void
CTBTriggerCandidateMaker::init(const nlohmann::json& iniobj)
{
  try {
    auto ci = appfwk::connection_index(iniobj, {"output", "hsi_input"});	   
    m_output_queue = get_iom_sender<triggeralgs::TriggerCandidate>(ci["output"]);
    m_hsievent_input = get_iom_receiver<dfmessages::HSIEvent>(ci["hsi_input"]);
  } catch (const ers::Issue& excpt) {
    throw dunedaq::trigger::InvalidQueueFatalError(ERS_HERE, get_name(), "input/output", excpt);
  }
}

void
CTBTriggerCandidateMaker::do_start(const nlohmann::json& startobj)
{
  // OpMon.
  m_tsd_received_count.store(0);
  m_tc_sent_count.store(0);
  m_tc_sig_type_err_count.store(0);
  m_tc_total_count.store(0);

  auto start_params = startobj.get<rcif::cmd::StartParams>();
  m_run_number.store(start_params.run);

  m_hsievent_input->add_callback(std::bind(&CTBTriggerCandidateMaker::receive_hsievent, this, std::placeholders::_1));
  
  TLOG_DEBUG(2) << get_name() + " successfully started.";
}

void
CTBTriggerCandidateMaker::do_stop(const nlohmann::json&)
{
  m_hsievent_input->remove_callback();

  TLOG() << "Received " << m_tsd_received_count << " HSIEvent messages. Successfully sent " << m_tc_sent_count
         << " TriggerCandidates";
  TLOG_DEBUG(2) << get_name() + " successfully stopped.";
}

void
CTBTriggerCandidateMaker::receive_hsievent(dfmessages::HSIEvent& data)
{
  TLOG_DEBUG(3) << "Activity received with timestamp " << data.timestamp << ", sequence_counter " << data.sequence_counter
                << ", and run_number " << data.run_number;

  if (data.run_number != m_run_number) {
    ers::error(dunedaq::trigger::InvalidHSIEventRunNumber(ERS_HERE, get_name(), data.run_number, m_run_number,
                                                          data.timestamp, data.sequence_counter));
    return;
  }

  ++m_tsd_received_count;

  if (m_prescale_flag) {
    if (m_tsd_received_count % m_prescale != 0){
      return;
    } 
  }

  if (m_hsi_passthrough == true){
    TLOG_DEBUG(3) << "Signal_map: " << data.signal_map << ", trigger bits: " << (std::bitset<16>)data.signal_map;
    try {
      if ((data.signal_map & 0xffffff00) != 0){
        throw dunedaq::trigger::BadTriggerBitmask(ERS_HERE, get_name(), (std::bitset<16>)data.signal_map);
      }
    } catch (BadTriggerBitmask& e) {
      ers::error(e);
      return;
    }
  }

  triggeralgs::TriggerCandidate candidate;
  try {
    candidate = HSIEventToTriggerCandidate(data);
  } catch (SignalTypeError& e) {
    m_tc_sig_type_err_count++;
    ers::error(e);
    return;
  }

  bool successfullyWasSent = false;
  while (!successfullyWasSent) {
    try {
        triggeralgs::TriggerCandidate candidate_copy(candidate);
      m_output_queue->send(std::move(candidate_copy), m_queue_timeout);
      successfullyWasSent = true;
      ++m_tc_sent_count;
    } catch (const dunedaq::iomanager::TimeoutExpired& excpt) {
      std::ostringstream oss_warn;
      oss_warn << "push to output queue \"" << m_output_queue->get_name() << "\"";
      ers::warning(
        dunedaq::iomanager::TimeoutExpired(ERS_HERE, get_name(), oss_warn.str(), m_queue_timeout.count()));
    }
  }
  m_tc_total_count++;
}

void
CTBTriggerCandidateMaker::do_scrap(const nlohmann::json&)
{}

void
CTBTriggerCandidateMaker::get_info(opmonlib::InfoCollector& ci, int /*level*/)
{
  ctbtriggercandidatemakerinfo::Info i;

  i.tsd_received_count = m_tsd_received_count.load();
  i.tc_sent_count = m_tc_sent_count.load();
  i.tc_sig_type_err_count = m_tc_sig_type_err_count.load();
  i.tc_total_count = m_tc_total_count.load();

  ci.add(i);
}

} // namespace trigger
} // namespace dunedaq

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::CTBTriggerCandidateMaker)
