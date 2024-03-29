/**
 * @file AlgorithmPlugins.hpp
 *
 * This file contains the macro definitions for creating Trigger plugins
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_INCLUDE_TRIGGER_ALGORITHMPLUGINS_HPP_
#define TRIGGER_INCLUDE_TRIGGER_ALGORITHMPLUGINS_HPP_

#include "triggeralgs/TriggerDecisionMaker.hpp"
#include "triggeralgs/TriggerActivityFactory.hpp"
#include "triggeralgs/TriggerCandidateFactory.hpp"
#include "trigger/Issues.hpp"

#include "cetlib/BasicPluginFactory.h"

#include <memory>
#include <string>

namespace dunedaq::trigger {
/**
 * @brief Load a TriggerActivityMaker plugin and return a unique_ptr to the contained
 * class
 * @param plugin_name Name of the plugin
 * @return unique_ptr to created TriggerActivityMaker instance
 */
inline std::unique_ptr<triggeralgs::TriggerActivityMaker>
make_ta_maker(std::string const& plugin_name)
{
  auto ta_factory = triggeralgs::TriggerActivityFactory::get_instance();
  auto ta_maker = ta_factory->build_maker(plugin_name);
  if (!ta_maker) {
    throw MissingFactoryItemError(ERS_HERE, plugin_name);
  }
  return ta_maker;
}

} // namespace dunedaq::trigger

namespace dunedaq::trigger {
/**
 * @brief Load a TriggerCandidateMaker plugin and return a unique_ptr to the contained
 * class
 * @param plugin_name Name of the plugin
 * @return unique_ptr to created TriggerCandidateMaker instance
 */
inline std::unique_ptr<triggeralgs::TriggerCandidateMaker>
make_tc_maker(std::string const& plugin_name)
{
  auto tc_factory = triggeralgs::TriggerCandidateFactory::get_instance();
  auto tc_maker = tc_factory->build_maker(plugin_name);
  if (!tc_maker) {
    throw MissingFactoryItemError(ERS_HERE, plugin_name);
  }
  return tc_maker;
}

} // namespace dunedaq::trigger

/**
 * @brief Declare the function that will be called by the plugin loader
 * @param klass Class to be defined as a DUNE TDMaker module
 */
// NOLINTNEXTLINE(build/define_used)
#define DEFINE_DUNE_TD_MAKER(klass)                                                                                    \
  extern "C"                                                                                                           \
  {                                                                                                                    \
    std::unique_ptr<triggeralgs::TriggerDecisionMaker> make()                                                          \
    {                                                                                                                  \
      return std::unique_ptr<triggeralgs::TriggerDecisionMaker>(new klass());                                            \
    }                                                                                                                  \
  }

namespace dunedaq::trigger {
/**
 * @brief Load a TriggerDecisionMaker plugin and return a unique_ptr to the contained
 * class
 * @param plugin_name Name of the plugin
 * @return unique_ptr to created TriggerDecisionMaker instance
 */
inline std::unique_ptr<triggeralgs::TriggerDecisionMaker>
make_td_maker(std::string const& plugin_name)
{
  static cet::BasicPluginFactory bpf("duneTDMaker", "make");

  // TODO Philip Rodrigues <philiprodrigues@github.com> Apr-04-2021: Rethrow any cetlib exception as an ERS issue
  return bpf.makePlugin<std::unique_ptr<triggeralgs::TriggerDecisionMaker>>(plugin_name);
}

} // namespace dunedaq::trigger

#endif // TRIGGER_INCLUDE_TRIGGER_ALGORITHMPLUGINS_HPP_
