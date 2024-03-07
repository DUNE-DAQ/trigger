/**
 * @file DataSubscriber.cpp DataSubscriber class implementation
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#include "DataSubscriber.hpp"
#include "logging/Logging.hpp"

#include "readoutlibs/ReadoutLogging.hpp"
#include "readoutlibs/ReadoutIssues.hpp"
#include "readoutlibs/models/DataSubscriberModel.hpp"
#include "trigger/HSISourceModel.hpp"
#include "trigger/TPSetSourceModel.hpp"
#include "trigger/TriggerSourceModel.hpp"

#include "appdal/DataSubscriber.hpp"

#include "trigger/TriggerPrimitiveTypeAdapter.hpp"
#include "trigger/TAWrapper.hpp"
#include "trigger/TCWrapper.hpp"
#include "trgdataformats/TriggerPrimitive.hpp"
#include "triggeralgs/TriggerActivity.hpp"
#include "triggeralgs/TriggerCandidate.hpp"

using namespace dunedaq::readoutlibs::logging;

namespace dunedaq {


//DUNE_DAQ_TYPESTRING(dunedaq::trigger::TPSet, "TPSet")
DUNE_DAQ_TYPESTRING(dunedaq::trigger::TriggerPrimitiveTypeAdapter, "TriggerPrimitive")
DUNE_DAQ_TYPESTRING(dunedaq::trigger::TAWrapper, "TriggerActivity")
DUNE_DAQ_TYPESTRING(dunedaq::trigger::TCWrapper, "TriggerCandidate")

namespace trigger {

DataSubscriber::DataSubscriber(const std::string& name)
  : DAQModule(name), m_source_concept(nullptr)
{

  inherited_mod::register_command("start", &DataSubscriber::do_start);
  inherited_mod::register_command("drain_dataflow", &DataSubscriber::do_stop);
}

void
DataSubscriber::init(std::shared_ptr<appfwk::ModuleConfiguration> cfg)
{
  TLOG_DEBUG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Entering init() method";
  auto ini = cfg->module<coredal::DaqModule>(get_name());
  if (ini->get_outputs().size() != 1) {
    throw readoutlibs::InitializationError(ERS_HERE, "Only 1 output supported for subscribers");
  }
  if (ini->get_inputs().size() != 1) {
    throw readoutlibs::InitializationError(ERS_HERE, "Only 1 input supported for subscribers");
  }
  m_source_concept = create_data_subscriber(ini);
  m_source_concept->init(ini);
  TLOG_DEBUG(TLVL_ENTER_EXIT_METHODS) <<  ": Exiting init() method";
}

void
DataSubscriber::do_start(const nlohmann::json& /*args*/) {
  m_source_concept->start();
}

void
DataSubscriber::do_stop(const nlohmann::json& /*args*/) {
  m_source_concept->stop();
}

void
DataSubscriber::get_info(opmonlib::InfoCollector& ci, int level)
{
  m_source_concept->get_info(ci, level);
}


std::unique_ptr<readoutlibs::SourceConcept>
DataSubscriber::create_data_subscriber(const coredal::DaqModule* cfg)
{
 
  auto datatypes = cfg->get_outputs()[0]->get_data_type();
  auto raw_dt = cfg->get_inputs()[0]->get_data_type();
  
  if (raw_dt == "TPSet") {
    TLOG_DEBUG(TLVL_WORK_STEPS) << "Creating trigger primitives subscriber";
    auto source_model =
      std::make_unique<trigger::TPSetSourceModel>();
    return source_model;
  }

  if (raw_dt == "TriggerActivity") {
    TLOG_DEBUG(TLVL_WORK_STEPS) << "Creating trigger activities subscriber";
    auto source_model =
      std::make_unique<trigger::TriggerSourceModel<triggeralgs::TriggerActivity, trigger::TAWrapper>>();
    return source_model;
  }

  if (raw_dt == "TriggerCandidate") {
    TLOG_DEBUG(TLVL_WORK_STEPS) << "Creating trigger candidates subscriber";
    auto source_model =
      std::make_unique<trigger::TriggerSourceModel<triggeralgs::TriggerCandidate, trigger::TCWrapper>>();
    return source_model;
  }

   if (raw_dt == "HSIEvent") {
    TLOG_DEBUG(TLVL_WORK_STEPS) << "Creating trigger candidates subscriber";
    auto source_model =
      std::make_unique<trigger::HSISourceModel>();
    return source_model;
  }
  return nullptr;
}

} // namespace trigger
} // namespace dunedaq

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::DataSubscriber)
