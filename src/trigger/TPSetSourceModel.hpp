/**
 * @file TPSetSourceModel.hpp
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef TRIGGER_SRC_TRIGGER_TPSETSOURCEMODEL_HPP_
#define TRIGGER_SRC_TRIGGER_TPSETSOURCEMODEL_HPP_

#include <functional>
#include "readoutlibs/concepts/SourceConcept.hpp"
#include "detdataformats/DetID.hpp"
#include "dfmessages/HSIEvent.hpp"
#include "triggeralgs/TriggerCandidate.hpp"
#include "trigger/TCWrapper.hpp"


#include "iomanager/IOManager.hpp"
#include "iomanager/Sender.hpp"
#include "iomanager/Receiver.hpp"
#include "logging/Logging.hpp"
#include "coredal/DaqModule.hpp"
#include "appdal/DataSubscriber.hpp"
#include "trigger/TPSet.hpp"
#include "trigger/TriggerPrimitiveTypeAdapter.hpp"

//#include "appdal/HSI2TCTranslatorConf.hpp" 
//#include "appdal/HSISignalWindow.hpp" 

namespace dunedaq::trigger {


class TPSetSourceModel : public readoutlibs::SourceConcept
{
public: 
  using inherited = readoutlibs::SourceConcept;

  /**
   * @brief SourceModel Constructor
   * @param name Instance name for this SourceModel instance
   */
  
  TPSetSourceModel(): readoutlibs::SourceConcept() {}
  ~TPSetSourceModel() {}

  void init(const coredal::DaqModule* cfg) override {
    if (cfg->get_outputs().size() != 1) {
      throw readoutlibs::InitializationError(ERS_HERE, "Only 1 output supported for subscribers");
    }
    m_data_sender = get_iom_sender<trigger::TriggerPrimitiveTypeAdapter>(cfg->get_outputs()[0]->UID());

    if (cfg->get_inputs().size() != 1) {
      throw readoutlibs::InitializationError(ERS_HERE, "Only 1 input supported for subscribers");
    }
    m_data_receiver = get_iom_receiver<trigger::TPSet>(cfg->get_inputs()[0]->UID());
/*
    auto data_reader = cfg->cast<appdal::DataSubscriber>();
    if (data_reader == nullptr) {
       throw readoutlibs::InitializationError(ERS_HERE, "DAQ module is not a DataReader");
    }
    auto hsi_conf = data_reader->get_configuration()->cast<appdal::HSI2TCTranslatorConf>();
    if (hsi_conf == nullptr) {
	throw readoutlibs::InitializationError(ERS_HERE, "Missing HSI2TCTranslatorConf");
    }
    for (auto win : hsi_conf->get_signals()) {
	   m_signals[win->get_signal_type()] = std::pair<uint32_t, uint32_t>(win->get_time_before(), win->get_time_after());
    }
    */
  }

  void start() {
    m_data_receiver->add_callback(std::bind(&TPSetSourceModel::handle_payload, this, std::placeholders::_1));
  }  

  void stop() {
    m_data_receiver->remove_callback();
  }

  void get_info(opmonlib::InfoCollector& /*ci*/, int /*level*/) {}

  bool handle_payload(trigger::TPSet& data) // NOLINT(build/unsigned)
  {
   for (auto tpraw : data.objects) {
    TriggerPrimitiveTypeAdapter tp;
    tp.tp = tpraw;    
    if (!m_data_sender->try_send(std::move(tp), iomanager::Sender::s_no_block)) {
      ++m_dropped_packets;
    }
   }
    return true;
  }

private:
  using source_t = dunedaq::iomanager::ReceiverConcept<trigger::TPSet>;
  std::shared_ptr<source_t> m_data_receiver;

  using sink_t = dunedaq::iomanager::SenderConcept<trigger::TriggerPrimitiveTypeAdapter>;
  std::shared_ptr<sink_t> m_data_sender;

  //Stats
  std::atomic<uint64_t> m_dropped_packets{0};
};

} // namespace dunedaq::trigger

#endif // TRIGGER_SRC_TRIGGER_TPSETSOURCEMODEL_HPP_
