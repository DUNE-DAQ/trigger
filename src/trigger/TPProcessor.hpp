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
#include "trigger/TriggerPrimitiveTypeAdapter.hpp"
#include "trigger/opmon/tpprocessor_info.pb.h"
#include "triggeralgs/TriggerActivity.hpp"

#include "triggeralgs/Types.hpp"
#include "triggeralgs/TriggerActivityMaker.hpp"

#include "appmodel/DataHandlerModule.hpp"

namespace dunedaq {
namespace trigger {

class TPProcessor : public datahandlinglibs::TaskRawDataProcessorModel<TriggerPrimitiveTypeAdapter>
{

public:
  using inherited = datahandlinglibs::TaskRawDataProcessorModel<TriggerPrimitiveTypeAdapter>;
  using tpptr = TriggerPrimitiveTypeAdapter*;
  using consttpptr = const TriggerPrimitiveTypeAdapter*;


  explicit TPProcessor(std::unique_ptr<datahandlinglibs::FrameErrorRegistry>& error_registry, bool post_processing_enabled);

  ~TPProcessor();

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

  void find_ta(const TriggerPrimitiveTypeAdapter* tp,  std::shared_ptr<triggeralgs::TriggerActivityMaker> tam);

  private:

  std::vector<std::shared_ptr<triggeralgs::TriggerActivityMaker>> m_tams;

  std::shared_ptr<iomanager::SenderConcept<triggeralgs::TriggerActivity>> m_ta_sink;

  daqdataformats::SourceID m_sourceid;

  std::atomic<uint64_t> m_tp_received_count{ 0 };  // NOLINT(build/unsigned)
  std::atomic<uint64_t> m_ta_made_count{ 0 };
  std::atomic<uint64_t> m_ta_sent_count{ 0 };
  std::atomic<uint64_t> m_ta_failed_sent_count{ 0 };
  void print_opmon_stats();
};

} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_SRC_TRIGGER_TPPROCESSOR_HPP_
