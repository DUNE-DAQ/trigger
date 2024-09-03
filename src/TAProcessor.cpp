/**
 * @file TAProcessor.hpp TPC TP specific Task based raw processor
 *
 * This is part of the DUNE DAQ , copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#include "trigger/TAProcessor.hpp" // NOLINT(build/include)

//#include "appfwk/DAQModuleHelper.hpp"
#include "iomanager/Sender.hpp"
#include "logging/Logging.hpp"

#include "datahandlinglibs/FrameErrorRegistry.hpp"
#include "datahandlinglibs/DataHandlingIssues.hpp"
#include "datahandlinglibs/ReadoutLogging.hpp"
#include "datahandlinglibs/models/IterableQueueModel.hpp"
#include "datahandlinglibs/utils/ReusableThread.hpp"

//#include "detchannelmaps/TPCChannelMap.hpp"

#include "trigger/TAWrapper.hpp"
#include "triggeralgs/TriggerActivity.hpp"

#include "trigger/AlgorithmPlugins.hpp"
#include "triggeralgs/TriggerCandidateMaker.hpp"

#include "appmodel/TADataProcessor.hpp"
#include "appmodel/TCAlgorithm.hpp"

using dunedaq::datahandlinglibs::logging::TLVL_BOOKKEEPING;
using dunedaq::datahandlinglibs::logging::TLVL_TAKE_NOTE;

// THIS SHOULDN'T BE HERE!!!!! But it is necessary.....
DUNE_DAQ_TYPESTRING(dunedaq::trigger::TAWrapper, "TriggerActivity")

namespace dunedaq {
namespace trigger {

TAProcessor::TAProcessor(std::unique_ptr<datahandlinglibs::FrameErrorRegistry>& error_registry, bool post_processing_enabled)
  : datahandlinglibs::TaskRawDataProcessorModel<TAWrapper>(error_registry, post_processing_enabled)
{
}

TAProcessor::~TAProcessor()
{}

void
TAProcessor::start(const nlohmann::json& args)
{

  // Reset stats
  m_new_tcs = 0;
  m_tcs_dropped = 0;
  inherited::start(args);
}

void
TAProcessor::stop(const nlohmann::json& args)
{
  inherited::stop(args);
}

void
TAProcessor::conf(const appmodel::DataHandlerModule* conf)
{
  for (auto output : conf->get_outputs()) {
   try {
      if (output->get_data_type() == "TriggerCandidate") {
         m_tc_sink = get_iom_sender<triggeralgs::TriggerCandidate>(output->UID());
      }
    } catch (const ers::Issue& excpt) {
      ers::error(datahandlinglibs::ResourceQueueError(ERS_HERE, "tc", "DefaultRequestHandlerModel", excpt));
    }
  }

  m_sourceid.id = conf->get_source_id();
  m_sourceid.subsystem = trigger::TAWrapper::subsystem;
  std::vector<const appmodel::TCAlgorithm*> tc_algorithms;
  auto dp = conf->get_module_configuration()->get_data_processor();
  auto proc_conf = dp->cast<appmodel::TADataProcessor>();
  if (proc_conf != nullptr && m_post_processing_enabled ) {
    tc_algorithms = proc_conf->get_algorithms();
    }

  for (auto algo : tc_algorithms)  {
    TLOG() << "Selected TC algorithm: " << algo->UID();
    std::shared_ptr<triggeralgs::TriggerCandidateMaker> maker = make_tc_maker(algo->class_name());
    nlohmann::json algo_json = algo->to_json(true);
    maker->configure(algo_json[algo->UID()]);
    inherited::add_postprocess_task(std::bind(&TAProcessor::find_tc, this, std::placeholders::_1, maker));
    m_tcms.push_back(maker);
  }
  inherited::conf(conf);
}

// void
// TAProcessor::get_info(opmonlib::InfoCollector& ci, int level)
// {

//   inherited::get_info(ci, level);
//   //ci.add(info);
// }


/**
 * Pipeline Stage 2.: Do software TPG
 * */
void
TAProcessor::find_tc(const TAWrapper* ta,  std::shared_ptr<triggeralgs::TriggerCandidateMaker> tca)
{
  std::vector<triggeralgs::TriggerCandidate> tcs;
  tca->operator()(ta->activity, tcs);
  for (auto tc : tcs) {
    if(!m_tc_sink->try_send(std::move(tc), iomanager::Sender::s_no_block)) {
        ers::warning(TCDropped(ERS_HERE, tc.time_start, m_sourceid.id));
        m_tcs_dropped++;
    }
    m_new_tcs++;
  }
  return;
}

} // namespace fdreadoutlibs
} // namespace dunedaq
