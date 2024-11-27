/**
 * @file TriggerActivityMaker.hpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_PLUGINS_TRIGGERACTIVITYMAKERPDS_HPP_
#define TRIGGER_PLUGINS_TRIGGERACTIVITYMAKERPDS_HPP_

#include "trigger/TriggerGenericMaker.hpp"

#include "trigger/TPSet.hpp"
#include "triggeralgs/TriggerActivityPDS.hpp"
#include "triggeralgs/TriggerActivityMakerPDS.hpp"
#include "triggeralgs/TriggerPrimitivePDS.hpp"

#include <memory>
#include <string>

namespace dunedaq::trigger {

class TriggerActivityMakerPDS
  : public TriggerGenericMaker<Set<triggeralgs::TriggerPrimitivePDS>,
                               triggeralgs::TriggerActivityPDS,
                               triggeralgs::TriggerActivityMakerPDS>
{
public:
  explicit TriggerActivityMakerPDS(const std::string& name)
    : TriggerGenericMaker(name)
  {
  }

  TriggerActivityMakerPDS(const TriggerActivityMakerPDS&) = delete;
  TriggerActivityMakerPDS& operator=(const TriggerActivityMakerPDS&) = delete;
  TriggerActivityMakerPDS(TriggerActivityMakerPDS&&) = delete;
  TriggerActivityMakerPDS& operator=(TriggerActivityMakerPDS&&) = delete;

private:
  virtual std::unique_ptr<triggeralgs::TriggerActivityMakerPDS> make_maker(const nlohmann::json& obj) override;
};

} // namespace dunedaq::trigger

#endif // TRIGGER_PLUGINS_TRIGGERACTIVITYMAKERPDS_HPP_
