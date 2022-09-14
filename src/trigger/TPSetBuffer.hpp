/**
 * @file TPSetBuffer.hpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_SRC_TRIGGER_TPSETBUFFER_HPP_
#define TRIGGER_SRC_TRIGGER_TPSETBUFFER_HPP_

#include "BufferManager.hpp"
#include "trigger/TPSet.hpp"

namespace dunedaq::trigger {

using TPSetBuffer = BufferManager<TPSet>;

} // namespace dunedaq::trigger
#endif // TRIGGER_SRC_TRIGGER_TPSETBUFFER_HPP_
