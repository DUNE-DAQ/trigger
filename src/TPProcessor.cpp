/**
 * @file TPProcessor.hpp TPC TP specific Task based raw processor
 *
 * This is part of the DUNE DAQ , copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#include "trigger/TPProcessor.hpp" // NOLINT(build/include)
#include "trigger/Issues.hpp" // NOLINT(build/include)

#include "iomanager/Sender.hpp"
#include "logging/Logging.hpp"

#include "readoutlibs/FrameErrorRegistry.hpp"
#include "readoutlibs/ReadoutIssues.hpp"
#include "readoutlibs/ReadoutLogging.hpp"
#include "readoutlibs/models/IterableQueueModel.hpp"
#include "readoutlibs/utils/ReusableThread.hpp"


#include "triggeralgs/TriggerActivity.hpp"

#include "trigger/AlgorithmPlugins.hpp"
#include "triggeralgs/TriggerActivityMaker.hpp"

#include "appdal/TPDataProcessor.hpp"
#include "appdal/TAAlgorithm.hpp"

using dunedaq::readoutlibs::logging::TLVL_BOOKKEEPING;
using dunedaq::readoutlibs::logging::TLVL_TAKE_NOTE;

// THIS SHOULDN'T BE HERE!!!!! But it is necessary.....
DUNE_DAQ_TYPESTRING(dunedaq::trigger::TriggerPrimitiveTypeAdapter, "TriggerPrimitive")

namespace dunedaq {
namespace trigger {

TPProcessor::TPProcessor(std::unique_ptr<readoutlibs::FrameErrorRegistry>& error_registry)
  : TaskRawDataProcessorModel<TriggerPrimitiveTypeAdapter>(error_registry)
{
}

TPProcessor::~TPProcessor()
{}

void
TPProcessor::start(const nlohmann::json& args)
{

  // Reset stats
  m_new_tas = 0;
  m_tas_dropped = 0;
  inherited::start(args);
}

void
TPProcessor::stop(const nlohmann::json& args)
{
  inherited::stop(args);
}

void
TPProcessor::conf(const appdal::ReadoutModule* conf)
{
  for (auto output : conf->get_outputs()) {
   try {
      if (output->get_data_type() == "TriggerActivity") {
         m_ta_sink = get_iom_sender<triggeralgs::TriggerActivity>(output->UID());
      }
    } catch (const ers::Issue& excpt) {
      ers::error(readoutlibs::ResourceQueueError(ERS_HERE, "ta", "DefaultRequestHandlerModel", excpt));
    }
  }

  m_sourceid.id = conf->get_source_id();
  m_sourceid.subsystem = TriggerPrimitiveTypeAdapter::subsystem;
  
  std::vector<const appdal::TAAlgorithm*> ta_algorithms;
  auto dp = conf->get_module_configuration()->get_data_processor();
  auto proc_conf = dp->cast<appdal::TPDataProcessor>();
  if (proc_conf != nullptr && proc_conf->get_mask_processing() == false) {
    ta_algorithms = proc_conf->get_algorithms();
    }

  for (auto algo : ta_algorithms)  {
    TLOG() << "Selected TA algorithm: " << algo->UID() << " from class " << algo->class_name();
    std::shared_ptr<triggeralgs::TriggerActivityMaker> maker = make_ta_maker(algo->class_name());
    nlohmann::json algo_json = algo->to_json(true);

    TLOG() << "Algo config:\n" << algo_json.dump();

    maker->configure(algo_json);
    inherited::add_postprocess_task(std::bind(&TPProcessor::find_ta, this, std::placeholders::_1, maker));
    m_tams.push_back(maker);
  }
  inherited::conf(conf);
}

void
TPProcessor::get_info(opmonlib::InfoCollector& ci, int level)
{
  TLOG() << "Generated TAs = " << m_new_tas << ", dropped TAs = " << m_tas_dropped;
  inherited::get_info(ci, level);
  //ci.add(info);
}


/**
 * Pipeline Stage 2.: Do software TPG
 * */
void
TPProcessor::find_ta(const TriggerPrimitiveTypeAdapter* tp,  std::shared_ptr<triggeralgs::TriggerActivityMaker> taa)
{
	
  std::vector<triggeralgs::TriggerActivity> tas;
  taa->operator()(tp->tp, tas);

  while (tas.size()) {
      if (!m_ta_sink->try_send(std::move(tas.back()), iomanager::Sender::s_no_block)) {
        ers::warning(TADropped(ERS_HERE, tp->tp.time_start, m_sourceid.id));
        m_tas_dropped++;
      }
      m_new_tas++;
      tas.pop_back();
  }
  return;
}

} // namespace fdreadoutlibs
} // namespace dunedaq
