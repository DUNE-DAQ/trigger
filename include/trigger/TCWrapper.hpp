/**
 * @file TCWrapper.cpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2021.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_INCLUDE_TRIGGER_TCWRAPPER_HPP_
#define TRIGGER_INCLUDE_TRIGGER_TCWRAPPER_HPP_

#include "daqdataformats/Fragment.hpp"

#include "triggeralgs/TriggerObjectOverlay.hpp"
#include "triggeralgs/TriggerActivity.hpp"
#include "triggeralgs/TriggerCandidate.hpp"
#include "trigger/Issues.hpp"

namespace dunedaq {
namespace trigger {
    struct TCWrapper
  {
    triggeralgs::TriggerCandidate candidate;
    std::vector<uint8_t> candidate_overlay_buffer;
    // Don't really want this default ctor, but IterableQueueModel requires it
    TCWrapper() {}
    
    TCWrapper(triggeralgs::TriggerCandidate c)
      : candidate(c)
    {
      populate_buffer();
    }

    void populate_buffer()
    {
      candidate_overlay_buffer.resize(triggeralgs::get_overlay_nbytes(candidate));
      triggeralgs::write_overlay(candidate, candidate_overlay_buffer.data());
    }
    
    // comparable based on first timestamp
    bool operator<(const TCWrapper& other) const
    {
      return this->candidate.time_start < other.candidate.time_start;
    }

    uint64_t get_timestamp() const // NOLINT(build/unsigned)
    {
      return candidate.time_start;;
    }

    uint64_t get_first_timestamp() const // NOLINT(build/unsigned)
    {
      return candidate.time_start;
    }

    void set_first_timestamp(uint64_t ts) // NOLINT(build/unsigned)
    {
      candidate.time_start = ts;
    }

    size_t get_payload_size() { return candidate_overlay_buffer.size(); }

    size_t get_num_frames() { return 1; }

    size_t get_frame_size() { return get_payload_size(); }

    TCWrapper* begin()
    {
      //return candidate_overlay_buffer.data();
      return this;
    }
    
    TCWrapper* end()
    {
      return (TCWrapper*)(candidate_overlay_buffer.data()+candidate_overlay_buffer.size());
    }

    //static const constexpr size_t fixed_payload_size = 5568;
    static const constexpr daqdataformats::SourceID::Subsystem subsystem = daqdataformats::SourceID::Subsystem::kTrigger;
    static const constexpr daqdataformats::FragmentType fragment_type = daqdataformats::FragmentType::kTriggerCandidate;
    // No idea what this should really be set to
    static const constexpr uint64_t expected_tick_difference = 16; // NOLINT(build/unsigned)

};
} // namespace trigger
} // namespace dunedaq
#endif // TRIGGER_INCLUDE_TRIGGER_TCWRAPPER_HPP_
