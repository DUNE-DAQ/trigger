/**
 * @file HSISourceModel.hpp
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef TRIGGER_PLUGINS_HSISOURCEMODEL_HPP_
#define TRIGGER_PLUGINS_HSISOURCEMODEL_HPP_

#include <functional>
#include "readoutlibs/concepts/SourceConcept.hpp"
#include "detdataformats/DetID.hpp"
#include "dfmessages/HSIEvent.hpp"
#include "triggeralgs/TriggerCandidate.hpp"
#include "trigger/Issues.hpp"

#include "iomanager/IOManager.hpp"
#include "iomanager/Sender.hpp"
#include "iomanager/Receiver.hpp"
#include "logging/Logging.hpp"
#include "confmodel/DaqModule.hpp"
#include "appmodel/DataSubscriber.hpp"
#include "appmodel/HSI2TCTranslatorConf.hpp" 
#include "appmodel/HSISignalWindow.hpp" 

namespace dunedaq::trigger {


class HSISourceModel : public readoutlibs::SourceConcept
{
public: 
  using inherited = readoutlibs::SourceConcept;

  /**
   * @brief SourceModel Constructor
   * @param name Instance name for this SourceModel instance
   */
  
  HSISourceModel(): readoutlibs::SourceConcept() {}
  ~HSISourceModel() {}

  void init(const confmodel::DaqModule* cfg) override {
    if (cfg->get_outputs().size() != 1) {
      throw readoutlibs::InitializationError(ERS_HERE, "Only 1 output supported for subscribers");
    }
    m_data_sender = get_iom_sender<triggeralgs::TriggerCandidate>(cfg->get_outputs()[0]->UID());

    if (cfg->get_inputs().size() != 1) {
      throw readoutlibs::InitializationError(ERS_HERE, "Only 1 input supported for subscribers");
    }
    m_data_receiver = get_iom_receiver<dfmessages::HSIEvent>(cfg->get_inputs()[0]->UID());
    auto data_reader = cfg->cast<appmodel::DataSubscriber>();
    if (data_reader == nullptr) {
       throw readoutlibs::InitializationError(ERS_HERE, "DAQ module is not a DataReader");
    }
    auto hsi_conf = data_reader->get_configuration()->cast<appmodel::HSI2TCTranslatorConf>();
    if (hsi_conf == nullptr) {
	throw readoutlibs::InitializationError(ERS_HERE, "Missing HSI2TCTranslatorConf");
    }
    for (auto win : hsi_conf->get_signals()) {
	   m_signals[win->get_signal_type()] = std::pair<uint32_t, uint32_t>(win->get_time_before(), win->get_time_after());
    }
  }

  void start() {
    m_data_receiver->add_callback(std::bind(&HSISourceModel::handle_payload, this, std::placeholders::_1));
  }  

  void stop() {
    m_data_receiver->remove_callback();
  }

  void get_info(opmonlib::InfoCollector& /*ci*/, int /*level*/) {}

  bool handle_payload(dfmessages::HSIEvent& data) // NOLINT(build/unsigned)
  {
    TLOG_DEBUG(1) << "Received HSIEvent with signal map " << data.signal_map << " and timestamp " << data.timestamp; 
    triggeralgs::TriggerCandidate candidate;
    auto signal_info = m_signals.find(data.signal_map);
    if (signal_info != m_signals.end()) {
        // clang-format off
        candidate.time_start = data.timestamp - signal_info->second.first;  // time_start
        candidate.time_end   = data.timestamp + signal_info->second.second; // time_end,
        // clang-format on    
    } else {
        throw dunedaq::trigger::SignalTypeError(ERS_HERE, "HSI subscriber" , data.signal_map);
    }
    
    candidate.time_candidate = data.timestamp;
    // throw away bits 31-16 of header, that's OK for now
    candidate.detid = (uint)detdataformats::DetID::Subdetector::kDAQ ; // NOLINT(build/unsigned)
    candidate.type = triggeralgs::TriggerCandidate::Type::kTiming;

    candidate.algorithm = triggeralgs::TriggerCandidate::Algorithm::kHSIEventToTriggerCandidate;
    candidate.inputs = {};

    if (!m_data_sender->try_send(std::move(candidate), iomanager::Sender::s_no_block)) {
      ++m_dropped_packets;
    }
    
    return true;
  }

private:
  using source_t = dunedaq::iomanager::ReceiverConcept<dfmessages::HSIEvent>;
  std::shared_ptr<source_t> m_data_receiver;

  using sink_t = dunedaq::iomanager::SenderConcept<triggeralgs::TriggerCandidate>;
  std::shared_ptr<sink_t> m_data_sender;

  std::map<uint32_t, std::pair<uint32_t,uint32_t>> m_signals;

  //Stats
  std::atomic<uint64_t> m_dropped_packets{0};
};

} // namespace dunedaq::trigger

#endif // TRIGGER_PLUGINS_HSISOURCEMODEL_HPP_
