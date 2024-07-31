/**
 * @file TAWrapper.cpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2021.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_INCLUDE_TRIGGER_TAWRAPPER_HPP_
#define TRIGGER_INCLUDE_TRIGGER_TAWRAPPER_HPP_

#include "daqdataformats/Fragment.hpp"
#include "iomanager/Receiver.hpp"
#include "triggeralgs/TriggerObjectOverlay.hpp"
#include "triggeralgs/TriggerPrimitive.hpp"
#include "triggeralgs/TriggerActivity.hpp"
#include "trigger/Issues.hpp"

namespace dunedaq {
namespace trigger {
  struct TAWrapper
  {
    triggeralgs::TriggerActivity activity;
    //std::vector<uint8_t> activity_overlay_buffer;
    
    // Don't really want this default ctor, but IterableQueueModel requires it
    TAWrapper() {}
    
    TAWrapper(triggeralgs::TriggerActivity a)
      : activity(a)
    {
      //populate_buffer();
    }
/*
    void populate_buffer()
    {
      activity_overlay_buffer.resize(triggeralgs::get_overlay_nbytes(activity));
      triggeralgs::write_overlay(activity, activity_overlay_buffer.data());
    }
*/    
    // comparable based on first timestamp
    bool operator<(const TAWrapper& other) const
    {
       return std::tie(this->activity.time_start, this->activity.channel_start) < std::tie(other.activity.time_start, other.activity.channel_start);
    }

    void set_timestamp(uint64_t ts) // NOLINT(build/unsigned)
    {
      activity.time_start = ts;
    }

    uint64_t get_timestamp() const // NOLINT(build/unsigned)
    {
      return activity.time_start;
    }

    void set_first_timestamp(uint64_t ts) // NOLINT(build/unsigned)
    { 
      activity.time_start = ts;
    } 

    uint64_t get_first_timestamp() const // NOLINT(build/unsigned)
    {
      return activity.time_start;
    }


    //DUMMY
    size_t get_payload_size() { return 1; }

    size_t get_num_frames() { return 1; }

    size_t get_frame_size() { return get_payload_size(); }

    TAWrapper* begin()
    {
      return this;
    }
    
    TAWrapper* end()
    {
	    //DUMMY
      return (this+1);
    }

    static const constexpr daqdataformats::SourceID::Subsystem subsystem = daqdataformats::SourceID::Subsystem::kTrigger;
    static const constexpr daqdataformats::FragmentType fragment_type = daqdataformats::FragmentType::kTriggerActivity;
    // No idea what this should really be set to
    static const constexpr uint64_t expected_tick_difference = 1; // NOLINT(build/unsigned)

};

} // namespace trigger
} // namespace dunedaq
/*
namespace dunedaq {
namespace datahandlinglibs {

template<>
uint64_t
get_frame_iterator_timestamp(uint8_t* it)
{
  trgdataformats::TriggerActivity* activity = reinterpret_cast<trgdataformats::TriggerActivity*>(it);
  return activity->data.time_start;
}

}
}
*/
#endif // TRIGGER_INCLUDE_TRIGGER_TAWRAPPER_HPP_
