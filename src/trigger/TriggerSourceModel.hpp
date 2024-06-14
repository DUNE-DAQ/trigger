/**
 * @file TriggerSourceModel.hpp
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef TRIGGER_SRC_TRIGGER_TRIGGERSOURCEMODEL_HPP_
#define TRIGGER_SRC_TRIGGER_TRIGGERSOURCEMODEL_HPP_

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
#include "confmodel/DaqModule.hpp"
#include "appmodel/DataSubscriberModule.hpp"
#include "trigger/TAWrapper.hpp"
#include "trigger/TCWrapper.hpp"

//#include "appmodel/HSI2TCTranslatorConf.hpp" 
//#include "appmodel/HSISignalWindow.hpp" 

namespace dunedaq::trigger {


template<class TriggerXObject, class TXWrapper>
class TriggerSourceModel : public readoutlibs::SourceConcept
{
public: 
  using inherited = readoutlibs::SourceConcept;

  /**
   * @brief SourceModel Constructor
   * @param name Instance name for this SourceModel instance
   */
  
  TriggerSourceModel(): readoutlibs::SourceConcept() {}
  ~TriggerSourceModel() {}

  void init(const confmodel::DaqModule* cfg) override {
    if (cfg->get_outputs().size() != 1) {
      throw readoutlibs::InitializationError(ERS_HERE, "Only 1 output supported for subscribers");
    }
    m_data_sender = get_iom_sender<TXWrapper>(cfg->get_outputs()[0]->UID());

    if (cfg->get_inputs().size() != 1) {
      throw readoutlibs::InitializationError(ERS_HERE, "Only 1 input supported for subscribers");
    }
    m_data_receiver = get_iom_receiver<TriggerXObject>(cfg->get_inputs()[0]->UID());
/*
    auto data_reader = cfg->cast<appmodel::DataSubscriberModule>();
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
    */
  }

  void start() {
    m_data_receiver->add_callback(std::bind(&TriggerSourceModel::handle_payload, this, std::placeholders::_1));
  }  

  void stop() {
    m_data_receiver->remove_callback();
  }

  void get_info(opmonlib::InfoCollector& /*ci*/, int /*level*/) {}

  bool handle_payload(TriggerXObject& data) // NOLINT(build/unsigned)
  {
    TXWrapper tx(data);
    if (!m_data_sender->try_send(std::move(tx), iomanager::Sender::s_no_block)) {
      ++m_dropped_packets;
    }
    return true;
  }

private:
  using source_t = dunedaq::iomanager::ReceiverConcept<TriggerXObject>;
  std::shared_ptr<source_t> m_data_receiver;

  using sink_t = dunedaq::iomanager::SenderConcept<TXWrapper>;
  std::shared_ptr<sink_t> m_data_sender;

  //Stats
  std::atomic<uint64_t> m_dropped_packets{0};
};

} // namespace dunedaq::trigger

#endif // TRIGGER_SRC_TRIGGER_TRIGGERSOURCEMODEL_HPP_
