/**
 * @file Utilities.cpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2021.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_INCLUDE_TRIGGER_UTILITIES_HPP_
#define TRIGGER_INCLUDE_TRIGGER_UTILITIES_HPP_

namespace dunedaq {
namespace trigger {

  using namespace std::chrono;

  std::atomic<uint64_t> get_current_system_time()
  {
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
  }

} // namespace trigger
} // namespace dunedaq
#endif // TRIGGER_INCLUDE_TRIGGER_UTILITIES_HPP_
