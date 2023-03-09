/**
 * @file TriggerActivityMaker.hpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_PLUGINS_TRIGGERACTIVITYMAKER_HPP_
#define TRIGGER_PLUGINS_TRIGGERACTIVITYMAKER_HPP_

#include "trigger/TriggerGenericMaker.hpp"

#include "triggeralgs/TriggerActivity.hpp"
#include "triggeralgs/TriggerActivityMaker.hpp"
#include "triggeralgs/TriggerPrimitive.hpp"

#include <memory>
#include <string>

namespace dunedaq::trigger {

class TriggerActivityMaker
  : public TriggerGenericMaker<Set<triggeralgs::TriggerPrimitive>,
                               Set<triggeralgs::TriggerActivity>,
                               triggeralgs::TriggerActivityMaker>
{
public:
  explicit TriggerActivityMaker(const std::string& name)
    : TriggerGenericMaker(name)
  {}

  TriggerActivityMaker(const TriggerActivityMaker&) = delete;
  TriggerActivityMaker& operator=(const TriggerActivityMaker&) = delete;
  TriggerActivityMaker(TriggerActivityMaker&&) = delete;
  TriggerActivityMaker& operator=(TriggerActivityMaker&&) = delete;

private:
  virtual std::shared_ptr<triggeralgs::TriggerActivityMaker> make_maker(const nlohmann::json& obj) override;
  
};

} // namespace dunedaq::trigger

#endif // TRIGGER_PLUGINS_TRIGGERACTIVITYMAKER_HPP_
