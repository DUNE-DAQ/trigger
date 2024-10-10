/**
 * @file Latency.hpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2021.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_INCLUDE_TRIGGER_LATENCY_HPP_
#define TRIGGER_INCLUDE_TRIGGER_LATENCY_HPP_

#include "utilities/TimestampEstimator.hpp"
#include "utilities/TimestampEstimatorSystem.hpp"

#include <atomic>
#include <chrono>
#include <iostream> // Include for std::ostream

namespace dunedaq {
  namespace trigger {

    class Latency {
      using latency = uint64_t;

      public:
          // Enumeration for selecting time units
          enum class TimeUnit { Microseconds = 1, Milliseconds = 2 };

          // Constructor with optional time unit selection (defaults to Microseconds)
          Latency(TimeUnit time_unit = TimeUnit::Microseconds)
              : m_latency_in(0), m_latency_out(0), m_time_unit(time_unit) {
              setup_conversion();
          }

          ~Latency() {}

          // Function to update latency_in
          void update_latency_in(uint64_t latency) {
              update_single_latency(latency, m_latency_in);
          }

          // Function to update latency_out
          void update_latency_out(uint64_t latency) {
              update_single_latency(latency, m_latency_out);
          }

          // Function to get the value of latency_in
          latency get_latency_in() const {
              return m_latency_in.load();
          }

          // Function to get the value of latency_out
          latency get_latency_out() const {
              return m_latency_out.load();
          }

      private:
          // Set up conversion based on the selected time unit
          void setup_conversion() {
              if (m_time_unit == TimeUnit::Microseconds) {
                  m_clock_ticks_conversion = 16 * 1e-3; // Conversion for microseconds
                  m_get_current_time = []() {
                      return std::chrono::duration_cast<std::chrono::microseconds>(
                          std::chrono::system_clock::now().time_since_epoch()).count();
                  };
              } else {
                  m_clock_ticks_conversion = 16 * 1e-6; // Conversion for milliseconds
                  m_get_current_time = []() {
                      return std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch()).count();
                  };
              }
          }

          // Function to get the current system time based on the set time unit
          uint64_t get_current_system_time() const {
              return m_get_current_time();
          }

          // Single update function for both latencies
          void update_single_latency(uint64_t latency, std::atomic<uint64_t>& latency_atomic) {
              uint64_t current_time = get_current_system_time();
              uint64_t latency_time = latency * m_clock_ticks_conversion;
              uint64_t diff = (current_time >= latency_time) ? (current_time - latency_time) : 0;
              latency_atomic.store(diff);
          }

          std::atomic<latency> m_latency_in;  // Member variable to store latency_in
          std::atomic<latency> m_latency_out; // Member variable to store latency_out
          TimeUnit m_time_unit;               // Member variable to store the selected time unit (ms or ns)
          double m_clock_ticks_conversion;    // Conversion factor from ticks to the selected time unit

          // Lambda to get the current time
          std::function<uint64_t()> m_get_current_time;
    };

  } // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_INCLUDE_TRIGGER_LATENCY_HPP_
