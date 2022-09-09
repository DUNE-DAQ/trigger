/**
 * @file TCZipper.hpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_PLUGINS_TCZIPPER_HPP_
#define TRIGGER_PLUGINS_TCZIPPER_HPP_

#include "TriggerZipper.hpp"
#include "trigger/TCSet.hpp"

namespace dunedaq::trigger {

using TCZipper = TriggerZipper<TCSet>;

} // namespace dunedaq::trigger
#endif // TRIGGER_PLUGINS_TCZIPPER_HPP_
