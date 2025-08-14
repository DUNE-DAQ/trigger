/**
 * @file TPReplayModule.cpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "TPReplayModule.hpp"

#include "trigger/Issues.hpp" // For TLVL_*
#include "trigger/TriggerPrimitiveTypeAdapter.hpp"

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

TPReplayModule::TPReplayModule(const std::string& name)
  : DAQModule(name)
  , m_queue_timeout(100)
{
  // clang-format off
  register_command("conf",  &TPReplayModule::do_configure);
  register_command("start", &TPReplayModule::do_start);
  register_command("stop_trigger_sources",  &TPReplayModule::do_stop);
  register_command("scrap", &TPReplayModule::do_scrap);
  // clang-format on
}

void
TPReplayModule::init(std::shared_ptr<appfwk::ConfigurationManager> mcfg)
{
  TLOG_DEBUG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Entering init() method";

  // ### Access configuration
  m_mtrg = mcfg->get_dal<appmodel::TPReplayModule>(get_name());

  // ### Extract relevant objects
  // Clock speed
  m_clocks_per_us = mcfg->session()->get_detector_configuration()->get_clock_speed_hz() /
                    double(1'000'000.0); // this is redundant but safer...

  TLOG_DEBUG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Exiting init() method";
}

void
TPReplayModule::do_configure(const nlohmann::json& /*obj*/)
{

  TLOG_DEBUG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Entering conf() method";

  m_conf = m_mtrg->get_configuration();
  if (!m_conf) {
    throw ReplayConfigurationProblem(ERS_HERE, get_name(), "Missing configuration!");
  }

  // Channel map
  m_channel_map_name = m_conf->get_channel_map();
  if (m_channel_map_name.empty()) {
    throw ReplayConfigurationProblem(ERS_HERE, get_name(), "No Channel map provided!");
  }

  // Valid Subdetectors (for now)
  m_validSubdetectors = { detdataformats::DetID::Subdetector::kHD_TPC,
                          detdataformats::DetID::Subdetector::kVD_BottomTPC,
                          detdataformats::DetID::Subdetector::kVD_TopTPC,
                          detdataformats::DetID::Subdetector::kNDLAr_TPC };

  TLOG() << "### REPLAY CONFIGURATION ###";
  TLOG() << "Will use channel map: " << m_channel_map_name;
  try {
    m_channel_map = dunedaq::detchannelmaps::make_tpc_map(m_channel_map_name);
  } catch (const detchannelmaps::ChannelMapCreationFailed& e) {
    ers::error(dunedaq::trigger::ReplayChannelMapProblem(ERS_HERE, get_name(), m_channel_map_name));
  }

  // Loops
  m_loops = m_conf->get_number_of_loops();

  // Plane filtering
  m_filter_planes_ids = std::set<int>(m_conf->get_filter_out_plane().begin(), m_conf->get_filter_out_plane().end());
  m_filter_planes = (m_filter_planes_ids.size() > 0) ? true : false;

  TLOG() << "Plane filtering: " << m_filter_planes;
  if (m_filter_planes) {
    TLOG() << "Planes to filter: ";
    for (auto plane : m_filter_planes_ids) {
      TLOG() << plane;
    }
  }

  // For each of the files that are specified in the config, we extract data and sort them.
  // Data is sorted by ROU -> Plane (if not filtered). Multiple files can contribute to each.
  // Then we create an outgoing sink for each unique ROU + plane combination.
  // We also keep track of the total timestamp range of all the streams, so we can keep
  // the timestamps of the multiple streams in sync when replaying,
  // even when they don't all start or end at the same time.

  // Output queues
  auto con = m_mtrg->get_outputs();

  // Global times
  m_earliest_first_tp_timestamp = std::numeric_limits<triggeralgs::timestamp_t>::max();
  m_latest_last_tp_timestamp = 0;

  // Loading sorted TP stream files
  for (auto& stream : m_conf->get_tp_streams()) {
    auto result = m_tpstream_files.insert(std::make_pair(stream->get_index(), stream->get_filename()));
    if (!result.second) {
      ers::error(dunedaq::trigger::ReplayStreamFileError(
        ERS_HERE, get_name(), stream->get_index(), stream->get_filename(), result.first->second));
    }
  }

  // Print grouped files
  TLOG() << "Files to use:";
  for (const auto& pair : m_tpstream_files) {
    std::cout << "Index: " << pair.first << ", Filename: " << pair.second << std::endl;
  }

  // Load data here
  m_all_tp_data = read_tps(m_tpstream_files);
  if (m_tpstream_files.size() == 0 || m_all_tp_data.size() == 0) {
    ers::error(dunedaq::trigger::ReplayNoValidFiles(ERS_HERE, get_name()));
  }

  // Get earliest time
  m_earliest_tp_time = get_earliest_time_start(m_all_tp_data).value_or(0); // Error if 0?
  TLOG_DEBUG(10) << "The earliest available TP time_start is: " << m_earliest_tp_time;

  // Shift TP times to 'now'
  shift_time_starts(m_all_tp_data);

  // Data loaded and sorted.
  // Now we create streams.
  int global_iter = 0;
  // Loop over ROUs
  for (auto& [ROU, plane_map] : m_all_tp_data) {
    int plane_iter = 0;
    // Loop over Planes
    for (auto& [plane, vector_of_tps] : plane_map) {

      TLOG_DEBUG(1) << "Stream: " << (global_iter + plane_iter) << "; ROU: " << ROU << "; plane: " << plane
                    << "; TP sink is " << con[global_iter + plane_iter]->class_name() << "@"
                    << con[global_iter + plane_iter]->UID();

      TPStream this_stream{ get_iom_sender<std::vector<trigger::TriggerPrimitiveTypeAdapter>>(
                              con[global_iter + plane_iter]->UID()),
                            vector_of_tps };

      m_earliest_first_tp_timestamp =
        std::min(m_earliest_first_tp_timestamp, this_stream.tpvs.front().front().tp.time_start);
      m_latest_last_tp_timestamp = std::max(m_latest_last_tp_timestamp, this_stream.tpvs.back().back().tp.time_start);

      m_tp_streams.push_back(std::move(this_stream));
      plane_iter++;
    }
    global_iter += plane_iter;
  }

  TLOG() << "Total of " << m_tp_streams.size() << " TP streams.";

  TLOG_DEBUG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Exiting conf() method";
}

