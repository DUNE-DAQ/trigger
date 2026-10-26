/**
 * @file streamed_TPs_to_text.cxx Dump streamed TPs from an HDF5 into the text format used by trigger
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#include "CLI/CLI.hpp"

#include "rawdatafilelibs/BackendFactory.hpp"
#include "rawdatafilelibs/RawDataFileReader.hpp"
#include "trgdataformats/TriggerObjectOverlay.hpp"
#include "trgdataformats/TriggerPrimitive.hpp"

#include <daqdataformats/Fragment.hpp>
#include <daqdataformats/SourceID.hpp>
#include <daqdataformats/TimeSlice.hpp>
#include <daqdataformats/TimeSliceHeader.hpp>
#include <daqdataformats/Types.hpp>

#include <iostream>

using dunedaq::trgdataformats::TriggerActivity;
using dunedaq::trgdataformats::TriggerActivityData;
using dunedaq::trgdataformats::TriggerCandidate;
using dunedaq::trgdataformats::TriggerPrimitive;

void
print_tp(const TriggerPrimitive& prim, size_t offset = 0)
{
  for (size_t i = 0; i < offset; ++i)
    std::cout << "\t";
  std::cout << "\t" << prim.time_start << "\t" << prim.samples_over_threshold << "\t" << prim.samples_to_peak << "\t"
            << prim.channel << "\t" << prim.adc_integral << "\t" << prim.adc_peak << "\t" << prim.detid << "\t"
            << std::endl;
}

void
print_tps(std::unique_ptr<dunedaq::daqdataformats::Fragment>&& frag, size_t offset = 0)
{
  size_t payload_size = frag->get_size() - sizeof(dunedaq::daqdataformats::FragmentHeader);
  size_t n_tps = payload_size / sizeof(TriggerPrimitive);
  size_t remainder = payload_size % sizeof(TriggerPrimitive);
  assert(remainder == 0);
  const TriggerPrimitive* prim = reinterpret_cast<TriggerPrimitive*>(frag->get_data());
  std::cout << "Trigger primitives for SourceID [" << frag->get_element_id() << "]" << std::endl;
  for (size_t i = 0; i < n_tps; ++i) {
    print_tp(*prim, offset);
    ++prim;
  }
}

void
print_ta(const TriggerActivity& activity)
{
  std::cout << "\t" << activity.data.time_start << "\t" << activity.data.time_end << "\t" << activity.data.channel_start
            << "\t" << activity.data.channel_end << std::endl;
  std::cout << "\tInput TPs:" << std::endl;
  for (size_t i = 0; i < activity.n_inputs; ++i) {
    const TriggerPrimitive& prim = activity.inputs[i];
    print_tp(prim, 1);
  }
  std::cout << std::endl;
}

void
print_tas(std::unique_ptr<dunedaq::daqdataformats::Fragment>&& frag)
{
  size_t payload_size = frag->get_size() - sizeof(dunedaq::daqdataformats::FragmentHeader);

  std::cout << "Trigger activities for SourceID [" << frag->get_element_id() << "]" << std::endl;
  // The fragment contains a number of variable-sized TAs stored
  // contiguously so we can't calculate the number of TAs a priori. We
  // have to do this pointer arithmetic business while looping over
  // the items in the fragment
  char* buffer = static_cast<char*>(frag->get_data());
  size_t offset = 0;
  const TriggerActivity* activity = reinterpret_cast<TriggerActivity*>(buffer);
  while (offset < payload_size) {
    print_ta(*activity);
    offset += sizeof(TriggerActivity) + activity->n_inputs * sizeof(TriggerActivity::input_t);
    activity = reinterpret_cast<TriggerActivity*>(buffer + offset);
  }
}

void
print_ta_data(const dunedaq::trgdataformats::TriggerActivityData& ta_data)
{
  std::cout << "\t\t" << ta_data.time_start << "\t" << ta_data.time_end << "\t" << ta_data.channel_start << "\t"
            << ta_data.channel_end << std::endl;
}

void
print_tc(const TriggerCandidate& candidate)
{
  std::cout << "\t" << candidate.data.time_start << "\t" << candidate.data.time_end << std::endl;
  std::cout << "\tInput TAs:" << std::endl;
  for (size_t i = 0; i < candidate.n_inputs; ++i) {
    const TriggerActivityData& activity_data = candidate.inputs[i];
    print_ta_data(activity_data);
  }
  std::cout << std::endl;
}

void
print_tcs(std::unique_ptr<dunedaq::daqdataformats::Fragment>&& frag)
{
  size_t payload_size = frag->get_size() - sizeof(dunedaq::daqdataformats::FragmentHeader);

  std::cout << "Trigger candidates for SourceID [" << frag->get_element_id() << "]" << std::endl;
  // The fragment contains a number of variable-sized TCs stored
  // contiguously so we can't calculate the number of TCs a priori. We
  // have to do this pointer arithmetic business while looping over
  // the items in the fragment
  char* buffer = static_cast<char*>(frag->get_data());
  size_t offset = 0;
  const TriggerCandidate* candidate = reinterpret_cast<TriggerCandidate*>(buffer);
  while (offset < payload_size) {
    print_tc(*candidate);
    offset += sizeof(TriggerCandidate) + candidate->n_inputs * sizeof(TriggerCandidate::input_t);
    candidate = reinterpret_cast<TriggerCandidate*>(buffer + offset);
  }
}

int
main(int argc, char** argv)
{
  CLI::App app{ "Print DS fragments from an HDF5 file" };

  std::string in_filename;
  app.add_option("-i,--input", in_filename, "Input HDF5 file");

  CLI11_PARSE(app, argc, argv);

  auto input_file = dunedaq::rawdatafilelibs::open_raw_data_file(in_filename);
  if (input_file->get_record_type() != dunedaq::rawdatafilelibs::RecordType::kTriggerRecord) {
    std::cout << "Not a TriggerRecord file." << std::endl;
    return 1;
  }

  dunedaq::daqdataformats::SourceIDSelector trigger_fragments;
  trigger_fragments.subsystem = dunedaq::daqdataformats::SourceID::Subsystem::kTrigger;
  for (auto const& record_id : input_file->get_record_ids()) {
    std::cout << "-----------------------------------------------------------------------------------" << std::endl;
    std::cout << "Trigger record " << record_id.number << std::endl;
    for (auto const& source_id : input_file->get_fragment_source_ids(record_id, trigger_fragments)) {
      auto frag_ptr = input_file->get_fragment(record_id, source_id);
      // (else-if: each print function takes the fragment, which can't be looked at afterwards)
      auto fragment_type = frag_ptr->get_fragment_type();
      if (fragment_type == dunedaq::daqdataformats::FragmentType::kTriggerPrimitive) {
        print_tps(std::move(frag_ptr));
      } else if (fragment_type == dunedaq::daqdataformats::FragmentType::kTriggerActivity) {
        print_tas(std::move(frag_ptr));
      } else if (fragment_type == dunedaq::daqdataformats::FragmentType::kTriggerCandidate) {
        print_tcs(std::move(frag_ptr));
      }
    }
    std::cout << std::endl;
  }
}
