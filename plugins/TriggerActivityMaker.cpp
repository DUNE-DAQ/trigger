/**
 * @file TriggerActivityMaker.cpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "TriggerActivityMaker.hpp"

#include "trigger/AlgorithmPlugins.hpp"
#include "trigger/triggeractivitymaker/Nljs.hpp"

#include <memory>

namespace dunedaq::trigger {

std::shared_ptr<triggeralgs::TriggerActivityMaker>
TriggerActivityMaker::make_maker(const nlohmann::json& obj)
{
  auto params = obj.get<triggeractivitymaker::Conf>();
  set_algorithm_name(params.activity_maker);
  set_sourceid(params.geoid_element);
  set_windowing(params.window_time, params.buffer_time);
  std::shared_ptr<triggeralgs::TriggerActivityMaker> maker = make_ta_maker(params.activity_maker);
  maker->configure(params.activity_maker_config);
  maker->set_region(params.geoid_element);
  std::cout << "Made a TAMaker - data_vs_system_time param: " << maker->m_data_vs_system_time << "\n";

  return maker;
}

} // namespace dunedaq::trigger

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::TriggerActivityMaker)