void
TPReplayModule::do_start(const nlohmann::json& /*obj*/)
{
  TLOG_DEBUG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Entering start() method";

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

  // Start threads for each stream
  for (auto& stream : m_tp_streams) {

    m_threads.push_back(std::make_unique<std::thread>(&TPReplayModule::do_work,
                                                      this,
                                                      std::ref(m_running_flag),
                                                      std::ref(stream.tpvs),
                                                      std::ref(stream.tp_sink),
                                                      earliest_timestamp_time));
  }
  for (size_t i = 0; i < m_threads.size(); i++) {
    std::string name("tpreplay-");
    name += std::to_string(i);
    pthread_setname_np(m_threads[i]->native_handle(), name.c_str());
  }
  TLOG() << "Total of " << m_threads.size() << " replay threads.";

  TLOG_DEBUG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Exiting start() method";
}

void
TPReplayModule::do_stop(const nlohmann::json& /*args*/)
{
  TLOG_DEBUG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Entering stop() method";

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

  TLOG() << "### SUMMARY ###";
  TLOG() << "------------------------------";
  TLOG() << "Generated TP vectors: " << m_tpv_made_count;
  TLOG() << "Generated TPs: " << m_tp_made_count;
  TLOG() << "Time taken: " << time_ms << " ms";
  TLOG() << "Rate: " << rate_hz << " TP vectors/s";
  TLOG() << "Failed to push TP vectors: " << m_tpv_failed_sent_count;
  TLOG();

  TLOG_DEBUG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Exiting stop() method";
}

void
TPReplayModule::do_scrap(const nlohmann::json& /*args*/)
{
  TLOG_DEBUG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Entering scrap() method";

  m_tp_streams.clear();
  m_threads.clear();
  m_tpstream_files.clear();
  m_all_tp_data.clear();
  m_filter_planes_ids.clear();
  m_validSubdetectors.clear();

  m_channel_map.reset();

  TLOG_DEBUG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Exiting scrap() method";
}

