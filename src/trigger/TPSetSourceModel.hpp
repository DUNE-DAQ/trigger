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
#include "datahandlinglibs/concepts/SourceConcept.hpp"
#include "detdataformats/DetID.hpp"
#include "dfmessages/HSIEvent.hpp"
#include "triggeralgs/TriggerCandidate.hpp"
#include "trigger/TCWrapper.hpp"


#include "iomanager/IOManager.hpp"
#include "iomanager/Sender.hpp"
#include "iomanager/Receiver.hpp"
#include "logging/Logging.hpp"
#include "confmodel/DaqModule.hpp"
#include "appmodel/DataSubscriberModule.hpp"
#include "trigger/TPSet.hpp"
#include "trigger/TriggerPrimitiveTypeAdapter.hpp"

//#include "appmodel/HSI2TCTranslatorConf.hpp" 
//#include "appmodel/HSISignalWindow.hpp" 

namespace dunedaq::trigger {


class TPSetSourceModel : public datahandlinglibs::SourceConcept
{
public: 
  using inherited = datahandlinglibs::SourceConcept;

  /**
   * @brief SourceModel Constructor
   * @param name Instance name for this SourceModel instance
   */
  
  TPSetSourceModel(): datahandlinglibs::SourceConcept() {}
  ~TPSetSourceModel() {}

  void init(const confmodel::DaqModule* cfg) override {
    if (cfg->get_outputs().size() != 1) {
      throw datahandlinglibs::InitializationError(ERS_HERE, "Only 1 output supported for subscribers");
    }
    m_data_sender = get_iom_sender<trigger::TriggerPrimitiveTypeAdapter>(cfg->get_outputs()[0]->UID());

    if (cfg->get_inputs().size() != 1) {
      throw datahandlinglibs::InitializationError(ERS_HERE, "Only 1 input supported for subscribers");
    }
    m_data_receiver = get_iom_receiver<trigger::TPSet>(cfg->get_inputs()[0]->UID());
/*
    auto data_reader = cfg->cast<appmodel::DataSubscriberModule>();
    if (data_reader == nullptr) {
       throw datahandlinglibs::InitializationError(ERS_HERE, "DAQ module is not a DataReader");
    }
    auto hsi_conf = data_reader->get_configuration()->cast<appmodel::HSI2TCTranslatorConf>();
    if (hsi_conf == nullptr) {
	throw datahandlinglibs::InitializationError(ERS_HERE, "Missing HSI2TCTranslatorConf");
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
