/**
 * @file DataSubscriberModule.hpp FarDetector Data Reader
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef TRIGGER_PLUGINS_DATASUBSCRIBER_HPP_
#define TRIGGER_PLUGINS_DATASUBSCRIBER_HPP_

//#include "appfwk/cmd/Nljs.hpp"
//#include "appfwk/app/Nljs.hpp"
//#include "appfwk/cmd/Structs.hpp"

#include "appfwk/DAQModule.hpp"
#include "appfwk/ModuleConfiguration.hpp"

#include "readoutlibs/concepts/SourceConcept.hpp"

#include <string>

namespace dunedaq {
namespace trigger {

class DataSubscriberModule : public dunedaq::appfwk::DAQModule
{
public:
  using inherited_mod = dunedaq::appfwk::DAQModule;
  /**
   * @brief DataSubscriberModule Constructor
   * @param name Instance name for this DataSubscriberModule instance
   */
  explicit DataSubscriberModule(const std::string& name);

  DataSubscriberModule(const DataSubscriberModule&) = delete;            ///< DataSubscriberModule is not copy-constructible
  DataSubscriberModule& operator=(const DataSubscriberModule&) = delete; ///< DataSubscriberModule is not copy-assignable
  DataSubscriberModule(DataSubscriberModule&&) = delete;                 ///< DataSubscriberModule is not move-constructible
  DataSubscriberModule& operator=(DataSubscriberModule&&) = delete;      ///< DataSubscriberModule is not move-assignable

  void init(std::shared_ptr<appfwk::ModuleConfiguration> cfg) override;
  void get_info(opmonlib::InfoCollector& ci, int level) override;

  std::unique_ptr<readoutlibs::SourceConcept> create_data_subscriber(const confmodel::DaqModule* cfg);
private:
  void do_start(const nlohmann::json& /*args*/);
  void do_stop(const nlohmann::json& /*args*/);

  // Internal
  std::unique_ptr<readoutlibs::SourceConcept> m_source_concept;

};

} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_PLUGINS_DATASUBSCRIBER_HPP_
