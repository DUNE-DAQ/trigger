/**
 * @file TCSetBuffer.hpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_SRC_TRIGGER_TCSETBUFFER_HPP_
#define TRIGGER_SRC_TRIGGER_TCSETBUFFER_HPP_

#include "BufferManager.hpp"
#include "trigger/TCSet.hpp"

namespace dunedaq::trigger {

using TCSetBuffer = BufferManager<TCSet>;

} // namespace dunedaq::trigger
#endif // TRIGGER_SRC_TRIGGER_TCSETBUFFER_HPP_
