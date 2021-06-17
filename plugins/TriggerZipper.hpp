/** TriggerZipper is an appfwk::module that runs zipper::merge
 */

#ifndef TRIGGER_PLUGINS_TRIGGERZIPPER_HPP_
#define TRIGGER_PLUGINS_TRIGGERZIPPER_HPP_

#include "appfwk/DAQModule.hpp"
#include "appfwk/DAQSink.hpp"
#include "appfwk/DAQSource.hpp"
#include "appfwk/ThreadHelper.hpp"
#include "appfwk/DAQModuleHelper.hpp"

#include "zipper.hpp"

#include <list>
#include <chrono>

const char* inqs_name = "inputs";
const char* outq_name = "output";

namespace dunedaq::trigger {

// template<typename Payload,
//          typename Ordering = size_t,
//          typename Identity = size_t,
//          typename TimePoint = std::chrono::steady_clock::time_point,
//          typename 
// >

// Note, this is fake for now, it will be replaced by codegen'ed.
struct TriggerZipperCfg {
    size_t cardinality{0};
    size_t max_latency_ms{100};
};

template<typename TSET>
class TriggerZipper : public dunedaq::appfwk::DAQModule {

    // Derived types
    using tset_type = TSET;
    using ordering_type = typename TSET::timestamp_t;
    using origin_type = typename TSET::origin_t; // GeoID

    using cache_type = std::list<TSET>;
    using payload_type = typename cache_type::iterator;
    using identity_type = size_t;

    using node_type = zipper::Node<payload_type>;
    using zm_type = zipper::merge<node_type>;
    zm_type zm;

    // queues
    using source_t = appfwk::DAQSource<TSET>;
    using sink_t = appfwk::DAQSink<TSET>;
    std::unique_ptr<source_t>  inq{};
    std::unique_ptr< sink_t > outq{};
    std::chrono::milliseconds ito{10}, oto{10};

    // The generated schema provides:
    //
    // input_timeout_ms - maxium (real) time in milliseconds to wait
    // for fresh input.
    // 
    // max_latency_ms - maximum latency (real) time in milliseconds
    // that the zipper will inflict.  If zero, the zipper latency is
    // unbound but zipper is lossless to tardy input.  If nonzero,
    // tardy input will be dropped but any sets added to the zipper
    // will be held no longer than this latency even if stream are
    // incomplete.  Note, the input_timeout_ms adds to total latency.
    TriggerZipperCfg cfg{};

    std::thread thread;
    std::atomic<bool> running{false};

    // We store input TSETs in a list and send iterator though the
    // zipper as payload so as to not suffer copy overhead.
    cache_type cache;

    size_t identify(const origin_type& geoid) {
        return
            (0xff000000 & (static_cast<size_t>(geoid.system_type) << 48)) |
            (0x00ff0000 & (static_cast<size_t>(geoid.region_id) << 32)) |
            (0x0000ffff & geoid.element_id);
    }

  public:
        

    explicit TriggerZipper(const std::string& name)
        : DAQModule(name), zm()
    {
        // clang-format off
        register_command("conf",   &TriggerZipper<TSET>::do_configure);
        register_command("start",  &TriggerZipper<TSET>::do_start);
        register_command("stop",   &TriggerZipper<TSET>::do_stop);
        register_command("pause",  &TriggerZipper<TSET>::do_pause);
        register_command("resume", &TriggerZipper<TSET>::do_resume);
        register_command("scrap",  &TriggerZipper<TSET>::do_scrap);
        // clang-format on
    }
    virtual ~TriggerZipper() {}

    void init(const nlohmann::json& ini)
    {
        inq.reset(new source_t(appfwk::queue_inst(ini, "input")));
        outq.reset(new sink_t(appfwk::queue_inst(ini, "output")));
    }

    void do_configure(const nlohmann::json& /*cfgobj*/)
    {
        // fixme: we fake configuration for now
        // auto cfg = cfgobj.get<TriggerZipperCfg>();
        zm.set_cardinality(cfg.cardinality);
    }

    void do_scrap(const nlohmann::json& /*stopobj*/)
    {
        cfg = TriggerZipperCfg{};
        zm.clear();
        zm.set_cardinality(0);
    }
        
    void do_start(const nlohmann::json& /*startobj*/)
    {
        running.store(true);
        thread = std::thread(&TriggerZipper::worker, this);
    }

    void do_stop(const nlohmann::json& /*stopobj*/)
    {
        running.store(false);
        thread.join();
        // should we zm.clear() here?
    }

    void do_pause(const nlohmann::json& /*pauseobj*/)
    {
        // fixme: need a 2nd flag?
        running.store(false);
    }

    void do_resume(const nlohmann::json& /*resumeobj*/)
    {
        running.store(true);
    }

    // thread worker
    void worker() {
        while (running.load()) {
            proc_one();
        }
    }

    void proc_one()
    {
        cache.emplace_front();  // to be filled
        auto& tset = cache.front();
        try {
            inq->pop(tset, std::chrono::milliseconds(10));
        }
        catch (appfwk::QueueTimeoutExpired&) {
            cache.pop_front(); // vestigial 
            drain();
            return;
        }

        bool accepted = zm.feed(cache.begin(),tset.start_time,
                                identify(tset.origin));
        if (!accepted) {
            cache.pop_front(); // vestigial
        }
        drain();
    }

    // call to maybe produce output
    void drain()
    {
        std::vector<node_type> got;
        if (cfg.max_latency_ms) {
            zm.drain_prompt(std::back_inserter(got));
        }
        else {
            zm.drain_waiting(std::back_inserter(got));
        }

        for (auto& node : got) {
            payload_type lit = node.payload;
            auto& tset = *lit;  // list iterator
            try {
                outq->push(tset, std::chrono::milliseconds(10));
            }
            catch (const dunedaq::appfwk::QueueTimeoutExpired& err) {
                ers::warning(err);
            }
            cache.erase(lit);
        }
    }

};
}

/// Need one of these in a .cpp for each concrete TSET type
//DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::TriggerZipper<TSET>)

#endif
// Local Variables:
// c-basic-offset: 4
// End:
