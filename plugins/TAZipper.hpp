/**
 * @file TAZipper.hpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_PLUGINS_TAZIPPER_HPP_
#define TRIGGER_PLUGINS_TAZIPPER_HPP_

#include "TriggerZipper.hpp"
#include "trigger/TASet.hpp"

namespace dunedaq::trigger {

using TAZipper = TriggerZipper<TASet>;

} // namespace dunedaq::trigger
#endif // TRIGGER_PLUGINS_TAZIPPER_HPP_
