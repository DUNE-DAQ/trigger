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

std::shared_ptr<triggeralgs::TriggerDecisionMaker>
TriggerDecisionMaker::make_maker(const nlohmann::json& obj)
{
  auto params = obj.get<triggerdecisionmaker::Conf>();
  set_algorithm_name(params.decision_maker);
  std::shared_ptr<triggeralgs::TriggerDecisionMaker> maker = make_td_maker(params.decision_maker);
  maker->configure(params.decision_maker_config);
  return maker;
}

} // namespace dunedaq::trigger

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::TriggerDecisionMaker)
