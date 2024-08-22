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

#include "datahandlinglibs/FrameErrorRegistry.hpp"
#include "datahandlinglibs/DataHandlingIssues.hpp"
#include "datahandlinglibs/ReadoutLogging.hpp"
#include "datahandlinglibs/models/IterableQueueModel.hpp"
#include "datahandlinglibs/utils/ReusableThread.hpp"


#include "triggeralgs/TriggerActivity.hpp"

#include "trigger/AlgorithmPlugins.hpp"
#include "triggeralgs/TriggerActivityMaker.hpp"

#include "appmodel/TPDataProcessor.hpp"
#include "appmodel/TAAlgorithm.hpp"

using dunedaq::datahandlinglibs::logging::TLVL_BOOKKEEPING;
using dunedaq::datahandlinglibs::logging::TLVL_TAKE_NOTE;

// THIS SHOULDN'T BE HERE!!!!! But it is necessary.....
DUNE_DAQ_TYPESTRING(dunedaq::trigger::TriggerPrimitiveTypeAdapter, "TriggerPrimitive")

namespace dunedaq {
namespace trigger {

TPProcessor::TPProcessor(std::unique_ptr<datahandlinglibs::FrameErrorRegistry>& error_registry)
  : TaskRawDataProcessorModel<TriggerPrimitiveTypeAdapter>(error_registry)
{
}

TPProcessor::~TPProcessor()
{}

void
TPProcessor::start(const nlohmann::json& args)
{

  // Reset stats
  m_tp_received_count.store(0);
  m_ta_made_count.store(0);
  m_ta_sent_count.store(0);
  m_ta_failed_sent_count.store(0);

  inherited::start(args);
}

void
TPProcessor::stop(const nlohmann::json& args)
{
  inherited::stop(args);
  print_opmon_stats();
}

void
TPProcessor::conf(const appmodel::DataHandlerModule* conf)
{
  for (auto output : conf->get_outputs()) {
   try {
      if (output->get_data_type() == "TriggerActivity") {
         m_ta_sink = get_iom_sender<triggeralgs::TriggerActivity>(output->UID());
      }
    } catch (const ers::Issue& excpt) {
      ers::error(datahandlinglibs::ResourceQueueError(ERS_HERE, "ta", "DefaultRequestHandlerModel", excpt));
    }
  }

  m_sourceid.id = conf->get_source_id();
  m_sourceid.subsystem = TriggerPrimitiveTypeAdapter::subsystem;
  
  std::vector<const appmodel::TAAlgorithm*> ta_algorithms;
  auto dp = conf->get_module_configuration()->get_data_processor();
  auto proc_conf = dp->cast<appmodel::TPDataProcessor>();
  if (proc_conf != nullptr && proc_conf->get_mask_processing() == false) {
    ta_algorithms = proc_conf->get_algorithms();
    }

  for (auto algo : ta_algorithms)  {
    TLOG() << "Selected TA algorithm: " << algo->UID() << " from class " << algo->class_name();
    std::shared_ptr<triggeralgs::TriggerActivityMaker> maker = make_ta_maker(algo->class_name());
    nlohmann::json algo_json = algo->to_json(true);

    TLOG() << "Algo config:\n" << algo_json.dump();

    maker->configure(algo_json[algo->UID()]);
    inherited::add_postprocess_task(std::bind(&TPProcessor::find_ta, this, std::placeholders::_1, maker));
    m_tams.push_back(maker);
  }
  inherited::conf(conf);
}

// void
// TPProcessor::get_info(opmonlib::InfoCollector& ci, int level)
// {
//   //TLOG() << "Generated TAs = " << m_new_tas << ", dropped TAs = " << m_tas_dropped;
//   inherited::get_info(ci, level);
//   //ci.add(info);
// }

void
TPProcessor::generate_opmon_data()
{
  opmon::TPProcessorInfo info;

  info.set_tp_received_count( m_tp_received_count.load() );
  info.set_ta_made_count( m_ta_made_count.load() );
  info.set_ta_sent_count( m_ta_sent_count.load() );
  info.set_ta_failed_sent_count( m_ta_failed_sent_count.load() );

  this->publish(std::move(info));
}

/**
 * Pipeline Stage 2.: Do software TPG
 * */
void
TPProcessor::find_ta(const TriggerPrimitiveTypeAdapter* tp,  std::shared_ptr<triggeralgs::TriggerActivityMaker> taa)
{
  m_tp_received_count++;	
  std::vector<triggeralgs::TriggerActivity> tas;
  taa->operator()(tp->tp, tas);

  while (tas.size()) {
      m_ta_made_count++;
      if (!m_ta_sink->try_send(std::move(tas.back()), iomanager::Sender::s_no_block)) {
        ers::warning(TADropped(ERS_HERE, tp->tp.time_start, m_sourceid.id));
        m_ta_failed_sent_count++;
      }
      m_ta_sent_count++;
      tas.pop_back();
  }
  return;
}

void
TPProcessor::print_opmon_stats()
{
  TLOG() << "TPProcessor opmon counters summary:";
  TLOG() << "------------------------------";
  TLOG() << "TPs received: \t\t" << m_tp_received_count;
  TLOG() << "TAs made: \t\t\t" << m_ta_made_count;
  TLOG() << "TAs sent: \t\t\t" << m_ta_sent_count;
  TLOG() << "TAs failed to send: \t" << m_ta_failed_sent_count;
  TLOG();
}

} // namespace fdreadoutlibs
} // namespace dunedaq
