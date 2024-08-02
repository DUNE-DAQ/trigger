/**
 * @file CIBTriggerCandidateMaker.cpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "CIBTriggerCandidateMaker.hpp"
#include "trigger/Logging.hpp"

#include "appfwk/DAQModuleHelper.hpp"
#include "iomanager/IOManager.hpp"
#include "rcif/cmd/Nljs.hpp"
#include "trgdataformats/Types.hpp"
#include "trigger/TriggerCandidate_serialization.hpp"

#include <bitset>
#include <regex>
#include <string>

using dunedaq::trigger::logging::TLVL_DEBUG_ALL;
using dunedaq::trigger::logging::TLVL_DEBUG_HIGH;
using dunedaq::trigger::logging::TLVL_DEBUG_MEDIUM;
using dunedaq::trigger::logging::TLVL_GENERAL;
using dunedaq::trigger::logging::TLVL_VERY_IMPORTANT;

namespace dunedaq {
namespace trigger {

CIBTriggerCandidateMaker::CIBTriggerCandidateMaker(const std::string& name)
  : DAQModule(name)
  , m_output_queue(nullptr)
  , m_queue_timeout(100)
{

  register_command("conf", &CIBTriggerCandidateMaker::do_conf);
  register_command("start", &CIBTriggerCandidateMaker::do_start);
  register_command("stop", &CIBTriggerCandidateMaker::do_stop);
  register_command("scrap", &CIBTriggerCandidateMaker::do_scrap);
}

std::vector<triggeralgs::TriggerCandidate>
CIBTriggerCandidateMaker::HSIEventToTriggerCandidate(const dfmessages::HSIEvent& data)
{
  TLOG() << "[CIB] Converting HSI event, signal: " << data.signal_map;
//  TLOG_DEBUG(TLVL_DEBUG_MEDIUM) << "[CIB] Converting HSI event, signal: " << data.signal_map;

  std::vector<triggeralgs::TriggerCandidate> candidates;

  std::bitset<32> bits(data.signal_map);
  TLOG_DEBUG(TLVL_DEBUG_HIGH) << "[CIB] BITS: " << bits;

  for (size_t i = 0; i < bits.size(); ++i)
  {
    if (bits.test(i))
    {

      TLOG_DEBUG(TLVL_DEBUG_ALL) << "[CIB] this bit: " << i;

      if (m_CIB_TC_map.count(i))
      {
        TLOG_DEBUG(TLVL_DEBUG_ALL) << "[CIB] TC type: " << static_cast<int>(m_CIB_TC_map[i]);

        triggeralgs::TriggerCandidate candidate;
        candidate.time_candidate = data.timestamp;
        candidate.time_start = data.timestamp - m_time_before;
        candidate.time_end = data.timestamp + m_time_after;
        // candidate.detid = 1;
        candidate.detid = data.header;
        candidate.type = m_CIB_TC_map[i];
        candidate.algorithm = triggeralgs::TriggerCandidate::Algorithm::kHSIEventToTriggerCandidate;
        candidate.inputs = {};

        candidates.push_back(candidate);
      }
      else
      {
        ers::error(
          dunedaq::trigger::InvalidCIBSignal(ERS_HERE, get_name(), data.signal_map, bits, m_CIB_TC_map.size()));
      }
    }
  }

  return candidates;
}

void
CIBTriggerCandidateMaker::do_conf(const nlohmann::json& config)
{
  auto params = config.get<dunedaq::trigger::cibtriggercandidatemaker::Conf>();
  m_time_before = params.time_before;
  m_time_after = params.time_after;
  m_prescale = params.prescale;
  m_prescale_flag = (m_prescale > 1) ? true : false;
//  TLOG_DEBUG(TLVL_GENERAL) << "[CIB] " << get_name() + " configured.";
//  TLOG_DEBUG(TLVL_VERY_IMPORTANT) << "[CIB] Time before: " << m_time_before;
//  TLOG_DEBUG(TLVL_VERY_IMPORTANT) << "[CIB] Time after: " << m_time_after;
  TLOG() << "[CIB] " << get_name() + " configured.";
  TLOG() << "[CIB] Time before: " << m_time_before;
  TLOG() << "[CIB] Time after: " << m_time_after;

  if (m_prescale_flag)
  {
//    TLOG_DEBUG(TLVL_VERY_IMPORTANT) << "[CIB] Running with prescale at: " << m_prescale;
    TLOG() << "[CIB] Running with prescale at: " << m_prescale;
  }
}

void
CIBTriggerCandidateMaker::init(const nlohmann::json& iniobj)
{
  TLOG() << get_name() << "Received iniobj \n "
      << iniobj.dump() ;
  try
  {
    auto ci = appfwk::connection_index(iniobj, { "output", "hsi_input" });
    m_output_queue = get_iom_sender<triggeralgs::TriggerCandidate>(ci["output"]);
    m_hsievent_input = get_iom_receiver<dfmessages::HSIEvent>(ci["hsi_input"]);
  }
  catch (const ers::Issue& excpt)
  {
    throw dunedaq::trigger::InvalidQueueFatalError(ERS_HERE, get_name(), "input/output", excpt);
  }
}

void
CIBTriggerCandidateMaker::do_start(const nlohmann::json& startobj)
{
  // OpMon.
  m_tsd_received_count.store(0);
  m_tc_sent_count.store(0);
  m_tc_sig_type_err_count.store(0);
  m_tc_total_count.store(0);

  auto start_params = startobj.get<rcif::cmd::StartParams>();
  m_run_number.store(start_params.run);

  m_hsievent_input->add_callback(std::bind(&CIBTriggerCandidateMaker::receive_hsievent, this, std::placeholders::_1));

  TLOG_DEBUG(TLVL_GENERAL) << "[CIB] " << get_name() + " successfully started.";
}

void
CIBTriggerCandidateMaker::do_stop(const nlohmann::json&)
{
  m_hsievent_input->remove_callback();

  TLOG() << "[CIB] Received " << m_tsd_received_count
         << " HSIEvent messages. Successfully sent " << m_tc_sent_count
         << " TriggerCandidates";
//  TLOG() << "[CIB] "
  TLOG_DEBUG(TLVL_GENERAL) << "[CIB] " << get_name() + " successfully stopped.";
}

void
CIBTriggerCandidateMaker::receive_hsievent(dfmessages::HSIEvent& data)
{
//  TLOG_DEBUG(TLVL_DEBUG_MEDIUM) << "[CIB] Activity received with timestamp " << data.timestamp << ", sequence_counter "
//                                << data.sequence_counter << ", and run_number " << data.run_number;

  TLOG() << "[CIB] Activity received with timestamp " << data.timestamp << ", sequence_counter "
                                << data.sequence_counter << ", and run_number " << data.run_number;

  if (data.run_number != m_run_number)
  {
    ers::error(dunedaq::trigger::InvalidHSIEventRunNumber(
      ERS_HERE, get_name(), data.run_number, m_run_number, data.timestamp, data.sequence_counter));
    return;
  }

  ++m_tsd_received_count;

  if (m_prescale_flag)
  {
    if (m_tsd_received_count % m_prescale != 0)
    {
      TLOG() << get_name() << "[CIB] : Prescaling received HSI";
      return;
    }
  }

  std::vector<triggeralgs::TriggerCandidate> candidates;
  try
  {
    candidates = HSIEventToTriggerCandidate(data);
  }
  catch (SignalTypeError& e)
  {
    m_tc_sig_type_err_count++;
    ers::error(e);
    return;
  }

  for (const auto& candidate : candidates)
  {
    bool successfullyWasSent = false;
    while (!successfullyWasSent)
    {
      try
      {
        triggeralgs::TriggerCandidate candidate_copy(candidate);
        TLOG() << get_name() << "[CIB] : Sending a candidate";
        m_output_queue->send(std::move(candidate_copy), m_queue_timeout);
        successfullyWasSent = true;
        ++m_tc_sent_count;
      }
      catch (const dunedaq::iomanager::TimeoutExpired& excpt)
      {
        std::ostringstream oss_warn;
        oss_warn << "push to output queue \"" << m_output_queue->get_name() << "\"";
        ers::warning(dunedaq::iomanager::TimeoutExpired(ERS_HERE, get_name(), oss_warn.str(), m_queue_timeout.count()));
      }
    }
    m_tc_total_count++;
  }
}

void
CIBTriggerCandidateMaker::do_scrap(const nlohmann::json&)
{
}

void
CIBTriggerCandidateMaker::get_info(opmonlib::InfoCollector& ci, int /*level*/)
{
  cibtriggercandidatemakerinfo::Info i;

  i.tsd_received_count = m_tsd_received_count.load();
  i.tc_sent_count = m_tc_sent_count.load();
  i.tc_sig_type_err_count = m_tc_sig_type_err_count.load();
  i.tc_total_count = m_tc_total_count.load();

  ci.add(i);
}

} // namespace trigger
} // namespace dunedaq

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::CIBTriggerCandidateMaker)