void
TPReplayModule::generate_opmon_data()
{
  opmon::TPReplayModuleInfo info;

  info.set_tp_made_count(m_tp_made_count);
  info.set_tpv_made_count(m_tpv_made_count);
  info.set_tpv_failed_sent_count(m_tpv_failed_sent_count);

  this->publish(std::move(info));
}

// This is the heavy-lifting function of TPRM
// Goes over all provided TPstream files
// Does basic file checks
// Extracts needed data
// Sorts data per ROU and plane
// Additional data-related checks
// Plane filtering happens here
std::map<std::string, std::map<int, std::deque<std::vector<TriggerPrimitiveTypeAdapter>>>>
TPReplayModule::read_tps(std::map<int, std::string> m_tpstream_files)
{

  std::map<std::string, std::map<int, std::deque<std::vector<TriggerPrimitiveTypeAdapter>>>> all_data;

  // Loop over each file
  for (const auto& a_file : m_tpstream_files) {
    std::string filename = a_file.second;

    // Check file exists
    std::unique_ptr<hdf5libs::HDF5RawDataFile> input_file;
    try {
      input_file = std::make_unique<hdf5libs::HDF5RawDataFile>(filename);
    } catch (const hdf5libs::FileOpenFailed& e) {
      ers::error(dunedaq::trigger::ReplayFileProblem(ERS_HERE, get_name(), filename));
      return {};
    }

    // Check that the file is a TimeSlice type
    if (!input_file->is_timeslice_type()) {
      ers::error(dunedaq::trigger::BadTPInputFile(ERS_HERE, get_name(), filename));
      return {};
    }

    std::vector<std::string> fragment_paths = input_file->get_all_fragment_dataset_paths();
    // Check there are fragments
    if (fragment_paths.empty()) {
      ers::error(dunedaq::trigger::ReplayNoFragments(ERS_HERE, get_name(), filename));
      return {};
    }

    // Local counters
    std::set<std::string> local_rous;
    std::set<int> local_planes;
    int local_tp_vectors = 0;
    int local_tps = 0;

    // Loop over paths/fragments
    for (const auto& path : fragment_paths) {
      std::unique_ptr<daqdataformats::Fragment> frag = input_file->get_frag_ptr(path);

      // Check fragment has data
      auto frag_size = frag->get_data_size();
      if (frag_size == 0) {
        ers::error(dunedaq::trigger::ReplayEmptyFrag(ERS_HERE, get_name(), filename));
        continue;
      }

      trgdataformats::TriggerPrimitive* tp_array = static_cast<trgdataformats::TriggerPrimitive*>(frag->get_data());
      size_t num_tps = frag_size / sizeof(trgdataformats::TriggerPrimitive);

      // Check there is TP data
      if (num_tps < 1) {
        ers::error(dunedaq::trigger::ReplayNoValidTPs(ERS_HERE, get_name(), filename));
        continue;
      }

      // Store TPs
      auto& tp = tp_array[0];

      // Only select TPC TPs (for now)
      dunedaq::detdataformats::DetID::Subdetector subdet =
        static_cast<dunedaq::detdataformats::DetID::Subdetector>(tp.detid);
      if (!m_validSubdetectors.count(subdet)) {
        continue;
      }

      // Get ROU and plane
      std::string ROU;
      try {
        ROU = m_channel_map->get_element_name_from_offline_channel(tp.channel);
        local_rous.insert(ROU);
      } catch (...) {
        ers::error(dunedaq::trigger::ReplayROUError(ERS_HERE, get_name(), filename));
        continue;
      }

      int plane;
      try {
        plane = m_channel_map->get_plane_from_offline_channel(tp.channel);
        local_planes.insert(plane);
      } catch (...) {
        ers::error(dunedaq::trigger::ReplayPlaneError(ERS_HERE, get_name(), filename));
        continue;
      }

      // hack for APA1, basically making plane 1 collection plane
      // decide whether we want this here long term
      if (ROU == "APA_P02SU") {
        plane = (plane == 1) ? 2 : (plane == 2) ? 1 : plane;
      }

      // Check if this plane should be filtered
      if (std::find(m_filter_planes_ids.begin(), m_filter_planes_ids.end(), plane) != m_filter_planes_ids.end()) {
        continue;
      }

      // Extract trigger primitives
      // Create a vector of the correct size, and directly associate it with tp_array
      std::vector<TriggerPrimitiveTypeAdapter> tps(reinterpret_cast<TriggerPrimitiveTypeAdapter*>(tp_array),
                                                   reinterpret_cast<TriggerPrimitiveTypeAdapter*>(tp_array) + num_tps);

      if (tps.empty()) {
        continue;
      }

      // Efficient insertion into deque
      auto& data_deque = all_data[ROU][plane];

      // First insertion if deque is empty
      if (data_deque.empty()) {
        data_deque.push_back(std::move(tps));
      }
      // Append if already in order
      else if (data_deque.back().front().tp.time_start <= tps.front().tp.time_start) {
        data_deque.push_back(std::move(tps));
      }
      // Prepend if this vector is older than the first element
      else if (data_deque.front().front().tp.time_start >= tps.front().tp.time_start) {
        data_deque.push_front(std::move(tps));
      }
      // General case: Insert using binary search (O(log N) search + O(1) insertion)
      else {
        auto insert_pos = std::lower_bound(
          data_deque.begin(),
          data_deque.end(),
          tps,
          [](const std::vector<TriggerPrimitiveTypeAdapter>& a, const std::vector<TriggerPrimitiveTypeAdapter>& b) {
            return a.front().tp.time_start < b.front().tp.time_start;
          });

        data_deque.insert(insert_pos, std::move(tps));
      }
      data_deque.back().shrink_to_fit(); // Reclaims unused memory
      frag.reset();

      local_tp_vectors++;
      local_tps += num_tps;
    } // frags loop

    TLOG() << "Data loading summary (end of file):";
    TLOG() << "------------------------------";
    TLOG() << "File: " << filename;
    TLOG() << "ROUs: " << local_rous.size();
    TLOG() << "Planes: " << local_planes.size();
    TLOG() << "TP vectors: " << local_tp_vectors;
    TLOG() << "Total read TPs: " << local_tps;
    TLOG();

  } // files loop

  TLOG() << "Data loading summary (all):";
  TLOG() << "------------------------------";
  TLOG() << "Files: " << m_tpstream_files.size();
  // Loop through the map and print sizes
  for (const auto& [ROU, plane_map] : all_data) {
    TLOG() << "ROU: " << ROU << ", Number of planes: " << plane_map.size();

    // Loop through each plane for the current ROU
    for (const auto& [plane, vector_of_tps] : plane_map) {
      TLOG() << "  Plane: " << plane << ", Number of vectors: " << vector_of_tps.size();
    }
  }

  return all_data;
}

