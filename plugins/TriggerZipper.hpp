/** TriggerZipper is an appfwk::module that runs zipper::merge
 */

#ifndef TRIGGER_PLUGINS_TRIGGERZIPPER_HPP_
#define TRIGGER_PLUGINS_TRIGGERZIPPER_HPP_

#include "appfwk/DAQModule.hpp"
#include "appfwk/DAQModuleHelper.hpp"
#include "appfwk/DAQSink.hpp"
#include "appfwk/DAQSource.hpp"
#include "toolbox/ThreadHelper.hpp"

#include "daqdataformats/GeoID.hpp"

#include "trigger/Issues.hpp"
#include "trigger/triggerzipper/Nljs.hpp"
#include "zipper.hpp"

#include <chrono>
#include <list>
#include <logging/Logging.hpp>
#include <sstream>

const char* inqs_name = "inputs";
const char* outq_name = "output";

namespace dunedaq::trigger {

// template<typename Payload,
//          typename Ordering = size_t,
//          typename Identity = size_t,
//          typename TimePoint = std::chrono::steady_clock::time_point,
//          typename
// >

size_t
zipper_stream_id(const daqdataformats::GeoID& geoid)
{
  return (0xffff000000000000 & (static_cast<size_t>(geoid.system_type) << 48)) |
         (0x0000ffff00000000 & (static_cast<size_t>(geoid.region_id) << 32)) | (0x00000000ffffffff & geoid.element_id);
}

template<typename TSET>
class TriggerZipper : public dunedaq::appfwk::DAQModule
{

public:
  // Derived types
  using tset_type = TSET;
  using ordering_type = typename TSET::timestamp_t;
  using origin_type = typename TSET::origin_t; // GeoID
  using seqno_type = typename TSET::seqno_t;   // GeoID

  using cache_type = std::list<TSET>;
  using payload_type = typename cache_type::iterator;
  using identity_type = size_t;

  using node_type = zipper::Node<payload_type>;
  using zm_type = zipper::merge<node_type>;
  zm_type m_zm;

  // queues
  using source_t = appfwk::DAQSource<TSET>;
  using sink_t = appfwk::DAQSink<TSET>;
  std::unique_ptr<source_t> m_inq{};
  std::unique_ptr<sink_t> m_outq{};

  using cfg_t = triggerzipper::ConfParams;
  cfg_t m_cfg;

  std::thread m_thread;
  std::atomic<bool> m_running{ false };

  // We store input TSETs in a list and send iterator though the
  // zipper as payload so as to not suffer copy overhead.
  cache_type m_cache;
  seqno_type m_next_seqno{ 0 };

  size_t m_n_received{ 0 };
  size_t m_n_sent{ 0 };
  size_t m_n_tardy{ 0 };
  std::map<daqdataformats::GeoID, size_t> m_tardy_counts;

  explicit TriggerZipper(const std::string& name)
    : DAQModule(name)
    , m_zm()
  {
    // clang-format off
        register_command("conf",   &TriggerZipper<TSET>::do_configure);
        register_command("start",  &TriggerZipper<TSET>::do_start);
        register_command("stop",   &TriggerZipper<TSET>::do_stop);
        register_command("scrap",  &TriggerZipper<TSET>::do_scrap);
    // clang-format on
  }
  virtual ~TriggerZipper() {}

  void init(const nlohmann::json& ini)
  {
    set_input(appfwk::queue_inst(ini, "input"));
    set_output(appfwk::queue_inst(ini, "output"));
  }
  void set_input(const std::string& name) { m_inq.reset(new source_t(name)); }
  void set_output(const std::string& name) { m_outq.reset(new sink_t(name)); }

  void do_configure(const nlohmann::json& cfgobj)
  {
    m_cfg = cfgobj.get<cfg_t>();
    m_zm.set_max_latency(std::chrono::milliseconds(m_cfg.max_latency_ms));
    m_zm.set_cardinality(m_cfg.cardinality);
  }

  void do_scrap(const nlohmann::json& /*stopobj*/)
  {
    m_cfg = cfg_t{};
    m_zm.set_cardinality(0);
  }

  void do_start(const nlohmann::json& /*startobj*/)
  {
    m_n_received = 0;
    m_n_sent = 0;
    m_n_tardy = 0;
    m_tardy_counts.clear();
    m_running.store(true);
    m_thread = std::thread(&TriggerZipper::worker, this);
  }

  void do_stop(const nlohmann::json& /*stopobj*/)
  {
    m_running.store(false);
    m_thread.join();
    flush();
    m_zm.clear();
    TLOG() << "Received " << m_n_received << " Sets. Sent " << m_n_sent << " Sets. " << m_n_tardy << " were tardy";
    std::stringstream ss;
    ss << std::endl;
    for(auto& [id, n]: m_tardy_counts){
      ss << id << "\t" << n << std::endl;
    }
    TLOG_DEBUG(1) << "Tardy counts:" << ss.str();
  }

  // thread worker
  void worker()
  {
    while (true) {
      // Once we've received a stop command, keep reading the input
      // queue until there's nothing left on it
      if (!proc_one() && !m_running.load()) {
        break;
      }
    }
  }

  bool proc_one()
  {
    m_cache.emplace_front(); // to be filled
    auto& tset = m_cache.front();
    try {
      m_inq->pop(tset, std::chrono::milliseconds(10));
      ++m_n_received;
    } catch (appfwk::QueueTimeoutExpired&) {
      m_cache.pop_front(); // vestigial
      drain();
      return false;
    }

    if(!m_tardy_counts.count(tset.origin)) m_tardy_counts[tset.origin]=0;

    bool accepted = m_zm.feed(m_cache.begin(), tset.start_time, zipper_stream_id(tset.origin));

    if (!accepted) {
      ++m_n_tardy;
      ++m_tardy_counts[tset.origin];

      ers::warning(TardyInputSet(ERS_HERE, get_name(), tset.origin.region_id, tset.origin.element_id, tset.start_time, m_zm.get_origin()));
      m_cache.pop_front(); // vestigial
    }
    drain();
    return true;
  }

  void send_out(std::vector<node_type>& got)
  {
    for (auto& node : got) {
      payload_type lit = node.payload;
      auto& tset = *lit; // list iterator

      // tell consumer "where" the set was produced
      tset.origin.region_id = m_cfg.region_id;
      tset.origin.element_id = m_cfg.element_id;
      tset.seqno = m_next_seqno++;

      try {
        m_outq->push(tset, std::chrono::milliseconds(10));
        ++m_n_sent;
      } catch (const dunedaq::appfwk::QueueTimeoutExpired& err) {
        // our output queue is stuffed.  should more be done
        // here than simply complain and drop?
        ers::error(err);
      }
      m_cache.erase(lit);
    }
  }

  // Maybe drain and send to out queue
  void drain()
  {
    std::vector<node_type> got;
    if (m_cfg.max_latency_ms) {
      m_zm.drain_prompt(std::back_inserter(got));
    } else {
      m_zm.drain_waiting(std::back_inserter(got));
    }
    send_out(got);
  }

  // Fully drain and send to out queue
  void flush()
  {
    std::vector<node_type> got;
    m_zm.drain_full(std::back_inserter(got));
    send_out(got);
  }
};
}

/// Need one of these in a .cpp for each concrete TSET type
// DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::TriggerZipper<TSET>)

#endif
// Local Variables:
// c-basic-offset: 2
// End:
