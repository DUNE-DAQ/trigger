/**
 * @file check_fragment_TPs.cxx Read TP fragments from file and check that they have start times within the request
 * window
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#include "CLI/CLI.hpp"

#include "rawdatafilelibs/BackendFactory.hpp"
#include "rawdatafilelibs/RawDataFileReader.hpp"
#include "trgdataformats/TriggerPrimitive.hpp"

#include <daqdataformats/Fragment.hpp>
#include <daqdataformats/FragmentHeader.hpp>
#include <daqdataformats/TriggerRecordHeader.hpp>
#include <daqdataformats/Types.hpp>
#include <iostream>

int
main(int argc, char** argv)
{
  CLI::App app{ "App description" };

  std::string filename;
  app.add_option("-f,--file", filename, "Input HDF5 file");

  CLI11_PARSE(app, argc, argv);

  auto input_file = dunedaq::rawdatafilelibs::open_raw_data_file(filename);

  for (auto const& record_id : input_file->get_record_ids()) {
    auto trigger_number = record_id.number;

    auto header = input_file->get_trigger_record_header(record_id);
    std::cout << "Trigger record " << trigger_number << " has type 0x" << std::hex << header->get_trigger_type()
              << std::dec << std::endl;
  }
}