void
TPReplayModule::do_work(
  std::atomic<bool>& running_flag,
  std::deque<std::vector<TriggerPrimitiveTypeAdapter>>& tpvs,
  std::shared_ptr<iomanager::SenderConcept<std::vector<trigger::TriggerPrimitiveTypeAdapter>>>& tp_sink,
  std::chrono::steady_clock::time_point earliest_timestamp_time)
{
  TLOG_DEBUG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Entering do_work() method";

  int current_iteration = 0; // NOLINT(build/unsigned)

  uint64_t prev_tpv_start_time = 0; // NOLINT(build/unsigned)
  auto prev_tpv_send_time = std::chrono::steady_clock::now();

  auto const total_stream_duration = m_latest_last_tp_timestamp - m_earliest_first_tp_timestamp;
  auto run_start_time = std::chrono::steady_clock::now();

  // Local counters
  int local_tp_made = 0;
  int local_tpv_made = 0;
  int local_tpv_failed = 0;

  while (running_flag.load()) {

    // Looping logic: exit if m_loops is set and we've reached the limit
    if ((m_loops != -1) && (current_iteration >= m_loops)) {
      break;
    }

    // Going over TP vectors
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
        wait_time_us = (tpv.front().tp.time_start - m_earliest_first_tp_timestamp) / m_clocks_per_us;
        next_tpv_send_time = earliest_timestamp_time + std::chrono::microseconds(wait_time_us);
      } else {
        wait_time_us = (tpv.front().tp.time_start - prev_tpv_start_time) / m_clocks_per_us;
        next_tpv_send_time = prev_tpv_send_time + std::chrono::microseconds(wait_time_us);
      }

      // Check running_flag periodically so we can stop punctually
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

      // Update times
      prev_tpv_send_time = next_tpv_send_time;
      prev_tpv_start_time = tpv.front().tp.time_start;

      // Update counters
      m_tpv_made_count++;
      m_tp_made_count += tpv.size();
      local_tpv_made++;
      local_tp_made += tpv.size();

      // Actually send data
      try {
        // Decide whether to move or copy based on loop count
        if (m_loops == 1) {
          // Only one loop: safe to move original data
          tp_sink->send(std::move(tpv), m_queue_timeout);
        } else {
          // Multiple or infinite loops: send a copy to preserve original
          auto tpv_copy = tpv;
          tp_sink->send(std::move(tpv_copy), m_queue_timeout);
        }
      } catch (const dunedaq::iomanager::TimeoutExpired& e) {
        ers::warning(e);
        m_tpv_failed_sent_count++;
        local_tpv_failed++;
      }

      // Increase timestamps in the TPs so they don't
      // repeat when we do multiple loops over the file
      bool will_repeat = (m_loops == -1) || (current_iteration + 1 < m_loops);
      if (will_repeat) {
        for (auto& tpa : tpv) {
          tpa.tp.time_start += total_stream_duration;
        }
      }

    } // end loop over tpsets
    ++current_iteration;

  } // end while(running_flag.load())

  auto run_end_time = std::chrono::steady_clock::now();
  auto time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(run_end_time - run_start_time).count();
  float rate_hz = 1e3 * static_cast<float>(local_tpv_made) / time_ms;

  TLOG() << "Thread summary:";
  TLOG() << "------------------------------";
  TLOG() << "Sent TPs: " << local_tp_made;
  TLOG() << "TP vectors: " << local_tpv_made;
  TLOG() << "Time taken: " << time_ms << " ms";
  TLOG() << "Rate: " << rate_hz << " TP vectors/s";
  TLOG() << "Failed to push TP vectors: " << local_tpv_failed;
  TLOG();

  TLOG_DEBUG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Exiting do_work() method";
}

