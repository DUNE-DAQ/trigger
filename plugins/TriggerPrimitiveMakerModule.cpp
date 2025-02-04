/**
 * @file TriggerPrimitiveMakerModule.cpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "TriggerPrimitiveMakerModule.hpp"

#include "trigger/Issues.hpp" // For TLVL_*
#include "trigger/TriggerPrimitiveTypeAdapter.hpp"

// #include "appfwk/cmd/Nljs.hpp"
#include "iomanager/IOManager.hpp"
#include "logging/Logging.hpp"
#include "rcif/cmd/Nljs.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace triggeralgs;

namespace dunedaq::trigger {

TriggerPrimitiveMakerModule::TriggerPrimitiveMakerModule(const std::string& name)
  : DAQModule(name)
  , m_queue_timeout(100)
{
  // clang-format off
  register_command("conf",  &TriggerPrimitiveMakerModule::do_configure);
  register_command("start", &TriggerPrimitiveMakerModule::do_start);
  register_command("stop_trigger_sources",  &TriggerPrimitiveMakerModule::do_stop);
  register_command("scrap", &TriggerPrimitiveMakerModule::do_scrap);
  // clang-format on
}
void
TriggerPrimitiveMakerModule::init(std::shared_ptr<appfwk::ModuleConfiguration> mcfg)
{
  auto mtrg = mcfg->module<appmodel::TriggerPrimitiveMakerModule>(get_name());
  m_conf = mtrg->get_configuration();
  clocks_per_us =
    mcfg->configuration_manager()->session()->get_detector_configuration()->get_clock_speed_hz() / 1'000'000;

  // Get channel map
  m_channel_map_name = m_conf->get_channel_map();
  TLOG() << "### REPLAY CONFIGURATION ###";
  TLOG() << "Will use channel map: " << m_channel_map_name;
  try {
    m_channel_map = dunedaq::detchannelmaps::make_map(m_channel_map_name);
  } catch (const detchannelmaps::ChannelMapCreationFailed& e) {
    ers::error(dunedaq::trigger::ReplayChannelMapProblem(ERS_HERE, get_name(), m_channel_map_name));
  }

  m_loops = m_conf->get_number_of_loops();

  // Plane filtering
  for (auto& plane_wrap : m_conf->get_filter_out_plane()) {
    m_filter_planes_ids.push_back(plane_wrap->get_plane());
  }
  m_filter_planes = (m_filter_planes_ids.size() > 0) ? true : false;

  TLOG() << "Plane filtering: " << m_filter_planes;
  if (m_filter_planes) {
    TLOG() << "Planes to filter: ";
    for (auto plane : m_filter_planes_ids) {
      TLOG() << plane;
    }
  }

  // Find planes to use
  if (m_filter_planes) {
    for (int plane = 0; plane < 3; plane++) {
      if (std::find(m_filter_planes_ids.begin(), m_filter_planes_ids.end(), plane) == m_filter_planes_ids.end()) {
        m_planes_to_use.push_back(plane);
      }
    }
  } else {
    m_planes_to_use = { 0, 1, 2 };
  }

  // For each of the streams that are specified in the config, we read
  // the input file, and create an outgoing sink. We also keep track
  // of the total timestamp range of all the streams, so we can keep
  // the timestamps of the multiple streams in sync when replaying,
  // even when they don't all start or end at the same time

  auto con = mtrg->get_outputs();

  m_earliest_first_tp_timestamp = std::numeric_limits<triggeralgs::timestamp_t>::max();
  m_latest_last_tp_timestamp = 0;

  // Map to group files by ROU
  // because this is the first time looping over files
  // the function also checks the file
  std::map<std::string, std::vector<std::string>> grouped_files;
  for (auto& stream : m_conf->get_tp_streams()) {
    std::string rou = extract_readout_unit(stream->get_filename());
    grouped_files[rou].push_back(stream->get_filename());
  }

  if (grouped_files.empty()) {
    ers::error(dunedaq::trigger::ReplayNoValidFiles(ERS_HERE, get_name()));
  }

  // Sort each vector in grouped_files (time ordering)
  for (auto& [rou, files] : grouped_files) {
    // Sort each vector of filenames by run number and bit using a custom comparator
    std::sort(files.begin(), files.end(), [this](const std::string& a, const std::string& b) {
      // Capture 'this' to access the member function
      auto [run_a, bit_a] = this->extract_run_and_bit(a); // Call the member function with 'this'
      auto [run_b, bit_b] = this->extract_run_and_bit(b);

      // First compare by run number, then by bit
      if (run_a != run_b)
        return run_a < run_b;
      return bit_a < bit_b;
    });
  }

  // Print grouped files
  TLOG() << "Files to use:";
  for (const auto& entry : grouped_files) {
    TLOG() << "ROU: " << entry.first << "\nFiles:\n";
    for (const auto& file : entry.second) {
      TLOG() << "  " << file << "\n";
    }
  }

  int iter = 0;
  // loop over ROUs
  for (auto it = grouped_files.begin(); it != grouped_files.end(); ++it) {
    std::map<int, std::vector<std::vector<TriggerPrimitiveTypeAdapter>>> tps_data = read_tps(it->second, it->first);
    // loop over planes
    int plane_iter = 0;
    for (auto plane : m_planes_to_use) {

      // Check that there is data for this ROU / plane
      auto it2 = tps_data.find(plane);
      if (it2 == tps_data.end() || it2->second.empty()) {
        // key doesn't exist or no data
        plane_iter++;
        continue;
      }

      TPStream this_stream;
      TLOG() << "Stream: " << (iter + plane_iter) << "; ROU: " << it->first << "; plane: " << plane << "; TP sink is "
             << con[iter + plane_iter]->class_name() << "@" << con[iter + plane_iter]->UID()
             << "; first file: " << it->second[0];
      this_stream.tp_sink =
        get_iom_sender<std::vector<trigger::TriggerPrimitiveTypeAdapter>>(con[iter + plane_iter]->UID());

      this_stream.tpvs = tps_data[plane];

      m_earliest_first_tp_timestamp =
        std::min(m_earliest_first_tp_timestamp, this_stream.tpvs.front().front().tp.time_start);

      m_latest_last_tp_timestamp = std::max(m_latest_last_tp_timestamp, this_stream.tpvs.back().back().tp.time_start);

      m_tp_streams.push_back(std::move(this_stream));
      plane_iter++;
    }
    iter = iter + m_planes_to_use.size();
  }
  TLOG() << "Total of " << m_tp_streams.size() << " TP streams.";
}

void
TriggerPrimitiveMakerModule::do_configure(const nlohmann::json& /*obj*/)
{
}

