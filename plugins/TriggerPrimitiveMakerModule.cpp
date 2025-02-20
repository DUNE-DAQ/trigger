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
TriggerPrimitiveMakerModule::init(std::shared_ptr<appfwk::ConfigurationManager> mcfg)
{
  auto mtrg = mcfg->get_dal<appmodel::TriggerPrimitiveMakerModule>(get_name());
  m_conf = mtrg->get_configuration();
  if (!m_conf) {
    throw ReplayConfigurationProblem(ERS_HERE, get_name(), "Missing configuration!");
  }

  clocks_per_us =
    mcfg->session()->get_detector_configuration()->get_clock_speed_hz() / 1'000'000.0;

  // Get channel map
  m_channel_map_name = m_conf->get_channel_map();
  if (m_channel_map_name.empty()) {
    throw ReplayConfigurationProblem(ERS_HERE, get_name(), "No Channel map provided!");
  }

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

  // For each of the streams that are specified in the config, we extract ROU, and sort them.
  // Then we create an outgoing sink for each unique ROU + plane (if not filtered) combination.
  // We also keep track of the total timestamp range of all the streams, so we can keep
  // the timestamps of the multiple streams in sync when replaying,
  // even when they don't all start or end at the same time

  auto con = mtrg->get_outputs();

  m_earliest_first_tp_timestamp = std::numeric_limits<triggeralgs::timestamp_t>::max();
  m_latest_last_tp_timestamp = 0;
 
  std::map <int, std::string> tpstream_files;
  for (auto& stream : m_conf->get_tp_streams()) {
    std::pair tmp_obj = std::make_pair( stream->get_index(), stream->get_filename() ); 
    tpstream_files.insert( tmp_obj );
  }

  // Print grouped files
  TLOG() << "Files to use:";
  for (const auto& pair : tpstream_files) {
    std::cout << "Index: " << pair.first << ", Filename: " << pair.second << std::endl;
  }

  // Load data here
  //             ROU               plane              vectors of TPs (one per frag)
  std::map < std::string, std::map< int, std::vector<std::vector<TriggerPrimitiveTypeAdapter>> > > all_tp_data;
  all_tp_data = read_tps( tpstream_files );

  // TEXT BLOCK MOVE LATER
  TLOG() << "THIS RIGHT HERE";
  // Loop through the map and print sizes
  for (const auto& rou_pair : all_tp_data) {
    const std::string& ROU = rou_pair.first;  // ROU name (key)
    const auto& plane_map = rou_pair.second;  // Map of planes for this ROU

    std::cout << "ROU: " << ROU << ", Number of planes: " << plane_map.size() << std::endl;

    // Loop through each plane for the current ROU
    for (const auto& plane_pair : plane_map) {
      int plane = plane_pair.first;  // Plane number (key)
      const auto& vector_of_tps = plane_pair.second;  // Vector of TriggerPrimitiveTypeAdapter vectors

      std::cout << "  Plane: " << plane << ", Number of vectors: " << vector_of_tps.size() << std::endl;
    }
  }
  // END OF TEXT BLOCK

  // Data loaded and sorted.
  // Now we create streams.
  // loop over ROUs
  int global_iter = 0;
  for (const auto& rou_pair : all_tp_data) {
    const std::string& ROU = rou_pair.first;
    const auto& plane_map = rou_pair.second;
    // loop over planes
    int plane_iter = 0;
    for (const auto& plane_pair : plane_map) {
      int plane = plane_pair.first;
      const auto& vector_of_tps = plane_pair.second;

      TPStream this_stream;
      TLOG() << "Stream: " << (global_iter + plane_iter) << "; ROU: " << ROU << "; plane: " << plane << "; TP sink is "
             << con[global_iter + plane_iter]->class_name() << "@" << con[global_iter + plane_iter]->UID();
      this_stream.tp_sink =
        get_iom_sender<std::vector<trigger::TriggerPrimitiveTypeAdapter>>(con[global_iter + plane_iter]->UID());

      this_stream.tpvs = vector_of_tps;

      m_earliest_first_tp_timestamp =
        std::min(m_earliest_first_tp_timestamp, this_stream.tpvs.front().front().tp.time_start);

      m_latest_last_tp_timestamp = std::max(m_latest_last_tp_timestamp, this_stream.tpvs.back().back().tp.time_start);

      m_tp_streams.push_back(std::move(this_stream));
      plane_iter++;
    }
    global_iter = global_iter + plane_iter;
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

  TLOG() << "### SUMMARY ###";
  TLOG() << "------------------------------";
  TLOG() << "Generated TP vectors: " << m_tpv_made_count;
  TLOG() << "Generated TPs: " << m_tp_made_count;
  TLOG() << "Time taken: " << time_ms << " ms";
  TLOG() << "Rate: " << rate_hz << " TP vectors/s";
  TLOG() << "Failed to push TP vectors: " << m_tpv_failed_sent_count;
  TLOG();

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
  info.set_tpv_made_count(m_tpv_made_count);
  info.set_tpv_failed_sent_count(m_tpv_failed_sent_count);

  this->publish(std::move(info));
}

std::map < std::string, std::map< int, std::vector<std::vector<TriggerPrimitiveTypeAdapter>> > >
TriggerPrimitiveMakerModule::read_tps( std::map <int, std::string> tpstream_files )
{
  std::map < std::string, std::map< int, std::vector<std::vector<TriggerPrimitiveTypeAdapter>> > > all_data;

  // loop over each file
  for (const auto& a_file : tpstream_files) {
    std::unique_ptr<hdf5libs::HDF5RawDataFile> input_file;
    std::string filename = a_file.second;
    input_file = std::make_unique<hdf5libs::HDF5RawDataFile>(filename); 
    std::vector<std::string> fragment_paths = input_file->get_all_fragment_dataset_paths();

    for (const auto& path : fragment_paths) {
      std::unique_ptr<daqdataformats::Fragment> frag = input_file->get_frag_ptr(path);
      trgdataformats::TriggerPrimitive* tp_array = static_cast<trgdataformats::TriggerPrimitive*>(frag->get_data());
      auto& tp = tp_array[0];
      // get rou
      std::string ROU = m_channel_map->get_tpc_element_from_offline_channel(tp.channel);
      // get plane
      int plane = m_channel_map->get_plane_from_offline_channel(tp.channel);
      // check filtering
      if (std::find(m_filter_planes_ids.begin(), m_filter_planes_ids.end(), plane) != m_filter_planes_ids.end()) {
        continue; // Skip this fragment if plane is in m_filter_planes_ids
      }
      // extract data
      std::vector<TriggerPrimitiveTypeAdapter> tps;
      size_t num_tps = frag->get_data_size() / sizeof(trgdataformats::TriggerPrimitive);
      tps.reserve(num_tps);
      for (size_t i(0); i < num_tps; i++) {
        trigger::TriggerPrimitiveTypeAdapter tpa;
        tpa.tp = tp_array[i];
        tps.push_back(std::move(tpa));
      }
      frag.reset();
      // store data
      all_data[ROU][plane].push_back(std::move(tps));
    }
  }

  // Sort each vector for ROU and plane by the time_start of the first TriggerPrimitiveTypeAdapter
  for (auto& rou_pair : all_data) {
    for (auto& plane_pair : rou_pair.second) {
      auto& vector_of_tps = plane_pair.second;

      std::sort(vector_of_tps.begin(), vector_of_tps.end(),
        [](const std::vector<TriggerPrimitiveTypeAdapter>& a, const std::vector<TriggerPrimitiveTypeAdapter>& b) {
          return a[0].tp.time_start < b[0].tp.time_start; // Sorting based on the first TriggerPrimitive's time_start
        });
    }
  }

  return all_data;
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

    if (current_iteration >= m_loops) {
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

  TLOG() << "Thread summary:";
  TLOG() << "------------------------------";
  TLOG() << "Sent TPs: " << local_tp_made;
  TLOG() << "TP vectors: " << local_tpv_made;
  TLOG() << "Time taken: " << time_ms << " ms";
  TLOG() << "Rate: " << rate_hz << " TP vectors/s";
  TLOG() << "Failed to push TP vectors: " << local_tpv_failed;
  TLOG();

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
    return {};
  }

  // Check that the file is a TimeSlice type
  if (!input_file->is_timeslice_type()) {
    ers::error(dunedaq::trigger::BadTPInputFile(ERS_HERE, get_name(), filename));
    return {};
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
    return {};
  }
  trgdataformats::TriggerPrimitive* tp_array = static_cast<trgdataformats::TriggerPrimitive*>(frag->get_data());
  auto& tp = tp_array[0];
  try {
    std::string ROU = m_channel_map->get_tpc_element_from_offline_channel(tp.channel);
    return ROU;
  } catch (...) {
    ers::error(dunedaq::trigger::ReplayROUError(ERS_HERE, get_name(), filename));
    return {};
  }
}

} // namespace dunedaq::trigger

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::TriggerPrimitiveMakerModule)
