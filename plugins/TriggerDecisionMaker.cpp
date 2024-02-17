/**
 * @file TriggerDecisionMaker.cpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "TriggerDecisionMaker.hpp"

#include "trigger/AlgorithmPlugins.hpp"
#include "trigger/triggerdecisionmaker/Nljs.hpp"

#include <memory>

namespace dunedaq::trigger {

std::unique_ptr<triggeralgs::TriggerDecisionMaker>
TriggerDecisionMaker::make_maker(const nlohmann::json& obj)
{
  auto params = obj.get<triggerdecisionmaker::Conf>();
  set_algorithm_name(params.decision_maker);
  std::unique_ptr<triggeralgs::TriggerDecisionMaker> maker = make_td_maker(params.decision_maker);
  maker->configure(params.decision_maker_config);
  return maker;
}

void 
TriggerDecisionMaker::init(std::shared_ptr<dunedaq::appfwk::ModuleConfiguration>)
{};

} // namespace dunedaq::trigger

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::TriggerDecisionMaker)
