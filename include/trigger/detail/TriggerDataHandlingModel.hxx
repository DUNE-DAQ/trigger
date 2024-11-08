/**
 * @file TriggerDataHandlingModel.hxx Glue between data source, payload raw processor,
 * latency buffer and request handler.
 *
 * This is part of the DUNE DAQ, copyright 2024.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#include "triggeralgs/TriggerActivity.hpp"
#include "triggeralgs/TriggerCandidate.hpp"

namespace dunedaq::trigger {

template<class RDT, class RHT, class LBT, class RPT, class IDT>
TriggerDataHandlingModel<RDT, RHT, LBT, RPT, IDT>::TriggerDataHandlingModel(std::atomic<bool>& run_marker)
  : Base(run_marker)
{
}

template<class RDT, class RHT, class LBT, class RPT, class IDT>
std::vector<RDT>
TriggerDataHandlingModel<RDT, RHT, LBT, RPT, IDT>::transform_payload(IDT& original) const
{
  if constexpr (std::is_same_v<IDT, trigger::TPSet>) {
    std::vector<RDT> transformed(original.objects.size());
    for (std::size_t i = 0; i < transformed.size(); ++i) {
      transformed[i].tp = std::move(original.objects[i]);
    }
    return transformed;
  } else if constexpr (std::is_same_v<IDT, std::vector<TriggerPrimitiveTypeAdapter>>) {
    return std::move(original);
  } else if constexpr (std::is_same_v<IDT, triggeralgs::TriggerActivity> ||
                       std::is_same_v<IDT, triggeralgs::TriggerCandidate>) {
    return { RDT(std::move(original)) };

  } else {
    return Base::transform_payload(original);
  }
}

} // namespace dunedaq::trigger
