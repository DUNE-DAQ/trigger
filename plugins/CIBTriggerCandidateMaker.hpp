/**
 * @file CIBTriggerCandidateMaker.cpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_PLUGINS_CIBTRIGGERCANDIDATEMAKER_HPP_
#define TRIGGER_PLUGINS_CIBTRIGGERCANDIDATEMAKER_HPP_

#include "trigger/Issues.hpp"
#include "trigger/cibtriggercandidatemaker/Nljs.hpp"
#include "trigger/cibtriggercandidatemakerinfo/InfoNljs.hpp"

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
class CIBTriggerCandidateMaker : public dunedaq::appfwk::DAQModule
{
public:
  explicit CIBTriggerCandidateMaker(const std::string& name);

  CIBTriggerCandidateMaker(const CIBTriggerCandidateMaker&) = delete;
  CIBTriggerCandidateMaker& operator=(const CIBTriggerCandidateMaker&) = delete;
  CIBTriggerCandidateMaker(CIBTriggerCandidateMaker&&) = delete;
  CIBTriggerCandidateMaker& operator=(CIBTriggerCandidateMaker&&) = delete;

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

  // Config
  int m_time_before;
  int m_time_after;

  std::vector<triggeralgs::TriggerCandidate> HSIEventToTriggerCandidate(const dfmessages::HSIEvent& data);
  void receive_hsievent(dfmessages::HSIEvent& data);

  using sink_t = dunedaq::iomanager::SenderConcept<triggeralgs::TriggerCandidate>;
  std::shared_ptr<sink_t> m_output_queue;
  std::shared_ptr<iomanager::ReceiverConcept<dfmessages::HSIEvent>> m_hsievent_input;

  std::chrono::milliseconds m_queue_timeout;

  // HLT to TC type map
  std::map<int, trgdataformats::TriggerCandidateData::Type> m_CIB_TC_map = {
    { 0, trgdataformats::TriggerCandidateData::Type::kCIBFakeTrigger },
    { 1, trgdataformats::TriggerCandidateData::Type::kCIBLaserTriggerP1 },
    { 2, trgdataformats::TriggerCandidateData::Type::kCIBLaserTriggerP2 },
    { 3, trgdataformats::TriggerCandidateData::Type::kCIBLaserTriggerP3 },
  };

  // Opmon variables
  using metric_counter_type = decltype(cibtriggercandidatemakerinfo::Info::tsd_received_count);
  std::atomic<metric_counter_type> m_tsd_received_count{ 0 };
  std::atomic<metric_counter_type> m_tc_sent_count{ 0 };
  std::atomic<metric_counter_type> m_tc_sig_type_err_count{ 0 };
  std::atomic<metric_counter_type> m_tc_total_count{ 0 };

  std::atomic<daqdataformats::run_number_t> m_run_number;
};
} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_PLUGINS_CIBTRIGGERCANDIDATEMAKER_HPP_
