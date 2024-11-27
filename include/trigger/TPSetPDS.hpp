/**
 * @file TPSet.hpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_INCLUDE_TRIGGER_TPPDSSET_HPP_
#define TRIGGER_INCLUDE_TRIGGER_TPPDSSET_HPP_

#include "dfmessages/SourceID_serialization.hpp"
#include "serialization/Serialization.hpp"
#include "trigger/Set.hpp"
#include "trigger/TriggerPrimitive_serialization.hpp"
#include "trgdataformats/TriggerPrimitivePDS.hpp"

namespace dunedaq::trigger {

using TPSet = Set<trgdataformats::TriggerPrimitivePDS>;

} // namespace dunedaq::trigger

MSGPACK_ADD_ENUM(dunedaq::trigger::TPSet::Type)
DUNE_DAQ_SERIALIZE_NON_INTRUSIVE(dunedaq::trigger, TPSet, seqno, run_number, origin, type, start_time, end_time, objects)

#endif // TRIGGER_INCLUDE_TRIGGER_TPPDSSET_HPP_