void
TriggerPrimitiveMakerModule::do_start(const nlohmann::json& args)
{
  TLOG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Entering do_start() method";

  rcif::cmd::StartParams start_params = args.get<rcif::cmd::StartParams>();
  m_run_number = start_params.run;

  m_running_flag.store(true);

  // Reset opmon
  m_tp_made_count.store(0);
  m_tpv_made_count.store(0);
  m_tpv_failed_sent_count.store(0);

  // We need the wall-clock time at which we'll send out the TPs
  // with the earliest timestamp, so we can keep all of the output
  // streams in sync. We pick "now" plus a bit, to allow time for all
  // of the threads to start up
  auto earliest_timestamp_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
  m_run_start_time = std::chrono::steady_clock::now();

  for (auto& stream : m_tp_streams) {
    m_threads.push_back(std::make_unique<std::thread>(&TriggerPrimitiveMakerModule::do_work,
                                                      this,
                                                      std::ref(m_running_flag),
                                                      std::ref(stream.tpvs),
                                                      std::ref(stream.tp_sink),
                                                      earliest_timestamp_time));
  }

  for (size_t i = 0; i < m_threads.size(); i++) {
    std::string name("replay-");
    name += std::to_string(i);
    pthread_setname_np(m_threads[i]->native_handle(), name.c_str());
  }
  TLOG() << "Total of " << m_threads.size() << " replay threads.";
  TLOG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Exiting do_start() method";
}

void
TriggerPrimitiveMakerModule::do_stop(const nlohmann::json& /*args*/)
{
  TLOG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Entering do_stop() method";
  m_running_flag.store(false);
  for (auto& thr : m_threads) {
    if (thr != nullptr && thr->joinable()) {
      thr->join();
    }
  }
  m_threads.clear();

  auto run_end_time = std::chrono::steady_clock::now();
  auto time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(run_end_time - m_run_start_time).count();
  float rate_hz = 1e3 * static_cast<float>(m_tpv_made_count) / time_ms;

  TLOG() << "TOTAL: Generated " << m_tpv_made_count << " TP vectors (" << m_tp_made_count << " TPs) in " << time_ms
         << " ms. (" << rate_hz << " TP vectors/s). " << m_tpv_failed_sent_count << " TP vectors failed to push.";

  TLOG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Exiting do_stop() method";
}

void
TriggerPrimitiveMakerModule::do_scrap(const nlohmann::json& /*args*/)
{
  TLOG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Entering do_scrap() method";
  m_tp_streams.clear();
  TLOG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Exiting do_scrap() method";
}