std::optional<uint64_t>
TPReplayModule::get_earliest_time_start(
  const std::map<std::string, std::map<int, std::deque<std::vector<TriggerPrimitiveTypeAdapter>>>>& data)
{
  std::optional<uint64_t> earliest;

  for (const auto& [str_key, inner_map] : data) {
    for (const auto& [int_key, dq] : inner_map) {
      if (!dq.empty() && !dq.front().empty()) {
        const auto& tp = dq.front().front().tp; // First TP
        if (!earliest.has_value() || tp.time_start < *earliest) {
          earliest = tp.time_start;
        }
      }
    }
  }

  return earliest;
}

void
TPReplayModule::shift_time_starts(
  std::map<std::string, std::map<int, std::deque<std::vector<TriggerPrimitiveTypeAdapter>>>>& data)
{
  TLOG_DEBUG(10) << "SHIFTING FUNCTION";
  // Get current time
  uint64_t current_time =
    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch())
      .count();
  // Convert to clock ticks (62.5 MHz clock)
  uint64_t current_time_clock = static_cast<uint64_t>(current_time * (625.0 / 10000.0));
  uint64_t diff = current_time_clock - m_earliest_tp_time;

  TLOG_DEBUG(10) << "Current sys time: " << current_time;
  TLOG_DEBUG(10) << "Current sys time, in ticks: " << current_time_clock;
  TLOG_DEBUG(10) << "Time diff: " << diff;

  for (auto& [str_key, inner_map] : data) {
    for (auto& [int_key, dq] : inner_map) {
      for (auto& vec : dq) {
        for (auto& tpa : vec) {
          tpa.tp.time_start += diff;
        }
      }
    }
  }
  return;
}

} // namespace dunedaq::trigger

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::TPReplayModule)
