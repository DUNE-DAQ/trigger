/**
 * @file TriggerDecisionMaker.hpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_PLUGINS_TRIGGERDECISIONMAKER_HPP_
#define TRIGGER_PLUGINS_TRIGGERDECISIONMAKER_HPP_

#include "trigger/TriggerGenericMaker.hpp"

#include "triggeralgs/TriggerCandidate.hpp"
#include "triggeralgs/TriggerDecision.hpp"
#include "triggeralgs/TriggerDecisionMaker.hpp"

#include <memory>
#include <string>

namespace dunedaq::trigger {

class TriggerDecisionMaker
  : public TriggerGenericMaker<triggeralgs::TriggerCandidate,
                               triggeralgs::TriggerDecision,
                               triggeralgs::TriggerDecisionMaker>
{
public:
  explicit TriggerDecisionMaker(const std::string& name)
    : TriggerGenericMaker(name)
  {}

  TriggerDecisionMaker(const TriggerDecisionMaker&) = delete;
  TriggerDecisionMaker& operator=(const TriggerDecisionMaker&) = delete;
  TriggerDecisionMaker(TriggerDecisionMaker&&) = delete;
  TriggerDecisionMaker& operator=(TriggerDecisionMaker&&) = delete;

private:
  virtual std::shared_ptr<triggeralgs::TriggerDecisionMaker> make_maker(const nlohmann::json& obj);
};

} // namespace dunedaq::trigger

#endif // TRIGGER_PLUGINS_TRIGGERDECISIONMAKER_HPP_