void
TriggerPrimitiveMakerModule::generate_opmon_data()
{
  opmon::TriggerPrimitiveMakerInfo info;

  info.set_tp_made_count(m_tp_made_count);
  info.set_tp_set_made_count(m_tpv_made_count);
  info.set_tp_set_failed_sent_count(m_tpv_failed_sent_count);

  this->publish(std::move(info));
}

std::map<int, std::vector<std::vector<TriggerPrimitiveTypeAdapter>>>
TriggerPrimitiveMakerModule::read_tps(std::vector<std::string> filenames, std::string rou)
{
  std::map<int, std::vector<std::vector<TriggerPrimitiveTypeAdapter>>> all_tpvs;
  int tps_counter = 0;
  int vectors_counter = 0;

  // Loop over files for this ROU
  for (const auto& a_file : filenames) {

    // Prepare input file
    std::unique_ptr<hdf5libs::HDF5RawDataFile> input_file;
    std::string filename = a_file;
    input_file = std::make_unique<hdf5libs::HDF5RawDataFile>(filename);
    std::vector<std::string> fragment_paths = input_file->get_all_fragment_dataset_paths();
    std::map<int, std::vector<std::string>> frags_by_plane;
    // sort fragments by plane
    for (const auto& path : fragment_paths) {
      int plane = extract_plane_number(path);

      // hack for APA1, basically making plane 1 collection plane :/
      if (rou == "APA_P02SU") {
        if (plane == 1) {
          plane = 2;
        } else if (plane == 2) {
          plane = 1;
        }
      }
      // Check if plane is in m_filter_planes_ids
      if (std::find(m_filter_planes_ids.begin(), m_filter_planes_ids.end(), plane) != m_filter_planes_ids.end()) {
        continue; // Skip this fragment if plane is in m_filter_planes_ids
      }
      frags_by_plane[plane].push_back(path);
    }

    // plane by plane
    for (auto plane : m_planes_to_use) {
      if (frags_by_plane[plane].size() == 0) {
        ers::error(dunedaq::trigger::ReplayNoDataAfterFilter(ERS_HERE, get_name(), filename, plane));
        continue;
      }

      TLOG() << "Will use " << frags_by_plane[plane].size() << " fragments for ROU: " << rou << ", plane: " << plane
             << ".";

      std::vector<std::vector<TriggerPrimitiveTypeAdapter>> this_plane_tpvs;
      int local_tps_counter = 0;
      int local_vectors_counter = 0;

      // Read in the file, convert TPs to TPTypeAdapters and place them in vector.
      // This loop assumes the input file is sorted by TP start time
      for (std::string& fragment_path : frags_by_plane[plane]) {
        std::unique_ptr<daqdataformats::Fragment> frag = input_file->get_frag_ptr(fragment_path);
        // Make sure this fragment is a TriggerPrimitive
        if (frag->get_fragment_type() != daqdataformats::FragmentType::kTriggerPrimitive)
          continue;
        if (frag->get_element_id().subsystem != daqdataformats::SourceID::Subsystem::kTrigger)
          continue;

        // Prepare TP buffer
        size_t num_tps = frag->get_data_size() / sizeof(trgdataformats::TriggerPrimitive);

        trgdataformats::TriggerPrimitive* tp_array = static_cast<trgdataformats::TriggerPrimitive*>(frag->get_data());

        std::vector<TriggerPrimitiveTypeAdapter> tps;
        for (size_t i(0); i < num_tps; i++) {
          trigger::TriggerPrimitiveTypeAdapter tpa;
          tpa.tp = tp_array[i];
          tps.push_back(std::move(tpa));
          local_tps_counter++;
        }
        if (tps.size() > 0) {
          this_plane_tpvs.push_back(std::move(tps));
          local_vectors_counter++;
        }

        frag.reset();
      }

      tps_counter += local_tps_counter;
      vectors_counter += local_vectors_counter;

      // Final check for orderliness
      // Sort the outer vector using stable_sort, comparing based on the time_start of the first element of each inner
      // vector
      std::stable_sort(
        this_plane_tpvs.begin(),
        this_plane_tpvs.end(),
        [](const std::vector<TriggerPrimitiveTypeAdapter>& a, const std::vector<TriggerPrimitiveTypeAdapter>& b) {
          return a.front().tp.time_start < b.front().tp.time_start;
        });

      // check for empty vector here
      if (this_plane_tpvs.size() == 0) {
        ers::error(dunedaq::trigger::ReplayNoValidTPs(ERS_HERE, get_name(), filename));
      }
      TLOG() << "Read " << local_tps_counter << " TPs, stored in " << local_vectors_counter << " vectors, from file "
             << filename << ", ROU: " << rou << ", plane: " << plane << ".";

      if (all_tpvs.find(plane) != all_tpvs.end()) {
        all_tpvs[plane].insert(all_tpvs[plane].end(),
                               std::make_move_iterator(this_plane_tpvs.begin()),
                               std::make_move_iterator(this_plane_tpvs.end()));
      } else {
        // If the key doesn't exist, add the new vector as a new entry
        all_tpvs[plane] = std::move(this_plane_tpvs);
      }

      this_plane_tpvs.clear();

    } // plane loop
  }   // file loop

  TLOG() << "Done with all files for this ROU (" << rou << "). Total of " << tps_counter << " TPs, stored in "
         << vectors_counter << " vectors, using " << m_planes_to_use.size() << " planes.";
  return all_tpvs;
}

