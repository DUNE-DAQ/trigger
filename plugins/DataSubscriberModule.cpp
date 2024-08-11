/**
 * @file DataSubscriberModule.cpp DataSubscriberModule class implementation
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#include "DataSubscriberModule.hpp"
#include "logging/Logging.hpp"

#include "datahandlinglibs/ReadoutLogging.hpp"
#include "datahandlinglibs/DataHandlingIssues.hpp"
#include "datahandlinglibs/models/DataSubscriberModel.hpp"
#include "trigger/HSISourceModel.hpp"
#include "trigger/TPSetSourceModel.hpp"
#include "trigger/TriggerSourceModel.hpp"

#include "appmodel/DataSubscriberModule.hpp"

#include "trigger/TriggerPrimitiveTypeAdapter.hpp"
#include "trigger/TAWrapper.hpp"
#include "trigger/TCWrapper.hpp"
#include "trgdataformats/TriggerPrimitive.hpp"
#include "triggeralgs/TriggerActivity.hpp"
#include "triggeralgs/TriggerCandidate.hpp"

using namespace dunedaq::datahandlinglibs::logging;

namespace dunedaq {


//DUNE_DAQ_TYPESTRING(dunedaq::trigger::TPSet, "TPSet")
DUNE_DAQ_TYPESTRING(dunedaq::trigger::TriggerPrimitiveTypeAdapter, "TriggerPrimitive")
DUNE_DAQ_TYPESTRING(dunedaq::trigger::TAWrapper, "TriggerActivity")
DUNE_DAQ_TYPESTRING(dunedaq::trigger::TCWrapper, "TriggerCandidate")

namespace trigger {

DataSubscriberModule::DataSubscriberModule(const std::string& name)
  : DAQModule(name), m_source_concept(nullptr)
{

  inherited_mod::register_command("start", &DataSubscriberModule::do_start);
  inherited_mod::register_command("drain_dataflow", &DataSubscriberModule::do_stop);
}

void
DataSubscriberModule::init(std::shared_ptr<appfwk::ModuleConfiguration> cfg)
{
  TLOG_DEBUG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Entering init() method";
  auto ini = cfg->module<confmodel::DaqModule>(get_name());
  if (ini->get_outputs().size() != 1) {
    throw datahandlinglibs::InitializationError(ERS_HERE, "Only 1 output supported for subscribers");
  }
  if (ini->get_inputs().size() != 1) {
    throw datahandlinglibs::InitializationError(ERS_HERE, "Only 1 input supported for subscribers");
  }
  m_source_concept = create_data_subscriber(ini);
  register_node(get_name(), m_source_concept); 
  m_source_concept->init(ini);
  TLOG_DEBUG(TLVL_ENTER_EXIT_METHODS) <<  ": Exiting init() method";
}

void
DataSubscriberModule::do_start(const nlohmann::json& /*args*/) {
  m_source_concept->start();
}

void
DataSubscriberModule::do_stop(const nlohmann::json& /*args*/) {
  m_source_concept->stop();
}

// void
// DataSubscriberModule::get_info(opmonlib::InfoCollector& ci, int level)
// {
//   m_source_concept->get_info(ci, level);
// }


std::shared_ptr<datahandlinglibs::SourceConcept>
DataSubscriberModule::create_data_subscriber(const confmodel::DaqModule* cfg)
{
 
  auto datatypes = cfg->get_outputs()[0]->get_data_type();
  auto raw_dt = cfg->get_inputs()[0]->get_data_type();
  
  if (raw_dt == "TPSet") {
    TLOG_DEBUG(1) << "Creating trigger primitives subscriber";
    auto source_model =
      std::make_shared<trigger::TPSetSourceModel>();
    return source_model;
  }

  if (raw_dt == "TriggerActivity") {
    TLOG_DEBUG(1) << "Creating trigger activities subscriber";
    auto source_model =
      std::make_shared<trigger::TriggerSourceModel<triggeralgs::TriggerActivity, trigger::TAWrapper>>();
    return source_model;
  }

  if (raw_dt == "TriggerCandidate") {
    TLOG_DEBUG(1) << "Creating trigger candidates subscriber";
    auto source_model =
      std::make_shared<trigger::TriggerSourceModel<triggeralgs::TriggerCandidate, trigger::TCWrapper>>();
    return source_model;
  }

   if (raw_dt == "HSIEvent") {
    TLOG_DEBUG(1) << "Creating trigger candidates subscriber";
    auto source_model =
      std::make_shared<trigger::HSISourceModel>();
    return source_model;
  }
  return nullptr;
}

} // namespace trigger
} // namespace dunedaq

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::DataSubscriberModule)
