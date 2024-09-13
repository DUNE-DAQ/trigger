/**
 * @file Latency.hpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2021.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_INCLUDE_TRIGGER_LATENCY_HPP_
#define TRIGGER_INCLUDE_TRIGGER_LATENCY_HPP_

#include <atomic>
#include <chrono>
#include <mutex>

namespace dunedaq {
namespace trigger {

  class Latency {
  public:

    // Enumeration for selecting time units
    enum class TimeUnit { Milliseconds, Microseconds };
   
    // Constructor with optional time unit selection (defaults to Milliseconds)
    Latency(TimeUnit time_unit = TimeUnit::Milliseconds)
      : m_latency_in(0), m_latency_out(0), m_time_unit(time_unit)
    {
      // Set the clock tick conversion factor based on time unit
      if (m_time_unit == TimeUnit::Milliseconds) {
        m_clock_ticks_conversion.store(16 * 1e-6); // For milliseconds: 1 tick = 16 * 10^-6 ms
      } else {
        m_clock_ticks_conversion.store(16 * 1e-3);
      }
      // to convert 62.5MHz clock ticks to ms: 1/62500000 = 0.000000016 <- seconds per tick; 0.000016 <- ms per tick;
      // 16*1e-6 <- sci notation
    }

    // Function to get the current system time in ms or ns based on time unit
    uint64_t get_current_system_time() const
    {
      if (m_time_unit == TimeUnit::Milliseconds) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::system_clock::now().time_since_epoch()).count();
      } else {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                 std::chrono::system_clock::now().time_since_epoch()).count();
      }
    }

    // Function to update latency_in
    void update_latency_in(uint64_t latency)
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_latency_in.store(latency * m_clock_ticks_conversion.load());
    }

    // Function to update latency_out
    void update_latency_out(uint64_t latency)
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_latency_out.store(latency * m_clock_ticks_conversion.load());
    }

    // Function to get the value of latency_in
    uint64_t get_latency_in() const
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (m_latency_in.load() != 0) {
        // in edge cases the TP time was more recent then current sys time...
        // this is a catch for that
        uint64_t diff = abs(int64_t(get_current_system_time()) - int64_t(m_latency_in.load()));
        return diff;
      } else {
        return 0;
      }
    }

    // Function to get the value of latency_out
    uint64_t get_latency_out() const
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (m_latency_out.load() != 0) {
        uint64_t diff = abs(int64_t(get_current_system_time()) - int64_t(m_latency_out.load()));
        return diff;
      } else {
        return 0;
      }
    }

  private:
    std::atomic<uint64_t> m_latency_in;  // Member variable to store latency_in
    std::atomic<uint64_t> m_latency_out; // Member variable to store latency_out
    std::atomic<double> m_clock_ticks_conversion; // Dynamically adjusted conversion factor for clock ticks
    mutable std::mutex m_mutex;
    TimeUnit m_time_unit;  // Member variable to store the selected time unit (ms or ns)
  };

} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_INCLUDE_TRIGGER_LATENCY_HPP_
