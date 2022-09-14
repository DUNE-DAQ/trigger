/**
 * @file TASetBuffer.hpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_SRC_TRIGGER_TASETBUFFER_HPP_
#define TRIGGER_SRC_TRIGGER_TASETBUFFER_HPP_

#include "BufferManager.hpp"
#include "trigger/TASet.hpp"

namespace dunedaq::trigger {

using TASetBuffer = BufferManager<TASet>;

} // namespace dunedaq::trigger
#endif // TRIGGER_SRC_TRIGGER_TASETBUFFER_HPP_
