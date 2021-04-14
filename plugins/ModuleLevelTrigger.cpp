/**
 * @file ModuleLevelTrigger.cpp ModuleLevelTrigger class
 * implementation
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "ModuleLevelTrigger.hpp"

#include "dataformats/ComponentRequest.hpp"

#include "dfmessages/TimeSync.hpp"
#include "dfmessages/TriggerDecision.hpp"
#include "dfmessages/TriggerInhibit.hpp"
#include "dfmessages/Types.hpp"
#include "logging/Logging.hpp"

#include "trigger/Issues.hpp"
#include "trigger/TimestampEstimator.hpp"
#include "trigger/moduleleveltrigger/Nljs.hpp"
// #include "trigger/moduleleveltriggerinfo/Nljs.hpp"

#include "appfwk/app/Nljs.hpp"

#include <algorithm>
#include <cassert>
#include <pthread.h>
#include <random>
#include <string>
#include <vector>

namespace dunedaq {
namespace trigger {

ModuleLevelTrigger::ModuleLevelTrigger(const std::string& name)
  : DAQModule(name)
  , m_token_source(nullptr)
  , m_trigger_decision_sink(nullptr)
  , m_last_trigger_number(0)
  , m_run_number(0)
{
  // clang-format off
  register_command("conf",   &ModuleLevelTrigger::do_configure);
  register_command("start",  &ModuleLevelTrigger::do_start);
  register_command("stop",   &ModuleLevelTrigger::do_stop);
  register_command("pause",  &ModuleLevelTrigger::do_pause);
  register_command("resume", &ModuleLevelTrigger::do_resume);
  register_command("scrap",  &ModuleLevelTrigger::do_scrap);
  // clang-format on
}

void
ModuleLevelTrigger::init(const nlohmann::json& iniobj)
{
  auto ini = iniobj.get<appfwk::app::ModInit>();
  for (const auto& qi : ini.qinfos) {
    if (qi.name == "trigger_decision_sink") {
      m_trigger_decision_sink.reset(new appfwk::DAQSink<dfmessages::TriggerDecision>(qi.inst));
    }
    if (qi.name == "token_source") {
      m_token_source.reset(new appfwk::DAQSource<dfmessages::TriggerDecisionToken>(qi.inst));
    }
    if (qi.name == "trigger_candidate_source") {
      m_candidate_source.reset(new appfwk::DAQSource<triggeralgs::TriggerCandidate>(qi.inst));
    }
  }
}

void
ModuleLevelTrigger::get_info(opmonlib::InfoCollector& /*ci*/, int /*level*/)
{}

void
ModuleLevelTrigger::do_configure(const nlohmann::json& confobj)
{
  auto params = confobj.get<moduleleveltrigger::ConfParams>();

  m_initial_tokens = params.initial_token_count;

  m_links.clear();
  for (auto const& link : params.links) {
    // For the future: Set APA properly
    m_links.push_back(dfmessages::GeoID{ 0, static_cast<uint32_t>(link) }); // NOLINT
  }

  m_configured_flag.store(true);
}

void
ModuleLevelTrigger::do_start(const nlohmann::json& startobj)
{
  m_run_number = startobj.value<dunedaq::dataformats::run_number_t>("run", 0);

  m_paused.store(true);
  m_running_flag.store(true);

  m_token_manager.reset(new TokenManager(m_token_source, m_initial_tokens, m_run_number));

  m_send_trigger_decisions_thread = std::thread(&ModuleLevelTrigger::send_trigger_decisions, this);
  pthread_setname_np(m_send_trigger_decisions_thread.native_handle(), "mlt-trig-dec");
}

void
ModuleLevelTrigger::do_stop(const nlohmann::json& /*stopobj*/)
{
  m_running_flag.store(false);
  m_send_trigger_decisions_thread.join();
  m_token_manager.reset(nullptr); // Calls TokenManager dtor
}

void
ModuleLevelTrigger::do_pause(const nlohmann::json& /*pauseobj*/)
{
  m_paused.store(true);
  TLOG() << "******* Triggers PAUSED! *********";
}

void
ModuleLevelTrigger::do_resume(const nlohmann::json& /*resumeobj*/)
{
  TLOG() << "******* Triggers RESUMED! *********";
  m_paused.store(false);
}

void
ModuleLevelTrigger::do_scrap(const nlohmann::json& /*stopobj*/)
{
  m_configured_flag.store(false);
}

dfmessages::TriggerDecision
ModuleLevelTrigger::create_decision(const triggeralgs::TriggerCandidate& tc)
{
  dfmessages::TriggerDecision decision;
  decision.trigger_number = m_last_trigger_number + 1;
  decision.run_number = m_run_number;
  decision.trigger_timestamp = tc.time_candidate;
  // TODO: work out what to set this to
  decision.trigger_type = 1; // m_trigger_type;

  for (auto link : m_links) {
    dfmessages::ComponentRequest request;
    request.component = link;
    // TODO: set these from some config map
    request.window_begin = tc.time_candidate;
    request.window_end = tc.time_candidate + 1000;

    decision.components.push_back(request);
  }

  return decision;
}

void
ModuleLevelTrigger::send_trigger_decisions()
{

  // We get here at start of run, so reset the trigger number
  m_last_trigger_number = 0;
  m_trigger_count.store(0);
  m_trigger_count_tot.store(0);
  m_inhibited_trigger_count.store(0);
  m_inhibited_trigger_count_tot.store(0);

  while (m_running_flag.load()) {
    triggeralgs::TriggerCandidate tc;
    try {
      m_candidate_source->pop(tc, std::chrono::milliseconds(100));
    } catch (appfwk::QueueTimeoutExpired&) {
      continue;
    }

    bool tokens_allow_triggers = m_token_manager->triggers_allowed();
    if (!m_paused.load() && tokens_allow_triggers) {

      dfmessages::TriggerDecision decision = create_decision(tc);

      TLOG_DEBUG(1) << "Pushing a decision with triggernumber " << decision.trigger_number << " timestamp "
                    << decision.trigger_timestamp << " number of links " << decision.components.size();
      try {
        m_trigger_decision_sink->push(decision, std::chrono::milliseconds(10));
      } catch (appfwk::QueueTimeoutExpired& e) {
        ers::error(e);
      }
      m_token_manager->trigger_sent(decision.trigger_number);
      decision.trigger_number++;
      m_last_trigger_number++;
      m_trigger_count++;
      m_trigger_count_tot++;
    } else if (!tokens_allow_triggers) {
      TLOG_DEBUG(1) << "There are no Tokens available. Not sending a TriggerDecision for timestamp ";
      m_inhibited_trigger_count++;
      m_inhibited_trigger_count_tot++;
    } else {
      TLOG_DEBUG(1) << "Triggers are paused. Not sending a TriggerDecision ";
    }
  }
}

} // namespace trigger
} // namespace dunedaq

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::ModuleLevelTrigger)

// Local Variables:
// c-basic-offset: 2
// End: