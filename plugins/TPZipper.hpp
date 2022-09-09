/**
 * @file TPZipper.hpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_PLUGINS_TPZIPPER_HPP_
#define TRIGGER_PLUGINS_TPZIPPER_HPP_

#include "TriggerZipper.hpp"
#include "trigger/TPSet.hpp"

namespace dunedaq::trigger {

using TPZipper = TriggerZipper<TPSet>;

} // namespace dunedaq::trigger
#endif // TRIGGER_PLUGINS_TPZIPPER_HPP_
