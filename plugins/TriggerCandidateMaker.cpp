/**
 * @file TriggerCandidateMaker.cpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "TriggerCandidateMaker.hpp"

#include "trigger/AlgorithmPlugins.hpp"
#include "trigger/triggercandidatemaker/Nljs.hpp"

#include <memory>

namespace dunedaq::trigger {

std::shared_ptr<triggeralgs::TriggerCandidateMaker>
TriggerCandidateMaker::make_maker(const nlohmann::json& obj)
{
  auto params = obj.get<triggercandidatemaker::Conf>();
  set_algorithm_name(params.candidate_maker);
  std::shared_ptr<triggeralgs::TriggerCandidateMaker> maker = make_tc_maker(params.candidate_maker);
  maker->configure(params.candidate_maker_config);
  return maker;
}

void 
TriggerCandidateMaker::init(std::shared_ptr<dunedaq::appfwk::ModuleConfiguration>)
{};

} // namespace dunedaq::trigger

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::TriggerCandidateMaker)
