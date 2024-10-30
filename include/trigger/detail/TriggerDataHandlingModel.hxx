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
std::unique_ptr<RDT[]> TriggerDataHandlingModel<RDT, RHT, LBT, RPT, IDT>::transform_payload(IDT& original, std::size_t& size) const
{
  if constexpr (std::is_same_v<IDT, trigger::TPSet>) {
    size = original.objects.size();
    auto transformed = std::make_unique_for_overwrite<RDT[]>(size);
    for (std::size_t i = 0; i < size; ++i) {
      transformed[i].tp = std::move(original.objects[i]);
    }
    return transformed;
  } else if constexpr (std::is_same_v<IDT, TriggerPrimitiveTypeAdapter::TPAArrayPair>) {
    size = original.second;
    return std::move(original.first);
  } else if constexpr (std::is_same_v<IDT, triggeralgs::TriggerActivity> || std::is_same_v<IDT, triggeralgs::TriggerCandidate>) {
    size = 1;
    auto transformed = std::make_unique_for_overwrite<RDT[]>(size);
    transformed[0] = RDT(std::move(original));
    return transformed;
  } else {
    return Base::transform_payload(original, size);
  }
}

} // namespace dunedaq::trigger
