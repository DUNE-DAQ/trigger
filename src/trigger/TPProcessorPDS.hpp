/**
 * @file TPProcessor.hpp TP specific Task based raw processor
 *
 * This is part of the DUNE DAQ , copyright 2022.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef TRIGGER_SRC_TRIGGER_TPPROCESSOR_HPP_
#define TRIGGER_SRC_TRIGGER_TPPROCESSOR_HPP_

#include "iomanager/IOManager.hpp"
#include "iomanager/Sender.hpp"
#include "logging/Logging.hpp"

#include "datahandlinglibs/models/TaskRawDataProcessorModel.hpp"

//#include "triggger/Issues.hpp"
#include "trigger/TriggerPrimitiveTypeAdapterPDS.hpp"
#include "trigger/Latency.hpp"
#include "trigger/opmon/tpprocessor_info.pb.h"
#include "trigger/opmon/latency_info.pb.h"

#include "triggeralgs/TriggerActivityPDS.hpp"
#include "triggeralgs/Types.hpp"
#include "triggeralgs/TriggerActivityMakerPDS.hpp"

#include "appmodel/DataHandlerModule.hpp"

namespace dunedaq {
namespace trigger {

class TPProcessorPDS : public datahandlinglibs::TaskRawDataProcessorModel<TriggerPrimitiveTypeAdapterPDS>
{

public:
  using inherited = datahandlinglibs::TaskRawDataProcessorModel<TriggerPrimitiveTypeAdapterPDS>;
  using tpptr = TriggerPrimitiveTypeAdapterPDS*;
  using consttpptr = const TriggerPrimitiveTypeAdapterPDS*;


  explicit TPProcessorPDS(std::unique_ptr<datahandlinglibs::FrameErrorRegistry>& error_registry, bool post_processing_enabled);

  ~TPProcessorPDS();

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

  void find_ta(const TriggerPrimitiveTypeAdapterPDS* tp,  std::shared_ptr<triggeralgs::TriggerActivityMakerPDS> tam);

  private:

  std::vector<std::shared_ptr<triggeralgs::TriggerActivityMakerPDS>> m_tams;

  std::shared_ptr<iomanager::SenderConcept<triggeralgs::TriggerActivityPDS>> m_ta_sink;

  daqdataformats::SourceID m_sourceid;

  using metric_counter_type = uint64_t;
  std::atomic<metric_counter_type> m_tp_received_count{ 0 };  // NOLINT(build/unsigned)
  std::atomic<metric_counter_type> m_ta_made_count{ 0 };
  std::atomic<metric_counter_type> m_ta_sent_count{ 0 };
  std::atomic<metric_counter_type> m_ta_failed_sent_count{ 0 };
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
