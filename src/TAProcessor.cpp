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

#include "readoutlibs/FrameErrorRegistry.hpp"
#include "readoutlibs/ReadoutIssues.hpp"
#include "readoutlibs/ReadoutLogging.hpp"
#include "readoutlibs/models/IterableQueueModel.hpp"
//#include "readoutlibs/readoutconfig/Nljs.hpp"
//#include "readoutlibs/readoutinfo/InfoNljs.hpp"
#include "readoutlibs/utils/ReusableThread.hpp"

//#include "detchannelmaps/TPCChannelMap.hpp"

#include "trigger/TAWrapper.hpp"
#include "triggeralgs/TriggerActivity.hpp"

#include "trigger/AlgorithmPlugins.hpp"
#include "triggeralgs/TriggerCandidateMaker.hpp"

#include "appdal/TADataProcessor.hpp"
#include "appdal/TCAlgorithm.hpp"

using dunedaq::readoutlibs::logging::TLVL_BOOKKEEPING;
using dunedaq::readoutlibs::logging::TLVL_TAKE_NOTE;

// THIS SHOULDN'T BE HERE!!!!! But it is necessary.....
DUNE_DAQ_TYPESTRING(dunedaq::trigger::TAWrapper, "TriggerActivity")

namespace dunedaq {
namespace trigger {

TAProcessor::TAProcessor(std::unique_ptr<readoutlibs::FrameErrorRegistry>& error_registry)
  : readoutlibs::TaskRawDataProcessorModel<TAWrapper>(error_registry)
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
TAProcessor::conf(const appdal::ReadoutModule* conf)
{
  for (auto output : conf->get_outputs()) {
   try {
      if (output->get_data_type() == "TriggerCandidate") {
         m_tc_sink = get_iom_sender<triggeralgs::TriggerCandidate>(output->UID());
      }
    } catch (const ers::Issue& excpt) {
      ers::error(readoutlibs::ResourceQueueError(ERS_HERE, "tc", "DefaultRequestHandlerModel", excpt));
    }
  }

  m_sourceid.id = conf->get_source_id();
  m_sourceid.subsystem = trigger::TAWrapper::subsystem;
  std::vector<const appdal::TCAlgorithm*> tc_algorithms;
  auto dp = conf->get_module_configuration()->get_data_processor();
  auto proc_conf = dp->cast<appdal::TADataProcessor>();
  if (proc_conf != nullptr && proc_conf->get_mask_processing() == false ) {
    tc_algorithms = proc_conf->get_algorithms();
    }

  for (auto algo : tc_algorithms)  {
    TLOG() << "Selected TC algorithm: " << algo->UID();
    //std::unique_ptr<triggeralgs::TriggerCandidateMaker> maker = make_tc_maker(algo->class_name());
    std::unique_ptr<triggeralgs::TriggerCandidateMaker> maker = make_tc_maker("TriggerCandidateMakerPrescalePlugin");
    //FIXME: I need and oks2json....
    nlohmann::json algo_json = algo->to_json(true);
    maker->configure(algo_json);
    inherited::add_postprocess_task(std::bind(&TAProcessor::find_tc, this, std::placeholders::_1, maker.get()));
    m_tcms.push_back(std::move(maker));
  }
  inherited::conf(conf);
}

void
TAProcessor::get_info(opmonlib::InfoCollector& ci, int level)
{

  inherited::get_info(ci, level);
  //ci.add(info);
}


/**
 * Pipeline Stage 2.: Do software TPG
 * */
void
TAProcessor::find_tc(const TAWrapper* ta,  triggeralgs::TriggerCandidateMaker* tca)
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
