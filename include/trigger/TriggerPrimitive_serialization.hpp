/**
 * @file TriggerPrimitive_serialization.hpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_INCLUDE_TRIGGER_TRIGGERPRIMITIVE_SERIALIZATION_HPP_
#define TRIGGER_INCLUDE_TRIGGER_TRIGGERPRIMITIVE_SERIALIZATION_HPP_

#include "serialization/Serialization.hpp"
#include "trgdataformats/Types.hpp"
#include "triggeralgs/TriggerPrimitive.hpp"

#include "logging/Logging.hpp" // NOTE: if ISSUES ARE DECLARED BEFORE include logging/Logging.hpp, TLOG_DEBUG<<issue wont work.

#include <memory>
#include <vector>

namespace dunedaq {
// Disable coverage collection LCOV_EXCL_START
ERS_DECLARE_ISSUE(dfmessages,
                  CannotDeserializeTriggerPrimitive,
                  "Cannot deserialize TriggerPrimitive from JSON due to type mismatch", )
// Re-enable coverage collection LCOV_EXCL_STOP
} // namespace dunedaq

// MsgPack serialization functions (which just put the raw bytes of
// the fragment array into a MsgPack message)
namespace msgpack {
MSGPACK_API_VERSION_NAMESPACE(MSGPACK_DEFAULT_API_NS)
{
  namespace adaptor {

  template<>
  struct pack<dunedaq::trgdataformats::TriggerPrimitive>
  {
    template<typename Stream>
    packer<Stream>& operator()(msgpack::packer<Stream>& o, dunedaq::trgdataformats::TriggerPrimitive const& tp) const
    {
      o.pack_array(9); // Number of members
      o.pack(tp.version);
      o.pack(tp.flag);
      o.pack(tp.detid);
      o.pack(tp.channel);
      o.pack(tp.samples_over_threshold);
      o.pack(tp.time_start);
      o.pack(tp.samples_to_peak);
      o.pack(tp.adc_integral);
      o.pack(tp.adc_peak);
      return o;
    }
  };

  template<>
  struct convert<dunedaq::trgdataformats::TriggerPrimitive>
  {
    msgpack::object const& operator()(msgpack::object const& o, dunedaq::trgdataformats::TriggerPrimitive& tp) const
    {
      if (o.type != msgpack::type::ARRAY)
        throw msgpack::type_error();
      if (o.via.array.size != 9) // Number of members
        throw msgpack::type_error();

      tp.version = o.via.array.ptr[0].as<decltype(tp.version)>();
      tp.flag = o.via.array.ptr[1].as<decltype(tp.flag)>();
      tp.detid = o.via.array.ptr[2].as<decltype(tp.detid)>();
      tp.channel = o.via.array.ptr[3].as<decltype(tp.channel)>();
      tp.samples_over_threshold = o.via.array.ptr[4].as<decltype(tp.samples_over_threshold)>();
      tp.time_start = o.via.array.ptr[5].as<decltype(tp.time_start)>();
      tp.samples_to_peak = o.via.array.ptr[6].as<decltype(tp.samples_to_peak)>();
      tp.adc_integral = o.via.array.ptr[7].as<decltype(tp.adc_integral)>();
      tp.adc_peak = o.via.array.ptr[8].as<decltype(tp.adc_peak)>();
      return o;
    }
  };

  } // namespace adaptor
} // namespace MSGPACK_DEFAULT_API_NS
} // namespace msgpack

// nlohmann::json serialization function.
namespace nlohmann {
template<>
struct adl_serializer<dunedaq::trgdataformats::TriggerPrimitive>
{
  // note: the return type is no longer 'void', and the method only takes
  // one argument
  static dunedaq::trgdataformats::TriggerPrimitive from_json(const json& j)
  {
    dunedaq::trgdataformats::TriggerPrimitive tp;

    tp.version = j["version"];
    tp.flag = j["flag"];
    tp.detid = j["detid"];
    tp.channel = j["channel"];
    tp.samples_over_threshold = j["samples_over_threshold"];
    tp.time_start = j["time_start"];
    tp.samples_to_peak = j["samples_to_peak"];
    tp.adc_integral = j["adc_integral"];
    tp.adc_peak = j["adc_peak"];

    return tp;
  }

  static void to_json(json& j, const dunedaq::trgdataformats::TriggerPrimitive& tp)
  {
    j["version"] = tp.version;
    j["flag"] = tp.flag;
    j["detid"] = tp.detid;
    j["channel"] = tp.channel;
    j["samples_over_threshold"] = tp.samples_over_threshold;
    j["time_start"] = tp.time_start;
    j["samples_to_peak"] = tp.samples_to_peak;
    j["adc_integral"] = tp.adc_integral;
    j["adc_peak"] = tp.adc_peak;
  }
};
} // namespace nlohmann

DUNE_DAQ_SERIALIZABLE(dunedaq::trgdataformats::TriggerPrimitive, "TriggerPrimitive");

#endif // TRIGGER_INCLUDE_TRIGGER_TRIGGERPRIMITIVE_SERIALIZATION_HPP_
