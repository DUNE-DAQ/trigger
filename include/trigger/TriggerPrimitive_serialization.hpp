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

DUNE_DAQ_SERIALIZE_NON_INTRUSIVE(dunedaq::trgdataformats,
                                 TriggerPrimitive,
                                 version,
                                 flag,
                                 detid,
                                 channel,
                                 samples_over_threshold,
                                 time_start,
                                 samples_to_peak,
                                 adc_integral,
                                 adc_peak);

#endif // TRIGGER_INCLUDE_TRIGGER_TRIGGERPRIMITIVE_SERIALIZATION_HPP_
