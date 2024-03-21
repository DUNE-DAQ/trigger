/**
 * @file CTBTriggerCandidateMaker.cpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_PLUGINS_CTBTRIGGERCANDIDATEMAKER_HPP_
#define TRIGGER_PLUGINS_CTBTRIGGERCANDIDATEMAKER_HPP_

#include "trigger/Issues.hpp"
#include "trigger/ctbtriggercandidatemaker/Nljs.hpp"
#include "trigger/ctbtriggercandidatemakerinfo/InfoNljs.hpp"

#include "appfwk/DAQModule.hpp"
#include "daqdataformats/Types.hpp"
#include "dfmessages/HSIEvent.hpp"
#include "iomanager/Receiver.hpp"
#include "iomanager/Sender.hpp"
#include "triggeralgs/TriggerActivity.hpp"
#include "triggeralgs/TriggerCandidate.hpp"
#include "utilities/WorkerThread.hpp"

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace dunedaq {
namespace trigger {
class CTBTriggerCandidateMaker : public dunedaq::appfwk::DAQModule
{
public:
  explicit CTBTriggerCandidateMaker(const std::string& name);

  CTBTriggerCandidateMaker(const CTBTriggerCandidateMaker&) = delete;
  CTBTriggerCandidateMaker& operator=(const CTBTriggerCandidateMaker&) = delete;
  CTBTriggerCandidateMaker(CTBTriggerCandidateMaker&&) = delete;
  CTBTriggerCandidateMaker& operator=(CTBTriggerCandidateMaker&&) = delete;

  void init(const nlohmann::json& iniobj) override;
  void get_info(opmonlib::InfoCollector& ci, int level) override;

private:
  void do_conf(const nlohmann::json& config);
  void do_start(const nlohmann::json& obj);
  void do_stop(const nlohmann::json& obj);
  void do_scrap(const nlohmann::json& obj);

  std::string m_hsievent_receive_connection;

  // Prescale functionality
  bool m_prescale_flag;
  int m_prescale;

  // HSI Passthrough changes
  std::atomic<bool> m_hsi_passthrough;
  int m_hsi_pt_before;
  int m_hsi_pt_after;

  triggeralgs::TriggerCandidate HSIEventToTriggerCandidate(const dfmessages::HSIEvent& data);
  void receive_hsievent(dfmessages::HSIEvent& data);

  using sink_t = dunedaq::iomanager::SenderConcept<triggeralgs::TriggerCandidate>;
  std::shared_ptr<sink_t> m_output_queue;
  std::shared_ptr<iomanager::ReceiverConcept<dfmessages::HSIEvent>> m_hsievent_input;

  std::chrono::milliseconds m_queue_timeout;

  // NOLINTNEXTLINE(build/unsigned)
  std::map<uint32_t, std::pair<triggeralgs::timestamp_t, triggeralgs::timestamp_t>> m_detid_offsets_map;

  // Opmon variables
  using metric_counter_type = decltype(ctbtriggercandidatemakerinfo::Info::tsd_received_count);
  std::atomic<metric_counter_type> m_tsd_received_count{ 0 };
  std::atomic<metric_counter_type> m_tc_sent_count{ 0 };
  std::atomic<metric_counter_type> m_tc_sig_type_err_count{ 0 };
  std::atomic<metric_counter_type> m_tc_total_count{ 0 };

  std::atomic<daqdataformats::run_number_t> m_run_number;
};
} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_PLUGINS_CTBTRIGGERCANDIDATEMAKER_HPP_
