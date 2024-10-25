/**
 * @file TriggerDataHandlingModel.hpp Glue between data source, payload raw processor,
 * latency buffer and request handler.
 *
 * This is part of the DUNE DAQ, copyright 2024.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef TRIGGER_INCLUDE_TRIGGER_TRIGGERDATAHANDLINGMODEL_HPP_
#define TRIGGER_INCLUDE_TRIGGER_TRIGGERDATAHANDLINGMODEL_HPP_

#include "datahandlinglibs/models/DataHandlingModel.hpp"
#include <vector>

namespace dunedaq::trigger {

template<class ReadoutType,
         class RequestHandlerType,
         class LatencyBufferType,
         class RawDataProcessorType,
         class InputDataType>
class TriggerDataHandlingModel
  : public datahandlinglibs::
      DataHandlingModel<ReadoutType, RequestHandlerType, LatencyBufferType, RawDataProcessorType, InputDataType>
{
public:
  using Base = datahandlinglibs::
    DataHandlingModel<ReadoutType, RequestHandlerType, LatencyBufferType, RawDataProcessorType, InputDataType>;
  using RDT = typename Base::RDT;
  using RHT = typename Base::RHT;
  using LBT = typename Base::LBT;
  using RPT = typename Base::RPT;
  using IDT = typename Base::IDT;

  explicit TriggerDataHandlingModel(std::atomic<bool>& run_marker);

  // Transform input data type to readout
  std::unique_ptr<ReadoutType[]> transform_payload(IDT& original, std::size_t& size) const override;
};

} // namespace dunedaq::trigger

// Declarations
#include "detail/TriggerDataHandlingModel.hxx"

#endif // TRIGGER_INCLUDE_TRIGGER_TRIGGERDATAHANDLINGMODEL_HPP_
