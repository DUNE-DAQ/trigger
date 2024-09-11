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
  m_ta_received_count.store(0);
  m_tc_made_count.store(0);
  m_tc_sent_count.store(0);
  m_tc_failed_sent_count.store(0);

  m_running_flag.store(true);

  inherited::start(args);
}

void
TAProcessor::stop(const nlohmann::json& args)
{
  inherited::stop(args);
  m_running_flag.store(false);
  print_opmon_stats();
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
  m_latency_monitoring = dp->get_latency_monitoring_conf()->get_latency_monitoring();
  inherited::conf(conf);
}

void
TAProcessor::generate_opmon_data()
{
  opmon::TAProcessorInfo info;

  info.set_ta_received_count( m_ta_received_count.load() );
  info.set_tc_made_count( m_tc_made_count.load() );
  info.set_tc_sent_count( m_tc_sent_count.load() );
  info.set_tc_failed_sent_count( m_tc_failed_sent_count.load() );

  this->publish(std::move(info));

  if (m_running_flag.load()) {
    opmon::TriggerLatency lat_info;

    lat_info.set_latency_in( m_latency_instance.get_latency_in() );
    lat_info.set_latency_out( m_latency_instance.get_latency_out() );

    this->publish(std::move(lat_info));
  }
}

/**
 * Pipeline Stage 2.: Do software TPG
 * */
void
TAProcessor::find_tc(const TAWrapper* ta,  std::shared_ptr<triggeralgs::TriggerCandidateMaker> tca)
{
  //time_activity gave 0 :/
  m_latency_instance.update_latency_in( ta->activity.time_start );
  m_ta_received_count++;
  std::vector<triggeralgs::TriggerCandidate> tcs;
  tca->operator()(ta->activity, tcs);
  for (auto tc : tcs) {
    m_tc_made_count++;
    if(!m_tc_sink->try_send(std::move(tc), iomanager::Sender::s_no_block)) {
        ers::warning(TCDropped(ERS_HERE, tc.time_start, m_sourceid.id));
        m_tc_failed_sent_count++;
    } else {
      m_tc_sent_count++;
    }
    m_latency_instance.update_latency_out( tc.time_candidate );
  }
  return;
}

void
TAProcessor::print_opmon_stats()
{
  TLOG() << "TAProcessor opmon counters summary:";
  TLOG() << "------------------------------";
  TLOG() << "TAs received: \t\t" << m_ta_received_count;
  TLOG() << "TCs made: \t\t\t" << m_tc_made_count;
  TLOG() << "TCs sent: \t\t\t" << m_tc_sent_count;
  TLOG() << "TCs failed to send: \t" << m_tc_failed_sent_count;
  TLOG();
}

} // namespace fdreadoutlibs
} // namespace dunedaq