void
TriggerPrimitiveMakerModule::do_work(
  std::atomic<bool>& running_flag,
  std::vector<std::vector<TriggerPrimitiveTypeAdapter>>& tpvs,
  std::shared_ptr<iomanager::SenderConcept<std::vector<trigger::TriggerPrimitiveTypeAdapter>>>& tp_sink,
  std::chrono::steady_clock::time_point earliest_timestamp_time)
{
  TLOG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Entering do_work() method";
  int current_iteration = 0; // NOLINT(build/unsigned)

  uint64_t prev_tpv_start_time = 0; // NOLINT(build/unsigned)
  auto prev_tpv_send_time = std::chrono::steady_clock::now();

  auto const total_stream_duration = m_latest_last_tp_timestamp - m_earliest_first_tp_timestamp;

  auto run_start_time = std::chrono::steady_clock::now();

  // local counters
  int local_tp_made = 0;
  int local_tpv_made = 0;
  int local_tpv_failed = 0;

  while (running_flag.load()) {

    if (m_loops > 1 && current_iteration >= m_loops) {
      break;
    }

    for (auto& tpv : tpvs) {

      if (!running_flag.load()) {
        break;
      }

      // The argument `earliest_timestamp_time` is the wall-clock time
      // of the earliest first tpset timestamp in _any_ of the input
      // streams. So for the first TP vector we send out, we wait until
      // _this_ stream's first timestamp comes up
      auto wait_time_us = 0;
      std::chrono::steady_clock::time_point next_tpv_send_time;
      if (prev_tpv_start_time == 0) {
        wait_time_us = (tpv.front().tp.time_start - m_earliest_first_tp_timestamp) / clocks_per_us;
        next_tpv_send_time = earliest_timestamp_time + std::chrono::microseconds(wait_time_us);
      } else {
        wait_time_us = (tpv.front().tp.time_start - prev_tpv_start_time) / clocks_per_us;
        next_tpv_send_time = prev_tpv_send_time + std::chrono::microseconds(wait_time_us);
      }

      // check running_flag periodically so we can stop punctually
      auto slice_period = std::chrono::microseconds(m_conf->get_maximum_wait_time_us());
      auto next_slice_send_time = prev_tpv_send_time + slice_period;
      bool break_flag = false;
      while (next_tpv_send_time > next_slice_send_time + slice_period) {
        if (!running_flag.load()) {
          break_flag = true;
          break;
        }
        std::this_thread::sleep_until(next_slice_send_time);
        next_slice_send_time = next_slice_send_time + slice_period;
      }
      if (!break_flag) {
        std::this_thread::sleep_until(next_tpv_send_time);
      }
      prev_tpv_send_time = next_tpv_send_time;
      prev_tpv_start_time = tpv.front().tp.time_start;

      m_tpv_made_count++;
      m_tp_made_count += tpv.size();
      local_tpv_made++;
      local_tp_made += tpv.size();
      try {
        if (m_loops > 1) {
          auto copy = tpv;
          tp_sink->send(std::move(copy), m_queue_timeout);
        } else {
          tp_sink->send(std::move(tpv), m_queue_timeout);
        }
      } catch (const dunedaq::iomanager::TimeoutExpired& e) {
        ers::warning(e);
        m_tpv_failed_sent_count++;
        local_tpv_failed++;
      }

      // Increase timestamps in the TPs so they don't
      // repeat when we do multiple loops over the file
      if (m_loops > 1 && current_iteration < m_loops) {
        for (auto& tpa : tpv) {
          tpa.tp.time_start += total_stream_duration;
          tpa.tp.time_peak += total_stream_duration;
        }
      }

    } // end loop over tpsets
    ++current_iteration;

  } // end while(running_flag.load())

  auto run_end_time = std::chrono::steady_clock::now();
  auto time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(run_end_time - run_start_time).count();
  float rate_hz = 1e3 * static_cast<float>(local_tpv_made) / time_ms;

  TLOG() << "LOCAL: Generated " << local_tpv_made << " TP vectors (" << local_tp_made << " TPs) in " << time_ms
         << " ms. (" << rate_hz << " TP vectors/s). " << local_tpv_failed << " TP vectors failed to push.";

  TLOG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Exiting do_work() method";
}

