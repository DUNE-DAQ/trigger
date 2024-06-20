/**
 * @file TPChannelFilter.cpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2021.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "TPChannelFilter.hpp"
#include "trigger/Logging.hpp"

#include "appfwk/DAQModuleHelper.hpp"
#include "iomanager/IOManager.hpp"
#include "triggeralgs/TriggerPrimitive.hpp"

#include <string>

using dunedaq::trigger::logging::TLVL_DEBUG_ALL;
using dunedaq::trigger::logging::TLVL_DEBUG_HIGH;
using dunedaq::trigger::logging::TLVL_DEBUG_MEDIUM;
using dunedaq::trigger::logging::TLVL_GENERAL;
using dunedaq::trigger::logging::TLVL_IMPORTANT;

namespace dunedaq {
namespace trigger {
TPChannelFilter::TPChannelFilter(const std::string& name)
  : DAQModule(name)
  , m_thread(std::bind(&TPChannelFilter::do_work, this))
  , m_input_queue(nullptr)
  , m_output_queue(nullptr)
  , m_queue_timeout(1000)
{

  register_command("conf", &TPChannelFilter::do_conf);
  register_command("start", &TPChannelFilter::do_start);
  register_command("stop", &TPChannelFilter::do_stop);
  register_command("scrap", &TPChannelFilter::do_scrap);
}

void
TPChannelFilter::init(const nlohmann::json& iniobj)
{
  try {
    m_input_queue = get_iom_receiver<TPSet>(appfwk::connection_uid(iniobj, "tpset_source"));
    m_output_queue = get_iom_sender<TPSet>(appfwk::connection_uid(iniobj, "tpset_sink"));
  } catch (const ers::Issue& excpt) {
    throw dunedaq::trigger::InvalidQueueFatalError(ERS_HERE, get_name(), "input/output", excpt);
  }

  // Latency
  m_data_vs_system_time = 0;
  // to convert 62.5MHz clock ticks to ms: 1/62500000 = 0.000000016 <- seconds per tick; 0.000016 <- ms per tick;
  // 16*1e-6 <- sci notation
  m_clock_ticks_to_ms = 16 * 1e-6;
  m_first_tp = true;
  m_initial_offset = 0;
  m_system_time = 0;
}

void
TPChannelFilter::get_info(opmonlib::InfoCollector& ci, int /*level*/)
{
  tpchannelfilterinfo::Info i;

  i.received_count = m_received_count.load();
  i.sent_count = m_sent_count.load();
  i.data_vs_system_time_ms = m_data_vs_system_time.load();

  ci.add(i);
}

void
TPChannelFilter::do_conf(const nlohmann::json& conf_arg)
{
  m_conf = conf_arg.get<dunedaq::trigger::tpchannelfilter::Conf>();
  m_channel_map = dunedaq::detchannelmaps::make_map(m_conf.channel_map_name);
  m_use_latency_monit = m_conf.enable_latency_monit;
  m_use_latency_offset = m_conf.use_latency_offset;
  TLOG_DEBUG(TLVL_GENERAL) << "[TPCF] Configured the TPChannelFilter!";
}

void
TPChannelFilter::do_start(const nlohmann::json&)
{
  m_running_flag.store(true);
  m_received_count.store(0);
  m_sent_count.store(0);
  m_thread.start_working_thread(get_name());
  TLOG_DEBUG(TLVL_GENERAL) << "[TPCF] " << get_name() + " successfully started.";
}

void
TPChannelFilter::do_stop(const nlohmann::json&)
{
  m_running_flag.store(false);
  m_thread.stop_working_thread();
  TLOG_DEBUG(TLVL_GENERAL) << "[TPCF] " << get_name() + " successfully stopped.";
}

void
TPChannelFilter::do_scrap(const nlohmann::json&)
{
}

bool
TPChannelFilter::channel_should_be_removed(int channel) const
{
  // The plane numbering convention is found in detchannelmaps/plugins/VDColdboxChannelMap.cpp and is:
  // U (induction) = 0, Y (induction) = 1, Z (induction) = 2, unconnected channel = 9999
  uint plane = m_channel_map->get_plane_from_offline_channel(channel);
  TLOG_DEBUG(TLVL_DEBUG_HIGH) << "[TPCF] Checking received TP with channel " << channel << " and plane " << plane;
  // Check for collection
  if (plane == 0 || plane == 1) {
    return !m_conf.keep_induction;
  }
  // Check for induction
  if (plane == 2) {
    return !m_conf.keep_collection;
  }
  // Always remove unconnected channels
  if (plane == 9999) {
    return true;
  }
  // Unknown plane?!
  TLOG_DEBUG(TLVL_IMPORTANT) << "[TPCF] Encountered unexpected plane " << plane << " from channel " << channel
                             << ", check channel map?";
  return false;
}

void
TPChannelFilter::do_work()
{
  while (m_running_flag.load()) {

    std::optional<TPSet> tpset = m_input_queue->try_receive(m_queue_timeout);
    using namespace std::chrono;

    if (!tpset.has_value()) {
      // The condition to exit the loop is that we've been stopped and
      // there's nothing left on the input queue
      if (!m_running_flag.load()) {
        break;
      } else {
        continue;
      }
    }

    // If we got here, we got a TPSet
    ++m_received_count;

    // Actually do the removal for payload TPSets. Leave heartbeat TPSets unmolested
    if (tpset->type == TPSet::kPayload) {

      if (m_first_tp) {
        if (m_use_latency_monit && m_use_latency_offset) {
          m_initial_offset = (duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()) -
                             (tpset->objects.begin()->time_start * m_clock_ticks_to_ms);
        }
        m_first_tp = false;
      }

      if (m_use_latency_monit) {
        // block to report latency to opmon
        m_system_time =
          std::chrono::duration_cast<std::chrono::milliseconds>(system_clock::now().time_since_epoch()).count();
        m_data_vs_system_time.store(fabs(m_system_time - (tpset->start_time * m_clock_ticks_to_ms) - m_initial_offset));
        TLOG_DEBUG(TLVL_DEBUG_ALL) << "[TPCF] "
                                   << fabs((m_system_time - (tpset->start_time * m_clock_ticks_to_ms) - m_initial_offset));
      }

      size_t n_before = tpset->objects.size();
      auto it = std::remove_if(tpset->objects.begin(), tpset->objects.end(), [this](triggeralgs::TriggerPrimitive p) {
        return channel_should_be_removed(p.channel) || (p.time_over_threshold > m_conf.max_time_over_threshold);
      });
      tpset->objects.erase(it, tpset->objects.end());
      size_t n_after = tpset->objects.size();
      TLOG_DEBUG(TLVL_DEBUG_MEDIUM) << "[TPCF] Removed " << (n_before - n_after) << " TPs out of " << n_before
                                    << " TPs remaining: " << n_after;
    }

    // The rule is that we don't send empty TPSets, so ensure that
    if (!tpset->objects.empty()) {
      try {
        m_output_queue->send(std::move(*tpset), m_queue_timeout);
        ++m_sent_count;
      } catch (const dunedaq::iomanager::TimeoutExpired& excpt) {
        std::ostringstream oss_warn;
        oss_warn << "push to output queue \"" << m_output_queue->get_name() << "\"";
        ers::warning(dunedaq::iomanager::TimeoutExpired(ERS_HERE, get_name(), oss_warn.str(), m_queue_timeout.count()));
      }
    }

  } // while
  TLOG_DEBUG(TLVL_GENERAL) << "[TPCF] Exiting do_work() method";
}

} // namespace trigger
} // namespace dunedaq

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::TPChannelFilter)
