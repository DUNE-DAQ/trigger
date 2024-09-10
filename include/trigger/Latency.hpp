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

namespace dunedaq {
namespace trigger {

  class Latency {
  public:
    Latency() 
      : m_latency_in(0), m_latency_out(0), m_clock_ticks_to_ms(16 * 1e-6)
    {}

    // to convert 62.5MHz clock ticks to ms: 1/62500000 = 0.000000016 <- seconds per tick; 0.000016 <- ms per tick;
    // 16*1e-6 <- sci notation

    // Function to get the current system time
    std::atomic<uint64_t> get_current_system_time() const
    {
      return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
    }

    // Function to update latency_in
    void update_latency_in(uint64_t latency)
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_latency_in.store(latency * m_clock_ticks_to_ms);
    }

    // Function to update latency_out
    void update_latency_out(uint64_t latency)
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_latency_out.store(latency * m_clock_ticks_to_ms);
    }

    // Function to get the value of latency_in
    uint64_t get_latency_in() const
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (m_latency_in.load() != 0) {
        // in edge cases the TP time was more recent then current sys time...
        // this is a catch for that
	uint64_t diff = abs( int64_t(get_current_system_time()) - int64_t(m_latency_in.load()) );
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
        uint64_t diff = abs( int64_t(get_current_system_time()) - int64_t(m_latency_out.load()) );
        return diff;
      } else {
        return 0;
      }
    }

  private:
    std::atomic<uint64_t> m_latency_in;  // Member variable to store latency_in
    std::atomic<uint64_t> m_latency_out; // Member variable to store latency_out
    std::atomic<double> m_clock_ticks_to_ms;
    mutable std::mutex m_mutex;
  };

} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_INCLUDE_TRIGGER_LATENCY_HPP_
