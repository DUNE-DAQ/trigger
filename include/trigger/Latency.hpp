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
      using latency = double;

      public:
          // Enumeration for selecting time units
          enum class TimeUnit { Milliseconds = 1, Microseconds = 2 };

          // Constructor with optional time unit selection (defaults to Milliseconds)
          Latency(TimeUnit time_unit = TimeUnit::Milliseconds)
              : m_latency_in(0), m_latency_out(0), m_time_unit(time_unit), clock_freq(62500000) {

              setup_conversion();

              // Start timestamp estimator
              m_timestamp_estimator.reset(new utilities::TimestampEstimatorSystem(clock_freq));
          }

          ~Latency() {
              m_timestamp_estimator.reset(nullptr); // Calls TimestampEstimator dtor
          }

          // Function to get the current system time in ms or ns based on time unit
          uint64_t get_current_system_time() const {
              return m_timestamp_estimator->get_timestamp_estimate();
          }

          // Function to update latency_in
          void update_latency_in(uint64_t latency) {
              uint64_t current_time = get_current_system_time();
              uint64_t diff = (current_time >= latency) ? (current_time - latency) : 0;
              m_latency_in.store( diff * m_clock_ticks_conversion );
              TLOG() << static_cast<int>(m_time_unit) << " " << current_time << " " << latency << " " << diff << " " << m_latency_in.load();
          }

          // Function to update latency_out
          void update_latency_out(uint64_t latency) {
              uint64_t current_time = get_current_system_time();
              uint64_t diff = (current_time >= latency) ? (current_time - latency) : 0;
              m_latency_out.store( diff * m_clock_ticks_conversion );
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
          void setup_conversion() {
              if (m_time_unit == TimeUnit::Milliseconds) {
                  m_clock_ticks_conversion = 16 * 1e-6; // For milliseconds
              } else {
                  m_clock_ticks_conversion = 16 * 1e-3; // For microseconds
              }
          }

          std::atomic<latency> m_latency_in;  // Member variable to store latency_in
          std::atomic<latency> m_latency_out; // Member variable to store latency_out
          TimeUnit m_time_unit;               // Member variable to store the selected time unit (ms or ns)
	  latency m_clock_ticks_conversion;   // Conversion factor from ticks to the selected time unit
	  std::unique_ptr<utilities::TimestampEstimatorBase> m_timestamp_estimator;
          latency clock_freq;
          // Function pointer or lambda for conversion based on time unit
          std::function<uint64_t(uint64_t)> m_convert_latency;
    };

  } // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_INCLUDE_TRIGGER_LATENCY_HPP_
