/**
 * @file TAProcessor.hpp TA specific Task based raw processor
 *
 * This is part of the DUNE DAQ , copyright 2022.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef TRIGGER_SRC_TRIGGER_TAPROCESSOR_HPP_
#define TRIGGER_SRC_TRIGGER_TAPROCESSOR_HPP_

#include "iomanager/IOManager.hpp"
#include "iomanager/Sender.hpp"
#include "logging/Logging.hpp"

#include "appmodel/DataHandlerModule.hpp"

#include "datahandlinglibs/models/TaskRawDataProcessorModel.hpp"

#include "trigger/Issues.hpp"
#include "trigger/TAWrapper.hpp"
#include "trigger/Latency.hpp"
#include "trigger/opmon/taprocessor_info.pb.h"
#include "trigger/opmon/latency_info.pb.h"

#include "triggeralgs/TriggerCandidate.hpp"
#include "triggeralgs/Types.hpp"
#include "triggeralgs/TriggerCandidateMaker.hpp"

namespace dunedaq {
namespace trigger {

class TAProcessor : public datahandlinglibs::TaskRawDataProcessorModel<TAWrapper>
{

public:
  using inherited = datahandlinglibs::TaskRawDataProcessorModel<TAWrapper>;
  using taptr = TAWrapper*;
  using consttaptr = const TAWrapper*;


  explicit TAProcessor(std::unique_ptr<datahandlinglibs::FrameErrorRegistry>& error_registry, bool post_processing_enabled);

  ~TAProcessor();

  void start(const nlohmann::json& args) override;

  void stop(const nlohmann::json& args) override;

  void conf(const appmodel::DataHandlerModule* conf) override;

  void generate_opmon_data() override;

protected:
  // Internals
  dunedaq::daqdataformats::timestamp_t m_previous_ts = 0;
  dunedaq::daqdataformats::timestamp_t m_current_ts = 0;

  /**
   * Pipeline Stage 2.: Do TA finding
   * */

  void find_tc(const TAWrapper* ta, std::shared_ptr<triggeralgs::TriggerCandidateMaker> tcm);

  private:

  std::vector<std::shared_ptr<triggeralgs::TriggerCandidateMaker>> m_tcms;

  std::shared_ptr<iomanager::SenderConcept<triggeralgs::TriggerCandidate>> m_tc_sink;

  daqdataformats::SourceID m_sourceid;

  using metric_counter_type = uint64_t;
  std::atomic<metric_counter_type> m_ta_received_count{ 0 };  // NOLINT(build/unsigned)
  std::atomic<metric_counter_type> m_tc_made_count{ 0 };
  std::atomic<metric_counter_type> m_tc_sent_count{ 0 };
  std::atomic<metric_counter_type> m_tc_failed_sent_count{ 0 };
  void print_opmon_stats();

  // Create an instance of the Latency class
  std::atomic<bool> m_running_flag{ false };
  std::atomic<bool> m_latency_monitoring{ false };
  dunedaq::trigger::Latency m_latency_instance;
  std::atomic<metric_counter_type> m_latency_in{ 0 };
  std::atomic<metric_counter_type> m_latency_out{ 0 };

};

} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_SRC_TRIGGER_TPPROCESSOR_HPP_
