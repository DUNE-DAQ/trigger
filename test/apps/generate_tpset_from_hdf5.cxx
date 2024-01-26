/**
 * @file generate_tpset_from_hdf5.cxx Read TP fragments from file and generate a TPSet vector.
 * Matches code within TriggerPrimitiveMaker.
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#include "CLI/CLI.hpp"

#include "../../plugins/TriggerPrimitiveMaker.hpp"
#include "trgdataformats/TriggerPrimitive.hpp"
#include "hdf5libs/HDF5RawDataFile.hpp"

#include "daqdataformats/Fragment.hpp"
#include "daqdataformats/FragmentHeader.hpp"
#include "daqdataformats/TriggerRecordHeader.hpp"
#include "daqdataformats/Types.hpp"

#include <iostream>

int main(int argc, char** argv) {
  CLI::App app{ "App description" };

  std::string filename;
  app.add_option("-f,--file", filename, "Input HDF5 file");

  CLI11_PARSE(app, argc, argv);

  dunedaq::trigger::TPSet tpset;
  std::vector<dunedaq::trigger::TPSet> tpsets;

  uint64_t prev_tpset_number = 0; // NOLINT(build/unsigned)
  uint32_t seqno = 0;             // NOLINT(build/unsigned)
  uint64_t old_time_start = 0;    // NOLINT(build/unsigned)
  uint32_t tpset_time_width = 10; // Arbitrary
  uint32_t tpset_time_offset = 5; // Arbitrary
  uint16_t element = 0;           // Arbitrary

  // Prepare input file
  std::unique_ptr<dunedaq::hdf5libs::HDF5RawDataFile> input_file = std::make_unique<dunedaq::hdf5libs::HDF5RawDataFile>(filename);

  // Check that the file is a TimeSlice type
  if (!input_file->is_timeslice_type()) {
    std::cout << "Not a timeslice type.\n";
    return 1;
  }

  std::vector<std::string> fragment_paths = input_file->get_all_fragment_dataset_paths();

  // Read in the file and place the TPs in TPSets. TPSets have time
  // boundaries ( n*tpset_time_width + tpset_time_offset ), and TPs are placed
  // in TPSets based on the TP start time
  //
  // This loop assumes the input file is sorted by TP start time
  for (std::string& fragment_path : fragment_paths) {
    std::unique_ptr<dunedaq::daqdataformats::Fragment> frag = input_file->get_frag_ptr(fragment_path);
    // Make sure this fragment is a TriggerPrimitive
    if (frag->get_fragment_type() != dunedaq::daqdataformats::FragmentType::kTriggerPrimitive) continue;

    // Prepare TP buffer
    size_t num_tps = frag->get_data_size() / sizeof(dunedaq::trgdataformats::TriggerPrimitive);

    dunedaq::trgdataformats::TriggerPrimitive* tp_array = static_cast<dunedaq::trgdataformats::TriggerPrimitive*>(frag->get_data());

    for (size_t i(0); i < num_tps; i++) {
      auto& tp = tp_array[i];
      if (tp.time_start < old_time_start) {
        std::cout << "TPs are unsorted.\n";
        return 1;
      }
      // NOLINTNEXTLINE(build/unsigned)
      uint64_t current_tpset_number = (tp.time_start + tpset_time_offset) / tpset_time_width;
      old_time_start = tp.time_start;

      // If we crossed a time boundary, push the current TPSet and reset it
      if (current_tpset_number > prev_tpset_number) {
        tpset.start_time = prev_tpset_number * tpset_time_width + tpset_time_offset;
        tpset.end_time = tpset.start_time + tpset_time_width;
        tpset.seqno = seqno;
        ++seqno;

        // 12-Jul-2021, KAB: setting origin fields from configuration
        tpset.origin.id = element;

        tpset.type = dunedaq::trigger::TPSet::Type::kPayload;

        if (!tpset.objects.empty()) {
          // We don't send empty TPSets, so there's no point creating them
          tpsets.push_back(tpset);
        }
        prev_tpset_number = current_tpset_number;

        tpset.objects.clear();
      }
      tpset.objects.push_back(tp);
    }
  }
  if (!tpset.objects.empty()) {
    // We don't send empty TPSets, so there's no point creating them
    tpsets.push_back(tpset);
  }
  std::cout << "Read " << seqno << " TPs into " << tpsets.size() << " TPSets, from file " << filename << std::endl;

  return 0;
}
