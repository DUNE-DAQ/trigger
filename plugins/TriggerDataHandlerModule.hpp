/**
 * @file TriggerDataHandlerModule.hpp FarDetector Generic readout
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef TRIGGER_PLUGINS_TRIGGERDATAHANDLER_HPP_
#define TRIGGER_PLUGINS_TRIGGERDATAHANDLER_HPP_

#include "appfwk/DAQModule.hpp"

#include "datahandlinglibs/RawDataHandlerBase.hpp"

#include <string>

namespace dunedaq {
namespace trigger {

class TriggerDataHandlerModule : public dunedaq::appfwk::DAQModule,
				 public dunedaq::datahandlinglibs::RawDataHandlerBase
{
public:
  using inherited_dlh = dunedaq::datahandlinglibs::RawDataHandlerBase;
  using inherited_mod = dunedaq::appfwk::DAQModule;
  /**
   * @brief TriggerDataHandlerModule Constructor
   * @param name Instance name for this TriggerDataHandlerModule instance
   */
  explicit TriggerDataHandlerModule(const std::string& name);

  TriggerDataHandlerModule(const TriggerDataHandlerModule&) = delete;            ///< TriggerDataHandlerModule is not copy-constructible
  TriggerDataHandlerModule& operator=(const TriggerDataHandlerModule&) = delete; ///< TriggerDataHandlerModule is not copy-assignable
  TriggerDataHandlerModule(TriggerDataHandlerModule&&) = delete;                 ///< TriggerDataHandlerModule is not move-constructible
  TriggerDataHandlerModule& operator=(TriggerDataHandlerModule&&) = delete;      ///< TriggerDataHandlerModule is not move-assignable

  void init(std::shared_ptr<appfwk::ModuleConfiguration> cfg) override;
  // void get_info(opmonlib::InfoCollector& ci, int level) override;

  std::shared_ptr<datahandlinglibs::DataHandlingConcept>
  create_readout(const appmodel::DataHandlerModule* modconf, std::atomic<bool>& run_marker) override;

};

} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_PLUGINS_TRIGGERDATAHANDLER_HPP_
