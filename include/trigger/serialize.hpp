/**
 * @file serialize.hpp
 *
 * This file contains the macro calls needed to allow the data
 * selection objects from triggeralgs to be serialized using the
 * dunedaq serialization package
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_INCLUDE_TRIGGER_SERIALIZE_HPP_
#define TRIGGER_INCLUDE_TRIGGER_SERIALIZE_HPP_

#include "serialization/Serialization.hpp"

#include "triggeralgs/TriggerActivity.hpp"
#include "triggeralgs/TriggerCandidate.hpp"
#include "triggeralgs/TriggerPrimitive.hpp"

DUNE_DAQ_SERIALIZE_NON_INTRUSIVE(triggeralgs,
                                 TriggerPrimitive,
                                 time_start,
                                 time_peak,
                                 time_over_threshold,
                                 channel,
                                 adc_integral,
                                 adc_peak,
                                 detid,
                                 type,
                                 algorithm,
                                 version,
                                 flag)

DUNE_DAQ_SERIALIZE_NON_INTRUSIVE(triggeralgs,
                                 TriggerActivity,
                                 time_start,
                                 time_end,
                                 time_peak,
                                 time_activity,
                                 channel_start,
                                 channel_end,
                                 channel_peak,
                                 adc_integral,
                                 adc_peak,
                                 detid,
                                 type,
                                 algorithm,
                                 version,
                                 tp_list)

MSGPACK_ADD_ENUM(triggeralgs::TriggerCandidateType)

DUNE_DAQ_SERIALIZE_NON_INTRUSIVE(triggeralgs,
                                 TriggerCandidate,
                                 time_start,
                                 time_end,
                                 time_candidate,
                                 detid,
                                 type,
                                 algorithm,
                                 version,
                                 ta_list)

#endif // TRIGGER_INCLUDE_TRIGGER_SERIALIZE_HPP_
