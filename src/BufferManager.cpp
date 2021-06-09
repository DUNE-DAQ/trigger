/**
 * @file BufferManager.cpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2021.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#include "trigger/BufferManager.hpp"

#include <iostream>

namespace dunedaq::trigger {

BufferManager::BufferManager()
{

}

BufferManager::~BufferManager()
{

}

bool
BufferManager::add(trigger::TPSet& tps)
{
  // dummy function to avoid compilation warning.
  trigger::TPSet temp = tps;
  bool dummy = false;

  return dummy;
}

std::vector<trigger::TPSet>
BufferManager::get_tpsets_in_window(dataformats::timestamp_t start_time, dataformats::timestamp_t end_time)
{
  // dummy function to avoid compilation warning.
  std::vector<trigger::TPSet> dummy;
  if(end_time > start_time){}

  return dummy;

    
}


} // namespace dunedaq::trigger
