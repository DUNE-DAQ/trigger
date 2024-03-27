/**
 * @file TimingTriggerCandidateMaker.cpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "TimingTriggerCandidateMaker.hpp"
#include "trigger/Logging.hpp"

#include "appfwk/DAQModuleHelper.hpp"
#include "trgdataformats/Types.hpp"
#include "trigger/TriggerCandidate_serialization.hpp"
#include "iomanager/IOManager.hpp"
#include "rcif/cmd/Nljs.hpp"

#include <regex>
#include <string>
#include <limits>

using dunedaq::trigger::logging::TLVL_VERY_IMPORTANT;
using dunedaq::trigger::logging::TLVL_GENERAL;
using dunedaq::trigger::logging::TLVL_DEBUG_MEDIUM;

namespace dunedaq {
namespace trigger {

TimingTriggerCandidateMaker::TimingTriggerCandidateMaker(const std::string& name)
  : DAQModule(name)
  , m_output_queue(nullptr)
  , m_queue_timeout(100)
{

  register_command("conf", &TimingTriggerCandidateMaker::do_conf);
  register_command("start", &TimingTriggerCandidateMaker::do_start);
  register_command("stop", &TimingTriggerCandidateMaker::do_stop);
  register_command("scrap", &TimingTriggerCandidateMaker::do_scrap);
}

template<typename T> 
std::vector<int> 
TimingTriggerCandidateMaker::GetTriggeredBits(T signal_map)
{
  std::vector<int> triggered_bits;
  int nbits = std::numeric_limits<T>::digits;
  for (int i = 0; i < nbits; ++i) {
    if ((signal_map >> i) & 1 ) {
      triggered_bits.push_back(i);
    }
  }
  return triggered_bits;
}

std::vector<triggeralgs::TriggerCandidate>
TimingTriggerCandidateMaker::HSIEventToTriggerCandidate(const dfmessages::HSIEvent& data)
{
  std::vector<triggeralgs::TriggerCandidate> candidates;
  // TODO Trigger Team <dune-daq@github.com> Nov-18-2021: the signal field ia now a signal bit map, rather than unique
  // value -> change logic of below?

  std::vector<int> triggered_bits = GetTriggeredBits(data.signal_map);
  for (int bit_index : triggered_bits) {
    uint32_t signal = 1 << bit_index;

    if (!m_hsisignal_map.count(signal)) {
      throw dunedaq::trigger::SignalTypeError(ERS_HERE, get_name(), data.signal_map);
    }

    triggeralgs::TriggerCandidate candidate;
    candidate.time_start = data.timestamp - m_hsisignal_map[data.signal_map].time_before;  // time_start
    candidate.time_end   = data.timestamp + m_hsisignal_map[data.signal_map].time_after; // time_end,
    candidate.time_candidate = data.timestamp;
    // throw away bits 31-16 of header, that's OK for now
    candidate.detid = data.header; // NOLINT(build/unsigned)

    candidate.type = m_hsisignal_map[signal].type; // type,

    candidate.algorithm = triggeralgs::TriggerCandidate::Algorithm::kHSIEventToTriggerCandidate;
    candidate.inputs = {};
    candidates.push_back(candidate);
  }

  return candidates;
}


void
TimingTriggerCandidateMaker::do_conf(const nlohmann::json& config)
{
  auto params = config.get<dunedaq::trigger::timingtriggercandidatemaker::Conf>();

  // Fill the internal hsi signal map. The map contains signal ID, TC type to
  // create, and the readout time window.
  for (auto hsi_input : params.hsi_configs) {
    triggeralgs::TriggerCandidate::Type type;
    type = static_cast<triggeralgs::TriggerCandidate::Type>(
        dunedaq::trgdataformats::string_to_fragment_type_value(hsi_input.tc_type_name));
    if (type == triggeralgs::TriggerCandidate::Type::kUnknown) {
      throw TTCMConfigurationProblem(ERS_HERE, get_name(),
          "Unknown TriggerCandidate supplied to TTCM HSI map");
    }

    if (m_hsisignal_map.count(hsi_input.signal)) {
      throw TTCMConfigurationProblem(ERS_HERE, get_name(),
          "Supplied more than one of the same hsi signal ID to TTCM HSI map");
    }

    m_hsisignal_map[hsi_input.signal] = { type,
                                          hsi_input.time_before,
                                          hsi_input.time_after };

    TLOG() << "[TTCM] will convert HSI signal id: " << hsi_input.signal << " to TC type: " << hsi_input.tc_type_name;
  }

  if (m_hsisignal_map.empty()) {
      throw TTCMConfigurationProblem(ERS_HERE, get_name(),
          "Created TTCM, but supplied an empty signal map!");
  }

  auto first_entry = *std::begin(m_hsisignal_map);
  m_prescale = params.prescale;
  m_prescale_flag = (m_prescale > 1) ? true : false;
  TLOG_DEBUG(TLVL_GENERAL) << "[TTCM] " << get_name() + " configured.";
  if (m_prescale_flag){
    TLOG_DEBUG(TLVL_VERY_IMPORTANT) << "[TTCM] Running with prescale at: " << m_prescale;
  }
}

void
TimingTriggerCandidateMaker::init(const nlohmann::json& iniobj)
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
TimingTriggerCandidateMaker::do_start(const nlohmann::json& startobj)
{
  // OpMon.
  m_tsd_received_count.store(0);
  m_tc_sent_count.store(0);
  m_tc_sig_type_err_count.store(0);
  m_tc_total_count.store(0);

  auto start_params = startobj.get<rcif::cmd::StartParams>();
  m_run_number.store(start_params.run);

  m_hsievent_input->add_callback(std::bind(&TimingTriggerCandidateMaker::receive_hsievent, this, std::placeholders::_1));
  
  TLOG_DEBUG(TLVL_GENERAL) << "[TTCM] " << get_name() + " successfully started.";
}

void
TimingTriggerCandidateMaker::do_stop(const nlohmann::json&)
{
  m_hsievent_input->remove_callback();

  TLOG() << "[TTCM] Received " << m_tsd_received_count << " HSIEvent messages. Successfully sent " << m_tc_sent_count
         << " TriggerCandidates";
  TLOG_DEBUG(TLVL_GENERAL) << "[TTCM] " << get_name() + " successfully stopped.";
}

void
TimingTriggerCandidateMaker::receive_hsievent(dfmessages::HSIEvent& data)
{
  TLOG_DEBUG(TLVL_DEBUG_MEDIUM) << "[TTCM] Activity received with timestamp " << data.timestamp << ", sequence_counter " << data.sequence_counter
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

  // Retreive all the potential TCs from HSI signal
  std::vector<triggeralgs::TriggerCandidate> candidates;
  try {
    candidates = HSIEventToTriggerCandidate(data);
  } catch (SignalTypeError& e) {
    m_tc_sig_type_err_count++;
    ers::error(e);
    return;
  }

  // Send each TC to the output queue separately
  for(const triggeralgs::TriggerCandidate& candidate: candidates){
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
}

void
TimingTriggerCandidateMaker::do_scrap(const nlohmann::json&)
{}

void
TimingTriggerCandidateMaker::get_info(opmonlib::InfoCollector& ci, int /*level*/)
{
  timingtriggercandidatemakerinfo::Info i;

  i.tsd_received_count = m_tsd_received_count.load();
  i.tc_sent_count = m_tc_sent_count.load();
  i.tc_sig_type_err_count = m_tc_sig_type_err_count.load();
  i.tc_total_count = m_tc_total_count.load();

  ci.add(i);
}

} // namespace trigger
} // namespace dunedaq

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::TimingTriggerCandidateMaker)
