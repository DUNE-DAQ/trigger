/**
 * @file TPReplayModule.hpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_PLUGINS_TPREPLAYMODULE_HPP_
#define TRIGGER_PLUGINS_TPREPLAYMODULE_HPP_

#include "trigger/TPSet.hpp"
#include "trigger/TriggerPrimitiveTypeAdapter.hpp"
#include "trigger/opmon/tpreplaymodule_info.pb.h"

#include "appmodel/PlaneNumberConf.hpp"
#include "appmodel/TPReplayModule.hpp"
#include "appmodel/TPReplayModuleConf.hpp"
#include "appmodel/TPStreamConf.hpp"

#include "appfwk/ConfigurationManager.hpp"
#include "appfwk/DAQModule.hpp"
#include "confmodel/Connection.hpp"
#include "confmodel/DetectorConfig.hpp"
#include "confmodel/Session.hpp"

#include "daqdataformats/SourceID.hpp"
#include "detdataformats/DetID.hpp"
#include "detchannelmaps/TPCChannelMap.hpp"
#include "hdf5libs/HDF5RawDataFile.hpp"
#include "iomanager/Sender.hpp"
#include "triggeralgs/TriggerPrimitive.hpp"
#include "triggeralgs/Types.hpp"
#include "utilities/WorkerThread.hpp"

#include <memory>
#include <string>
#include <vector>

using TriggerPrimitive = dunedaq::trgdataformats::TriggerPrimitive;

DUNE_DAQ_TYPESTRING(dunedaq::trigger::TriggerPrimitiveTypeAdapter, "TriggerPrimitive")
DUNE_DAQ_TYPESTRING(std::vector<dunedaq::trigger::TriggerPrimitiveTypeAdapter>, "TriggerPrimitiveVector")

namespace dunedaq {
namespace trigger {
class TPReplayModule : public dunedaq::appfwk::DAQModule
{
public:
  /**
   * @brief RandomDataListGenerator Constructor
   * @param name Instance name for this RandomDataListGenerator instance
   */
  explicit TPReplayModule(const std::string& name);

  TPReplayModule(const TPReplayModule&) = delete;            ///< TPReplayModule is not copy-constructible
  TPReplayModule& operator=(const TPReplayModule&) = delete; ///< TPReplayModule is not copy-assignable
  TPReplayModule(TPReplayModule&&) = delete;                 ///< TPReplayModule is not move-constructible
  TPReplayModule& operator=(TPReplayModule&&) = delete;      ///< TPReplayModule is not move-assignable

  void init(std::shared_ptr<appfwk::ConfigurationManager> mcfg) override;
  void generate_opmon_data() override;

private:
  // Commands
  void do_configure(const nlohmann::json& /*obj*/);
  void do_start(const nlohmann::json& obj);
  void do_stop(const nlohmann::json& obj);
  void do_scrap(const nlohmann::json& obj);

  // Threading
  void do_work(std::atomic<bool>&,
               std::deque<std::vector<TriggerPrimitiveTypeAdapter>>& tpvs,
               std::shared_ptr<iomanager::SenderConcept<std::vector<trigger::TriggerPrimitiveTypeAdapter>>>& tp_sink,
               std::chrono::steady_clock::time_point earliest_timestamp_time);
  std::vector<std::unique_ptr<std::thread>> m_threads;
  std::atomic<bool> m_running_flag;

  // TP streams
  struct TPStream
  {
    std::shared_ptr<iomanager::SenderConcept<std::vector<trigger::TriggerPrimitiveTypeAdapter>>> tp_sink;
    std::deque<std::vector<TriggerPrimitiveTypeAdapter>> tpvs;
  };
  std::vector<TPStream> m_tp_streams;
  std::map<int, std::string> m_tpstream_files;
  triggeralgs::timestamp_t m_earliest_first_tp_timestamp;
  triggeralgs::timestamp_t m_latest_last_tp_timestamp;

  // TP data
  std::map<std::string, std::map<int, std::deque<std::vector<TriggerPrimitiveTypeAdapter>>>> read_tps(
    std::map<int, std::string>);
  //           ROU             plane                   vectors of TPs (one per frag)
  std::map<std::string, std::map<int, std::deque<std::vector<TriggerPrimitiveTypeAdapter>>>> m_all_tp_data;

  // Configuration
  const appmodel::TPReplayModuleConf* m_conf;
  double clocks_per_us;
  int m_loops;
  std::chrono::milliseconds m_queue_timeout;
  std::chrono::steady_clock::time_point m_run_start_time;

  // Channel maps, plane related
  std::string m_channel_map_name;
  std::shared_ptr<detchannelmaps::TPCChannelMap> m_channel_map;
  bool m_filter_planes;
  std::vector<int> m_filter_planes_ids;
  const std::unordered_set<detdataformats::DetID::Subdetector> m_validSubdetectors;

  // opmon
  using metric_counter_type = uint64_t;
  std::atomic<metric_counter_type> m_tp_made_count;
  std::atomic<metric_counter_type> m_tpv_made_count;
  std::atomic<metric_counter_type> m_tpv_failed_sent_count;
};
} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_PLUGINS_TPREPLAYMODULE_HPP_
