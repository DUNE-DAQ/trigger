/**
 * @file ModuleLevelTrigger.cpp ModuleLevelTrigger class
 * implementation
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

/**
 * TODO: get_group_links
 * TODO: get_mandatory_links
 */

#include "ModuleLevelTrigger.hpp"

#include "trigger/Issues.hpp"
#include "trigger/LivetimeCounter.hpp"

#include "appfwk/app/Nljs.hpp"
#include "daqdataformats/ComponentRequest.hpp"
#include "dfmessages/TriggerDecision.hpp"
#include "dfmessages/TriggerInhibit.hpp"
#include "dfmessages/Types.hpp"
#include "iomanager/IOManager.hpp"
#include "logging/Logging.hpp"
#include "trgdataformats/Types.hpp"

namespace dunedaq {
namespace trigger {

ModuleLevelTrigger::ModuleLevelTrigger(const std::string& name)
  : DAQModule(name)
  , m_last_trigger_number(0)
  , m_run_number(0)
{
  // clang-format off
  //register_command("conf",   &ModuleLevelTrigger::do_configure);
  register_command("start",  &ModuleLevelTrigger::do_start);
  register_command("stop",   &ModuleLevelTrigger::do_stop);
  register_command("disable_triggers",  &ModuleLevelTrigger::do_pause);
  register_command("enable_triggers", &ModuleLevelTrigger::do_resume);
//  register_command("scrap",  &ModuleLevelTrigger::do_scrap);
  // clang-format on
}

void
ModuleLevelTrigger::init(std::shared_ptr<appfwk::ModuleConfiguration> mcfg)
{
  auto mtrg = mcfg->module<appmodel::ModuleLevelTrigger>(get_name());

  // Get the inputs
  std::string candidate_input;
  std::string inhibit_input;
  for(auto con : mtrg->get_inputs()){
    if(con->get_data_type() == datatype_to_string<dfmessages::TriggerDecision>()) {
      m_decision_input = get_iom_receiver<dfmessages::TriggerDecision>(con->UID());
    } else if(con->get_data_type() == datatype_to_string<dfmessages::TriggerInhibit>()) {
      m_inhibit_input = get_iom_receiver<dfmessages::TriggerInhibit>(con->UID());
    }
  }

  // Get the outputs
  for(auto con : mtrg->get_outputs()){
    if(con->get_data_type() == datatype_to_string<dfmessages::TriggerDecision>())
      m_decision_output = get_iom_sender<dfmessages::TriggerDecision>(con->UID());
  }

  // Now do the configuration: dummy for now
  m_configured_flag.store(true);
}

void
ModuleLevelTrigger::get_info(opmonlib::InfoCollector& ci, int /*level*/)
{
  moduleleveltriggerinfo::Info i;

  i.tc_received_count = m_tc_received_count.load();
  i.tc_ignored_count = m_tc_ignored_count.load();
  i.td_sent_count = m_td_sent_count.load();
  i.new_td_sent_count = m_new_td_sent_count.exchange(0);
  i.td_sent_tc_count = m_td_sent_tc_count.load();
  i.td_inhibited_count = m_td_inhibited_count.load();
  i.new_td_inhibited_count = m_new_td_inhibited_count.exchange(0);
  i.td_inhibited_tc_count = m_td_inhibited_tc_count.load();
  i.td_paused_count = m_td_paused_count.load();
  i.td_paused_tc_count = m_td_paused_tc_count.load();
  i.td_dropped_count = m_td_dropped_count.load();
  i.td_dropped_tc_count = m_td_dropped_tc_count.load();
  i.td_cleared_count = m_td_cleared_count.load();
  i.td_cleared_tc_count = m_td_cleared_tc_count.load();
  i.td_not_triggered_count = m_td_not_triggered_count.load();
  i.td_not_triggered_tc_count = m_td_not_triggered_tc_count.load();
  i.td_total_count = m_td_total_count.load();
  i.new_td_total_count = m_new_td_total_count.exchange(0);

  if (m_livetime_counter.get() != nullptr) {
    i.lc_kLive = m_livetime_counter->get_time(LivetimeCounter::State::kLive);
    i.lc_kPaused = m_livetime_counter->get_time(LivetimeCounter::State::kPaused);
    i.lc_kDead = m_livetime_counter->get_time(LivetimeCounter::State::kDead);
  }

  ci.add(i);
}

void
ModuleLevelTrigger::do_start(const nlohmann::json& startobj)
{
  m_run_number = startobj.value<dunedaq::daqdataformats::run_number_t>("run", 0);
  // We get here at start of run, so reset the trigger number
  m_last_trigger_number = 0;

  // OpMon.
  m_tc_received_count.store(0);
  m_tc_ignored_count.store(0);
  m_td_sent_count.store(0);
  m_td_sent_tc_count.store(0);
  m_td_inhibited_count.store(0);
  m_td_inhibited_tc_count.store(0);
  m_td_paused_count.store(0);
  m_td_paused_tc_count.store(0);
  m_td_dropped_count.store(0);
  m_td_dropped_tc_count.store(0);
  m_td_cleared_count.store(0);
  m_td_cleared_tc_count.store(0);
  m_td_not_triggered_count.store(0);
  m_td_not_triggered_tc_count.store(0);
  m_td_total_count.store(0);
  m_lc_kLive.store(0);
  m_lc_kPaused.store(0);
  m_lc_kDead.store(0);

  m_paused.store(true);
  m_running_flag.store(true);
  m_dfo_is_busy.store(false);

  m_livetime_counter.reset(new LivetimeCounter(LivetimeCounter::State::kPaused));

  m_inhibit_input->add_callback(std::bind(&ModuleLevelTrigger::dfo_busy_callback, this, std::placeholders::_1));
  m_decision_input->add_callback(std::bind(&ModuleLevelTrigger::trigger_decisions_callback, this, std::placeholders::_1));
  //m_send_trigger_decisions_thread = std::thread(&ModuleLevelTrigger::send_trigger_decisions, this);
  //pthread_setname_np(m_send_trigger_decisions_thread.native_handle(), "mlt-dec"); // TODO: originally mlt-trig-dec

  ers::info(TriggerStartOfRun(ERS_HERE, m_run_number));

}

void
ModuleLevelTrigger::do_stop(const nlohmann::json& /*stopobj*/)
{
  m_running_flag.store(false);
  m_decision_input->remove_callback();
  m_inhibit_input->remove_callback();
  //m_send_trigger_decisions_thread.join();
  m_lc_kLive_count = m_livetime_counter->get_time(LivetimeCounter::State::kLive);
  m_lc_kPaused_count = m_livetime_counter->get_time(LivetimeCounter::State::kPaused);
  m_lc_kDead_count = m_livetime_counter->get_time(LivetimeCounter::State::kDead);
  m_lc_kLive = m_lc_kLive_count;
  m_lc_kPaused = m_lc_kPaused_count;
  m_lc_kDead = m_lc_kDead_count;

  m_lc_deadtime = m_livetime_counter->get_time(LivetimeCounter::State::kDead) +
                  m_livetime_counter->get_time(LivetimeCounter::State::kPaused);

  TLOG(3) << "LivetimeCounter - total deadtime+paused: " << m_lc_deadtime << std::endl;
  m_livetime_counter.reset(); // Calls LivetimeCounter dtor?

  ers::info(TriggerEndOfRun(ERS_HERE, m_run_number));
}

void
ModuleLevelTrigger::do_pause(const nlohmann::json& /*pauseobj*/)
{

  m_paused.store(true);
  m_livetime_counter->set_state(LivetimeCounter::State::kPaused);
  TLOG() << "******* Triggers PAUSED! in run " << m_run_number << " *********";
  ers::info(TriggerPaused(ERS_HERE));
  TLOG_DEBUG(5) << "TS End: "
                << std::chrono::duration_cast<std::chrono::microseconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();
}

void
ModuleLevelTrigger::do_resume(const nlohmann::json& /*resumeobj*/)
{
  ers::info(TriggerActive(ERS_HERE));
  TLOG() << "******* Triggers RESUMED! in run " << m_run_number << " *********";
  m_livetime_counter->set_state(LivetimeCounter::State::kLive);
  m_paused.store(false);
  TLOG_DEBUG(5) << "TS Start: "
                << std::chrono::duration_cast<std::chrono::microseconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();
}

void
ModuleLevelTrigger::trigger_decisions_callback(dfmessages::TriggerDecision& decision )
{
    auto ts = decision.trigger_timestamp;
    auto tt = decision.trigger_type;
    decision.run_number = m_run_number;
    decision.trigger_number = m_last_trigger_number;

    TLOG() << "Received decision with timestamp "
             << decision.trigger_timestamp ;
    
    if ((!m_paused.load() && !m_dfo_is_busy.load())) {
      TLOG_DEBUG(1) << "Sending a decision with triggernumber " << decision.trigger_number << " timestamp "
             << decision.trigger_timestamp << " start " << decision.components.front().window_begin << " end " << decision.components.front().window_end
 	     << " number of links " << decision.components.size();

      //using namespace std::chrono;
      // uint64_t end_lat_prescale = duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
      try {
        m_decision_output->send(std::move(decision), std::chrono::milliseconds(1));
        m_td_sent_count++;
        m_new_td_sent_count++;
//        m_td_sent_tc_count += pending_td.contributing_tcs.size();
        m_last_trigger_number++;
//        add_td(pending_td);
      } catch (const ers::Issue& e) {
        ers::error(e);
        TLOG_DEBUG(1) << "The network is misbehaving: TD send failed for "
                      << m_last_trigger_number;
        m_td_queue_timeout_expired_err_count++;
        //m_td_queue_timeout_expired_err_tc_count += pending_td.contributing_tcs.size();
      }

    } else if (m_paused.load()) {
      ++m_td_paused_count;
      //m_td_paused_tc_count += pending_td.contributing_tcs.size();
      TLOG_DEBUG(1) << "Triggers are paused. Not sending a TriggerDecision for TD with timestamp and type "
                    << ts << "/" << tt;
    } else {
      ers::warning(TriggerInhibited(ERS_HERE, m_run_number));
      TLOG_DEBUG(1) << "The DFO is busy. Not sending a TriggerDecision with timestamp and type "
                    << ts << "/" << tt;
      m_td_inhibited_count++;
      m_new_td_inhibited_count++;
      //m_td_inhibited_tc_count += pending_td.contributing_tcs.size();
    }
    m_td_total_count++;
    m_new_td_total_count++;
   
}

void
ModuleLevelTrigger::dfo_busy_callback(dfmessages::TriggerInhibit& inhibit)
{
  TLOG_DEBUG(17) << "Received inhibit message with busy status " << inhibit.busy << " and run number "
                 << inhibit.run_number;
  if (inhibit.run_number == m_run_number) {
    TLOG_DEBUG(18) << "Changing our flag for the DFO busy state from " << m_dfo_is_busy.load() << " to "
                   << inhibit.busy;
    m_dfo_is_busy = inhibit.busy;
    m_livetime_counter->set_state(LivetimeCounter::State::kDead);
  }
}


} // namespace trigger
} // namespace dunedaq

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::ModuleLevelTrigger)

// Local Variables:
// c-basic-offset: 2
// End:
