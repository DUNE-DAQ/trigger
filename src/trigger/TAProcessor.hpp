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

#include "appdal/ReadoutModule.hpp"

#include "readoutlibs/models/TaskRawDataProcessorModel.hpp"

#include "trigger/Issues.hpp"
#include "trigger/TAWrapper.hpp"
#include "triggeralgs/TriggerCandidate.hpp"

#include "triggeralgs/Types.hpp"
#include "triggeralgs/TriggerCandidateMaker.hpp"

namespace dunedaq {
namespace trigger {

class TAProcessor : public readoutlibs::TaskRawDataProcessorModel<TAWrapper>
{

public:
  using inherited = readoutlibs::TaskRawDataProcessorModel<TAWrapper>;
  using taptr = TAWrapper*;
  using consttaptr = const TAWrapper*;


  explicit TAProcessor(std::unique_ptr<readoutlibs::FrameErrorRegistry>& error_registry);

  ~TAProcessor();

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

  void find_tc(const TAWrapper* ta, std::shared_ptr<triggeralgs::TriggerCandidateMaker> tcm);

  private:

  std::vector<std::shared_ptr<triggeralgs::TriggerCandidateMaker>> m_tcms;

  std::shared_ptr<iomanager::SenderConcept<triggeralgs::TriggerCandidate>> m_tc_sink;

  daqdataformats::SourceID m_sourceid;

  std::atomic<uint64_t> m_new_tcs{ 0 };  // NOLINT(build/unsigned)
  std::atomic<uint64_t> m_tcs_dropped{ 0 };
};

} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_SRC_TRIGGER_TPPROCESSOR_HPP_
