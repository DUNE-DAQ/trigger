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

#include "readoutlibs/models/TaskRawDataProcessorModel.hpp"

//#include "triggger/Issues.hpp"
#include "trigger/TriggerPrimitiveTypeAdapter.hpp"
#include "triggeralgs/TriggerActivity.hpp"

#include "triggeralgs/Types.hpp"
#include "triggeralgs/TriggerActivityMaker.hpp"

#include "appdal/ReadoutModule.hpp"

namespace dunedaq {
namespace trigger {

class TPProcessor : public readoutlibs::TaskRawDataProcessorModel<TriggerPrimitiveTypeAdapter>
{

public:
  using inherited = readoutlibs::TaskRawDataProcessorModel<TriggerPrimitiveTypeAdapter>;
  using tpptr = TriggerPrimitiveTypeAdapter*;
  using consttpptr = const TriggerPrimitiveTypeAdapter*;


  explicit TPProcessor(std::unique_ptr<readoutlibs::FrameErrorRegistry>& error_registry);

  ~TPProcessor();

  void start(const nlohmann::json& args) override;

  void stop(const nlohmann::json& args) override;

  void conf(const appdal::ReadoutModule* conf) override;

  void get_info(opmonlib::InfoCollector& ci, int level) override;

protected:
  // Internals
  dunedaq::daqdataformats::timestamp_t m_previous_ts = 0;
  dunedaq::daqdataformats::timestamp_t m_current_ts = 0;

  /**
   * Pipeline Stage 2.: Do TA finding
   * */

  void find_ta(const TriggerPrimitiveTypeAdapter* tp,  triggeralgs::TriggerActivityMaker* tam);

  private:

  std::vector<std::unique_ptr<triggeralgs::TriggerActivityMaker>> m_tams;

  std::shared_ptr<iomanager::SenderConcept<triggeralgs::TriggerActivity>> m_ta_sink;

  daqdataformats::SourceID m_sourceid;

  std::atomic<uint64_t> m_new_tas{ 0 };  // NOLINT(build/unsigned)
  std::atomic<uint64_t> m_tas_dropped{ 0 };
};

} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_SRC_TRIGGER_TPPROCESSOR_HPP_
