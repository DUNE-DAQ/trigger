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
#include "datahandlinglibs/concepts/SourceConcept.hpp"
#include "detdataformats/DetID.hpp"
#include "dfmessages/HSIEvent.hpp"
#include "triggeralgs/TriggerCandidate.hpp"
#include "trigger/Issues.hpp"

#include "iomanager/IOManager.hpp"
#include "iomanager/Sender.hpp"
#include "iomanager/Receiver.hpp"
#include "logging/Logging.hpp"
#include "confmodel/DaqModule.hpp"
#include "appmodel/DataSubscriberModule.hpp"
#include "appmodel/HSI2TCTranslatorConf.hpp" 
#include "appmodel/HSISignalWindow.hpp" 

namespace dunedaq::trigger {

/**
 * @struct HSISignal
 * @brief Struct holding configuration for one HSI signal bit
 */
struct HSISignal
{
  triggeralgs::TriggerCandidate::Type type;
  triggeralgs::timestamp_t time_before;
  triggeralgs::timestamp_t time_after;
};

class HSISourceModel : public datahandlinglibs::SourceConcept
{
public: 
  using inherited = datahandlinglibs::SourceConcept;

  /**
   * @brief SourceModel Constructor
   * @param name Instance name for this SourceModel instance
   */
  HSISourceModel(): datahandlinglibs::SourceConcept() {}
  ~HSISourceModel() {}

  void init(const confmodel::DaqModule* cfg) override
  {
    if (cfg->get_outputs().size() != 1) {
      throw datahandlinglibs::InitializationError(ERS_HERE, "Only 1 output supported for subscribers");
    }
    m_data_sender = get_iom_sender<triggeralgs::TriggerCandidate>(cfg->get_outputs()[0]->UID());

    if (cfg->get_inputs().size() != 1) {
      throw datahandlinglibs::InitializationError(ERS_HERE, "Only 1 input supported for subscribers");
    }
    m_data_receiver = get_iom_receiver<dfmessages::HSIEvent>(cfg->get_inputs()[0]->UID());
    auto data_reader = cfg->cast<appmodel::DataSubscriberModule>();
    if (data_reader == nullptr) {
      throw datahandlinglibs::InitializationError(ERS_HERE, "DAQ module is not a DataReader");
    }
    auto hsi_conf = data_reader->get_configuration()->cast<appmodel::HSI2TCTranslatorConf>();
    if (hsi_conf == nullptr) {
      throw datahandlinglibs::InitializationError(ERS_HERE, "Missing HSI2TCTranslatorConf");
    }

    // Get the HSI-signal to TC-output map
    for (auto win : hsi_conf->get_signals()) {
      triggeralgs::TriggerCandidate::Type tc_type;
      tc_type = static_cast<triggeralgs::TriggerCandidate::Type>(
          dunedaq::trgdataformats::string_to_fragment_type_value(win->get_tc_type_name()));

      // Throw error if unknown TC type
      if (tc_type == triggeralgs::TriggerCandidate::Type::kUnknown) {
        throw datahandlinglibs::InitializationError(ERS_HERE, "Provided an unknown TC type output to HSISourceModel");
      }

      // Throw error if already exists
      uint32_t signal = win->get_signal_type();
      if (m_signals.count(signal)) {
        throw datahandlinglibs::InitializationError(ERS_HERE, "Provided more than one of the same HSI signal ID input to HSISourceModel");
      }

      // Fill the signal-tctype map
      m_signals[signal] = { tc_type,
                            win->get_time_before(),
                            win->get_time_after() };

      TLOG() << "Will cover HSI signal id: " << signal << " to TC type: " << win->get_tc_type_name() 
             << " window before: " << win->get_time_before() << " window after: " << win->get_time_after();
    }

    m_prescale = hsi_conf->get_prescale();
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
    ++m_hsievents_received;

    // Prescale after n-hsi received
    if (m_hsievents_received % m_prescale != 0) {
      return true;
    }

    TLOG_DEBUG(1) << "Received HSIEvent with signal map " << data.signal_map << " and timestamp " << data.timestamp;

    // Iterate over all the signals
    uint32_t signal_map = data.signal_map;
    while (signal_map) {
      // Get the index of the least significant bit
      int bit_index = __builtin_ctzll(signal_map);
      uint32_t signal = 1 << bit_index;

      // Throw an error if we don't have this signal bit configured
      if (!m_signals.count(signal)) {
        throw dunedaq::trigger::SignalTypeError(ERS_HERE, "HSI subscriber" , data.signal_map);
      }

      // Create the trigger candidate
      triggeralgs::TriggerCandidate candidate;
      candidate.time_start = data.timestamp - m_signals[signal].time_before;
      candidate.time_end = data.timestamp + m_signals[signal].time_after;
      candidate.time_candidate = data.timestamp;
      // throw away bits 31-16 of header, that's OK for now
      candidate.detid = (uint)detdataformats::DetID::Subdetector::kDAQ ; // NOLINT(build/unsigned)
      candidate.type = m_signals[signal].type;
      candidate.algorithm = triggeralgs::TriggerCandidate::Algorithm::kHSIEventToTriggerCandidate;
      candidate.inputs = {};
      ++m_tc_made;

      // Send the TC
      if (!m_data_sender->try_send(std::move(candidate), iomanager::Sender::s_no_block)) {
        ++m_dropped_packets;
      }
      else {
        ++m_tc_sent;
      }

      // Clear the least significant bit
      signal_map &= signal_map - 1;
    }
    
    return true;
  }

private:
  using source_t = dunedaq::iomanager::ReceiverConcept<dfmessages::HSIEvent>;
  std::shared_ptr<source_t> m_data_receiver;

  using sink_t = dunedaq::iomanager::SenderConcept<triggeralgs::TriggerCandidate>;
  std::shared_ptr<sink_t> m_data_sender;

  /// @brief map of HSI signal ID bits to TC output configurations
  std::map<uint32_t, HSISignal> m_signals;

  //Stats
  std::atomic<uint64_t> m_dropped_packets{0};
  std::atomic<uint64_t> m_hsievents_received{0};
  std::atomic<uint64_t> m_tc_made{0};
  std::atomic<uint64_t> m_tc_sent{0};

  /// @brief {rescale for the input HSIEvents, default 1
  uint64_t m_prescale;
};

} // namespace dunedaq::trigger

#endif // TRIGGER_PLUGINS_HSISOURCEMODEL_HPP_
