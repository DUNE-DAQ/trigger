/**
 * @file TimeSliceInputBuffer.hpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_SRC_TRIGGER_TIMESLICEINPUTBUFFER_HPP_
#define TRIGGER_SRC_TRIGGER_TIMESLICEINPUTBUFFER_HPP_

#include "trigger/Issues.hpp"
#include "trigger/Set.hpp"

#include "logging/Logging.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace dunedaq::trigger {

// When reading Set<T> from a queue, we want to buffer all Set<T> with the same
// time_start and time_end. Finally, get a vector of all contained objects in
// time order when a Set<T> with a different time_start arrives.
// This class encapsulates that logic.
template<class T>
class TimeSliceInputBuffer
{
public:
  // Parameters purely for ers warning metadata
  TimeSliceInputBuffer(const std::string& name, const std::string& algorithm)
    : m_name(name)
    , m_algorithm(algorithm)
  {
  }
  // Add a new Set<T> to the buffer. If it's inconsistent with buffered events,
  // fill time_slice, start_time, end_time with the previous (complete) slice.
  // Returns whether the previous slice was complete (and time_slice etc was filled)
  bool buffer(Set<T> in,
              std::vector<T>& time_slice,
              daqdataformats::timestamp_t& start_time,
              daqdataformats::timestamp_t& end_time)
  {
    if (m_buffer.size() == 0 || m_buffer.back().start_time == in.start_time) {
      // if `in` is the current time slice
      m_buffer.emplace_back(in);
      return false; // buffer the time slice
    }
    // obtain the current (complete) time slice
    flush(time_slice, start_time, end_time);
    // add `in`, which is the next time slice
    m_buffer.emplace_back(in);
    return true;
  }
  // Fill time_slice with the sorted buffer, clear the buffer, and return true
  // Returns false and does nothing if the buffer is empty
  bool flush(std::vector<T>& time_slice, daqdataformats::timestamp_t& start_time, daqdataformats::timestamp_t& end_time)
  {
    if (m_buffer.size() == 0) {
      return false;
    }
    // build a vector of the T objects from all the sets in the slice
    start_time = m_buffer[0].start_time;
    end_time = m_buffer[0].end_time;
    for (Set<T>& x : m_buffer) {
      if (x.start_time != start_time || x.end_time != end_time) {
        ers::warning(InconsistentSetTimeError(ERS_HERE, m_name, m_algorithm));
      }
      time_slice.insert(time_slice.end(), x.objects.begin(), x.objects.end());
    }
    // clear the buffer
    m_buffer.clear();
    // sort the vector by time_start property of T
    std::sort(time_slice.begin(), time_slice.end(), [](const T& a, const T& b) { return a.time_start < b.time_start; });
    // TODO Benjamin Land <BenLand100@github.com> June-01-2021: would be nice if the T (TriggerPrimative, etc) included
    // a natural ordering with operator<()
    return true;
  }

private:
  std::vector<Set<T>> m_buffer;
  const std::string &m_name, &m_algorithm;
};

} // namespace dunedaq::trigger

#endif // TRIGGER_SRC_TRIGGER_TIMESLICEINPUTBUFFER_HPP_
