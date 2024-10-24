/**
 * @file TriggerDataHandlerModule.cpp TriggerDataHandlerModule class implementation
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#include "TriggerDataHandlerModule.hpp"

#include "logging/Logging.hpp"
#include "iomanager/IOManager.hpp"

#include "datahandlinglibs/DataHandlingIssues.hpp"
#include "datahandlinglibs/ReadoutLogging.hpp"
#include "datahandlinglibs/models/DataHandlingModel.hpp"
#include "datahandlinglibs/models/SkipListLatencyBufferModel.hpp"
#include "datahandlinglibs/models/DefaultSkipListRequestHandler.hpp"
#include "trigger/TPRequestHandler.hpp"

#include "trigger/TriggerPrimitiveTypeAdapter.hpp"
#include "trigger/TPProcessor.hpp"
#include "trigger/TAProcessor.hpp"
#include "trigger/TCProcessor.hpp"


#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace dunedaq::datahandlinglibs::logging;

namespace dunedaq {

DUNE_DAQ_TYPESTRING(dunedaq::trigger::TriggerPrimitiveTypeAdapter, "TriggerPrimitive")
DUNE_DAQ_TYPESTRING(dunedaq::trigger::TAWrapper, "TriggerActivity")
DUNE_DAQ_TYPESTRING(dunedaq::trigger::TCWrapper, "TriggerCandidate")

namespace trigger {

TriggerDataHandlerModule::TriggerDataHandlerModule(const std::string& name)
  : DAQModule(name)
  , RawDataHandlerBase(name)
{ 
  //inherited_dlh::m_readout_creator = make_readout_creator("fd");

  inherited_mod::register_command("conf", &inherited_dlh::do_conf);
  inherited_mod::register_command("scrap", &inherited_dlh::do_scrap);
  inherited_mod::register_command("start", &inherited_dlh::do_start);
  inherited_mod::register_command("stop_trigger_sources", &inherited_dlh::do_stop);
  inherited_mod::register_command("record", &inherited_dlh::do_record);
}

void
TriggerDataHandlerModule::init(std::shared_ptr<appfwk::ModuleConfiguration> cfg)
{

  TLOG_DEBUG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Entering init() method";
  inherited_dlh::init(cfg);
  TLOG_DEBUG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Exiting init() method";
}

std::shared_ptr<datahandlinglibs::DataHandlingConcept>
TriggerDataHandlerModule::create_readout(const appmodel::DataHandlerModule* modconf, std::atomic<bool>& run_marker)
{
  namespace rol = dunedaq::datahandlinglibs;

  // Acquire DataType  
  std::string raw_dt = modconf->get_module_configuration()->get_input_data_type();
  TLOG() << "Choosing specializations for DataHandlingModel with data_type:" << raw_dt << ']';

  // IF TriggerPrimitive (TP)
  if (raw_dt.find("TriggerPrimitive") != std::string::npos) {
    TLOG(TLVL_WORK_STEPS) << "Creating readout for TriggerPrimitive";
    auto readout_model = std::make_shared<rol::DataHandlingModel<
      TriggerPrimitiveTypeAdapter,
      TPRequestHandler,
      rol::SkipListLatencyBufferModel<TriggerPrimitiveTypeAdapter>,
      TPProcessor>>(run_marker);
    register_node("TPProcessor", readout_model); 
    readout_model->init(modconf);
    return readout_model;
  }

 // IF TriggerActivity (TA)
  if (raw_dt.find("TriggerActivity") != std::string::npos) {
    TLOG(TLVL_WORK_STEPS) << "Creating readout for TriggerActivity";
    auto readout_model = std::make_shared<rol::DataHandlingModel<
      TAWrapper,
      rol::DefaultSkipListRequestHandler<trigger::TAWrapper>,
      rol::SkipListLatencyBufferModel<trigger::TAWrapper>,
      TAProcessor>>(run_marker);
    register_node("TAProcessor", readout_model); 
    
    readout_model->init(modconf);
    return readout_model;
  }

 // No processing, only buffering to respond to data requests
  if (raw_dt.find("TriggerCandidate") != std::string::npos) {
    TLOG(TLVL_WORK_STEPS) << "Creating readout for TriggerCandidate";
    auto readout_model = std::make_shared<rol::DataHandlingModel<
      TCWrapper,
      rol::DefaultSkipListRequestHandler<trigger::TCWrapper>,
      rol::SkipListLatencyBufferModel<trigger::TCWrapper>,
      TCProcessor>>(run_marker);
    register_node("TCProcessor", readout_model); 
    
    readout_model->init(modconf);
    return readout_model;
  }

  return nullptr;
}

} // namespace trigger
} // namespace dunedaq

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::TriggerDataHandlerModule)
