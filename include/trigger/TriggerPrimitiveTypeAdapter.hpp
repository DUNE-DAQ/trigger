#ifndef TRIGGER_INCLUDE_TRIGGER_TRIGGERPRIMITIVETYPEADAPTER_HPP_
#define TRIGGER_INCLUDE_TRIGGER_TRIGGERPRIMITIVETYPEADAPTER_HPP_


#include "daqdataformats/FragmentHeader.hpp"
#include "daqdataformats/SourceID.hpp"
#include "trgdataformats/TriggerPrimitive.hpp"
#include "trgdataformats/Types.hpp"

#include <cstdint> // uint_t types
#include <memory>  // unique_ptr
#include <vector>
#include <cstring> // memcpy
#include <tuple> // tie

namespace dunedaq {
namespace trigger {

const constexpr std::size_t kTriggerPrimitiveSize = sizeof(trgdataformats::TriggerPrimitive);
struct TriggerPrimitiveTypeAdapter
{
  using FrameType = TriggerPrimitiveTypeAdapter;
  // data
  trgdataformats::TriggerPrimitive tp;
  // comparable based on start timestamp
  bool operator<(const TriggerPrimitiveTypeAdapter& other) const
  {
    return std::tie(this->tp.time_start, this->tp.channel) < std::tie(other.tp.time_start, other.tp.channel);
  }

  uint64_t get_first_timestamp() const // NOLINT(build/unsigned)
  {
    return tp.time_start;
  }

  void set_first_timestamp(uint64_t ts) // NOLINT(build/unsigned)
  {
    tp.time_start = ts;
  }

  uint64_t get_timestamp() const // NOLINT(build/unsigned)
  {
    return tp.time_start;
  }

  void fake_timestamps(uint64_t first_timestamp, uint64_t /*offset = 25*/) // NOLINT(build/unsigned)
  {
    tp.time_start = first_timestamp;
  }

  void fake_geoid(uint16_t /*crate_id*/, uint16_t /*slot_id*/, uint16_t /*link_id*/) {}

  void fake_adc_pattern(int /*channel*/) {}

  FrameType* begin() { return this; }

  FrameType* end() { return (this + 1); } // NOLINT

  size_t get_payload_size() { return kTriggerPrimitiveSize; }

  size_t get_num_frames() { return 1; }

  size_t get_frame_size() { return kTriggerPrimitiveSize; }

  static const constexpr daqdataformats::SourceID::Subsystem subsystem = daqdataformats::SourceID::Subsystem::kTrigger;
  static const constexpr daqdataformats::FragmentType fragment_type = daqdataformats::FragmentType::kTriggerPrimitive;
  static const constexpr uint64_t expected_tick_difference = 1; // NOLINT(build/unsigned)
};

static_assert(sizeof(trgdataformats::TriggerPrimitive) == kTriggerPrimitiveSize,
              "Check your assumptions on TriggerPrimitiveTypeAdapter");

} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_INCLUDE_TRIGGER_TRIGGERPRIMITIVETYPEADAPTER_HPP_