int
TriggerPrimitiveMakerModule::extract_plane_number(const std::string& str)
{

  // Find the position of the substring "Trigger_0x"
  size_t startPos = str.find("Trigger_0x");
  if (startPos != std::string::npos) {
    startPos += 10; // Move past "Trigger_0x"

    // Extract the 8 characters representing the hex number
    std::string hexPart = str.substr(startPos, 8);

    // Convert the hex string to an integer
    int hexValue;
    std::stringstream ss;
    ss << std::hex << hexPart;
    ss >> hexValue;

    // Return the last digit of the integer value
    return hexValue % 10;
  }

  // Return -1 if the pattern was not found
  return -1;
}

std::string
TriggerPrimitiveMakerModule::extract_readout_unit(const std::string& filename)
{
  std::unique_ptr<hdf5libs::HDF5RawDataFile> input_file;

  // Check file exists
  try {
    input_file = std::make_unique<hdf5libs::HDF5RawDataFile>(filename);
  } catch (const hdf5libs::FileOpenFailed& e) {
    ers::error(dunedaq::trigger::ReplayFileProblem(ERS_HERE, get_name(), filename));
  }

  // Check that the file is a TimeSlice type
  if (!input_file->is_timeslice_type()) {
    ers::error(dunedaq::trigger::BadTPInputFile(ERS_HERE, get_name(), filename));
  }

  std::vector<std::string> fragment_paths = input_file->get_all_fragment_dataset_paths();
  if (fragment_paths.size() == 0) {
    ers::error(dunedaq::trigger::ReplayNoFragments(ERS_HERE, get_name(), filename));
  }

  // the rest here is to access 'random' (first) TP to extract the channel, so that plane is known
  // expectation is that the fragment only has TPs from the same plane...
  std::unique_ptr<daqdataformats::Fragment> frag = input_file->get_frag_ptr(fragment_paths[0]);

  auto frag_data_size = frag->get_data_size();
  if (frag_data_size == 0) {
    ers::error(dunedaq::trigger::ReplayEmptyFrag(ERS_HERE, get_name(), filename));
  }
  trgdataformats::TriggerPrimitive* tp_array = static_cast<trgdataformats::TriggerPrimitive*>(frag->get_data());
  auto& tp = tp_array[0];
  try {
    std::string ROU = m_channel_map->get_tpc_element_from_offline_channel(tp.channel);
    return ROU;
  } catch (...) {
    ers::error(dunedaq::trigger::ReplayROUError(ERS_HERE, get_name(), filename));
    throw;
  }
}

std::vector<std::string>
TriggerPrimitiveMakerModule::filter_fragments(const std::vector<std::string>& fragment_paths, std::string rou)
{
  std::vector<std::string> filtered_fragments;
  for (const auto& path : fragment_paths) {
    int plane = extract_plane_number(path);

    // hack for APA1, basically making plane 1 collection plane :/
    if (rou == "APA_P02SU") {
      if (plane == 1) {
        plane = 2;
      }
    }

    // Check if plane is in m_filter_planes_ids
    if (std::find(m_filter_planes_ids.begin(), m_filter_planes_ids.end(), plane) != m_filter_planes_ids.end()) {
      continue; // Skip this fragment if plane is in m_filter_planes_ids
    }

    // Otherwise, add to filtered_fragments
    filtered_fragments.push_back(path);
  }
  return filtered_fragments;
}

// Helper function to extract run number and bit from the filename
std::pair<int, int>
TriggerPrimitiveMakerModule::extract_run_and_bit(const std::string& filename)
{
  std::regex pattern("_run(\\d+)_.*?_(\\d+)_"); // Matches _run<run_number>_..._<bit>_
  std::smatch match;

  if (std::regex_search(filename, match, pattern) && match.size() > 2) {
    int run_number = std::stoi(match[1].str()); // Extract run number
    int bit = std::stoi(match[2].str());        // Extract bit
    return { run_number, bit };
  }

  // Default if the regex doesn't match
  return { 0, 0 };
}

} // namespace dunedaq::trigger

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::TriggerPrimitiveMakerModule)
