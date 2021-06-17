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

BufferManager::BufferManager(long unsigned int buffer_size)
  : m_buffer_max_size(buffer_size)
{

}

BufferManager::~BufferManager()
{

}

bool
BufferManager::add(trigger::TPSet& tps)
{
  if(m_tpset_buffer.size() >= m_buffer_max_size) //delete oldest TPSet if buffer full
  {
    auto firstIt = m_tpset_buffer.begin();
    m_tpset_buffer.erase(firstIt);
    // add some warning message here?
  }
  return m_tpset_buffer.insert(tps).second; //false if tps with same start_time already exists
}

std::vector<trigger::TPSet>
BufferManager::get_tpsets_in_window(dataformats::timestamp_t start_time, dataformats::timestamp_t end_time)
{
  std::vector<trigger::TPSet> tpsets_output;

  for(auto& tps: m_tpset_buffer)
  {
    if( ( (tps.start_time > start_time) && (tps.end_time   < end_time) ) ||   //condition (1)
	( (tps.end_time   > start_time) && (tps.end_time   < end_time) ) ||   //condition (2)
	( (tps.start_time > start_time) && (tps.start_time < end_time) )    ) //condition (3)
    {
      tpsets_output.push_back(tps);
    }
  }

  return tpsets_output;


  /*
   Conditions:

   (1) TPSet starts and ends withing the requested window:

            start_time                    end_time
                      |--TPSet-------|


   (2) TPSet starts after start_time but finishes after end_time:

            start_time                    end_time
                      |--TPSet-----------------------------|


   (3) TPSet starts before start_time but finishes before end_time:

            start_time                    end_time
   |--TPSet-------------------------|


   (4) TPSet transfer to buffer is delayed

  */

}


} // namespace dunedaq::trigger
