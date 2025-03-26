/**
 * @file taset_serialization.cxx Test TASet serialization
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "serialization/Serialization.hpp"
#include "trigger/TASet.hpp"

#include <chrono>
#include <iostream>
#include <random>
#include <vector>

int
main()
{
  dunedaq::trigger::TASet taset;
  // NOLINTNEXTLINE(build/unsigned)
  std::vector<uint8_t> bytes = dunedaq::serialization::serialize(taset, dunedaq::serialization::kMsgPack);
  dunedaq::trigger::TASet set_recv = dunedaq::serialization::deserialize<dunedaq::trigger::TASet>(bytes);
}
