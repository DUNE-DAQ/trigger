/**
 * @file TriggerDataHandler.hpp FarDetector Generic readout
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef TRIGGER_PLUGINS_TRIGGERDATAHANDLER_HPP_
#define TRIGGER_PLUGINS_TRIGGERDATAHANDLER_HPP_

#include "appfwk/DAQModule.hpp"

#include "readoutmodules/DataLinkHandlerBase.hpp"

#include <string>

namespace dunedaq {
namespace trigger {

class TriggerDataHandler : public dunedaq::appfwk::DAQModule,
                          public dunedaq::readoutmodules::DataLinkHandlerBase
{
public:
  using inherited_dlh = dunedaq::readoutmodules::DataLinkHandlerBase;
  using inherited_mod = dunedaq::appfwk::DAQModule;
  /**
   * @brief TriggerDataHandler Constructor
   * @param name Instance name for this TriggerDataHandler instance
   */
  explicit TriggerDataHandler(const std::string& name);

  TriggerDataHandler(const TriggerDataHandler&) = delete;            ///< TriggerDataHandler is not copy-constructible
  TriggerDataHandler& operator=(const TriggerDataHandler&) = delete; ///< TriggerDataHandler is not copy-assignable
  TriggerDataHandler(TriggerDataHandler&&) = delete;                 ///< TriggerDataHandler is not move-constructible
  TriggerDataHandler& operator=(TriggerDataHandler&&) = delete;      ///< TriggerDataHandler is not move-assignable

  void init(std::shared_ptr<appfwk::ModuleConfiguration> cfg) override;
  void get_info(opmonlib::InfoCollector& ci, int level) override;

  std::unique_ptr<readoutlibs::ReadoutConcept>
  create_readout(const appdal::ReadoutModule* modconf, std::atomic<bool>& run_marker) override;

};

} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_PLUGINS_TRIGGERDATAHANDLER_HPP_