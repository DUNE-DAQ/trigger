/**
 * @file TASet.hpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_INCLUDE_TRIGGER_TASET_HPP_
#define TRIGGER_INCLUDE_TRIGGER_TASET_HPP_

#include "trigger/Set.hpp"
#include "trigger/TriggerActivity_serialization.hpp"
#include "triggeralgs/TriggerActivity.hpp"
#include "serialization/Serialization.hpp"

namespace dunedaq::trigger {

using TASet = Set<triggeralgs::TriggerActivity>;

} // namespace dunedaq::trigger

MSGPACK_ADD_ENUM(dunedaq::trigger::TASet::Type)
DUNE_DAQ_SERIALIZE_NON_INTRUSIVE(dunedaq::trigger, TASet, seqno, type, from_detids, start_time, end_time, objects)

#endif // TRIGGER_INCLUDE_TRIGGER_TASET_HPP_
