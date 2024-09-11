/**
 * @file MLTModule.cpp MLTModule class
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

#include "MLTModule.hpp"

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

MLTModule::MLTModule(const std::string& name)
  : DAQModule(name)
  , m_last_trigger_number(0)
  , m_run_number(0)
{
  // clang-format off
  //register_command("conf",   &MLTModule::do_configure);
  register_command("start",  &MLTModule::do_start);
  register_command("stop",   &MLTModule::do_stop);
  register_command("disable_triggers",  &MLTModule::do_pause);
  register_command("enable_triggers", &MLTModule::do_resume);
//  register_command("scrap",  &MLTModule::do_scrap);
  // clang-format on
}

void
MLTModule::init(std::shared_ptr<appfwk::ModuleConfiguration> mcfg)
{
  auto mtrg = mcfg->module<appmodel::MLTModule>(get_name());

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
  m_latency_monitoring = mtrg->get_configuration()->get_latency_monitoring_conf()->get_latency_monitoring();
  m_configured_flag.store(true);
}

void
MLTModule::generate_opmon_data()
{
  opmon::ModuleLevelTriggerInfo info;

  info.set_td_msg_received_count( m_td_msg_received_count.load() );
  info.set_td_sent_count( m_td_sent_count.load() );
  info.set_td_inhibited_count( m_td_inhibited_count.load() );
  info.set_td_paused_count( m_td_paused_count.load() );
  info.set_td_queue_timeout_expired_err_count( m_td_queue_timeout_expired_err_count.load() );
  info.set_td_total_count( m_td_total_count.load() );

  if (m_lc_started) {
    info.set_lc_klive( m_livetime_counter->get_time(LivetimeCounter::State::kLive) );
    info.set_lc_kpaused( m_livetime_counter->get_time(LivetimeCounter::State::kPaused) );
    info.set_lc_kdead( m_livetime_counter->get_time(LivetimeCounter::State::kDead) );
  } else {
    info.set_lc_klive( m_lc_kLive);
    info.set_lc_kpaused( m_lc_kPaused );
    info.set_lc_kdead( m_lc_kDead );
  }

  this->publish(std::move(info));

  // per TC type
  std::lock_guard<std::mutex>   guard(m_trigger_mutex);
  for ( auto & [type, counts] : m_trigger_counters ) {
    auto name = dunedaq::trgdataformats::get_trigger_candidate_type_names()[type];
    opmon::TriggerDecisionInfo td_info;
    td_info.set_received(counts.received.exchange(0));
    td_info.set_sent(counts.sent.exchange(0));
    td_info.set_failed_send(counts.failed_send.exchange(0));
    td_info.set_paused(counts.paused.exchange(0));
    td_info.set_inhibited(counts.inhibited.exchange(0));
    this->publish( std::move(td_info), {{"type", name}} );
  }

  // latency
  if ( m_running_flag.load() && m_latency_monitoring.load() ) {
    // TC in, TD out
    opmon::TriggerLatency lat_info;
    lat_info.set_latency_in( m_latency_instance.get_latency_in() );
    lat_info.set_latency_out( m_latency_instance.get_latency_out() );
    this->publish(std::move(lat_info));

    // vs readout window requests
    opmon::ModuleLevelTriggerRequestLatency lat_request_info;
    lat_request_info.set_latency_window_start( m_latency_requests_instance.get_latency_in() );
    lat_request_info.set_latency_window_end( m_latency_requests_instance.get_latency_out() );
    this->publish(std::move(lat_request_info));
  }
}

void
MLTModule::do_start(const nlohmann::json& startobj)
{
  m_run_number = startobj.value<dunedaq::daqdataformats::run_number_t>("run", 0);
  // We get here at start of run, so reset the trigger number
  m_last_trigger_number = 0;

  // OpMon.
  m_td_msg_received_count.store(0);
  m_td_sent_count.store(0);
  m_td_total_count.store(0);
  // OpMon DFO
  m_td_inhibited_count.store(0);
  m_td_paused_count.store(0);
  m_td_queue_timeout_expired_err_count.store(0);
  // OpMon Livetime counter
  m_lc_kLive.store(0);
  m_lc_kPaused.store(0);
  m_lc_kDead.store(0);

  m_paused.store(true);
  m_running_flag.store(true);
  m_dfo_is_busy.store(false);

  m_livetime_counter.reset(new LivetimeCounter(LivetimeCounter::State::kPaused));
  m_lc_started = true;

  m_inhibit_input->add_callback(std::bind(&MLTModule::dfo_busy_callback, this, std::placeholders::_1));
  m_decision_input->add_callback(std::bind(&MLTModule::trigger_decisions_callback, this, std::placeholders::_1));
  //m_send_trigger_decisions_thread = std::thread(&MLTModule::send_trigger_decisions, this);
  //pthread_setname_np(m_send_trigger_decisions_thread.native_handle(), "mlt-dec"); // TODO: originally mlt-trig-dec

  ers::info(TriggerStartOfRun(ERS_HERE, m_run_number));

}

void
MLTModule::do_stop(const nlohmann::json& /*stopobj*/)
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
  m_lc_started = false; 

  print_opmon_stats();

  ers::info(TriggerEndOfRun(ERS_HERE, m_run_number));
}

void
MLTModule::do_pause(const nlohmann::json& /*pauseobj*/)
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
MLTModule::do_resume(const nlohmann::json& /*resumeobj*/)
{
  ers::info(TriggerActive(ERS_HERE));
  TLOG() << "******* Triggers RESUMED! in run " << m_run_number << " *********";
  m_livetime_counter->set_state(LivetimeCounter::State::kLive);
  m_lc_started = true;
  m_paused.store(false);
  TLOG_DEBUG(5) << "TS Start: "
                << std::chrono::duration_cast<std::chrono::microseconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();
}

void
MLTModule::trigger_decisions_callback(dfmessages::TriggerDecision& decision )
{
    m_td_msg_received_count++;
    if (m_latency_monitoring.load()) m_latency_instance.update_latency_in( decision.trigger_timestamp );

    auto trigger_types = unpack_types(decision.trigger_type);
    for ( const auto t : trigger_types ) {
      ++get_trigger_counter(t).received;
    }

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

      try {
        m_decision_output->send(std::move(decision), std::chrono::milliseconds(1));

	// readout window latency update
        if (m_latency_monitoring.load()) { 
	  m_latency_requests_instance.update_latency_in( decision.components.front().window_begin );
          m_latency_requests_instance.update_latency_out( decision.components.front().window_end );
	}

        m_td_sent_count++;

        for ( const auto t : trigger_types ) {
          ++get_trigger_counter(t).sent;
        }

        m_last_trigger_number++;
//        add_td(pending_td);
      } catch (const ers::Issue& e) {
        ers::error(e);
        TLOG_DEBUG(1) << "The network is misbehaving: TD send failed for "
                      << m_last_trigger_number;
        m_td_queue_timeout_expired_err_count++;

        for ( const auto t : trigger_types ) {
          ++get_trigger_counter(t).failed_send;
        }
      }

    } else if (m_paused.load()) {
      ++m_td_paused_count;
      for ( const auto t : trigger_types ) {
        ++get_trigger_counter(t).paused;
      }

      TLOG_DEBUG(1) << "Triggers are paused. Not sending a TriggerDecision for TD with timestamp and type "
                    << ts << "/" << tt;
    } else {
      ers::warning(TriggerInhibited(ERS_HERE, m_run_number));
      TLOG_DEBUG(1) << "The DFO is busy. Not sending a TriggerDecision with timestamp and type "
                    << ts << "/" << tt;
      m_td_inhibited_count++;
      for ( const auto t : trigger_types ) {
        ++get_trigger_counter(t).inhibited;
      }

    }
    if (m_latency_monitoring.load()) m_latency_instance.update_latency_out( decision.trigger_timestamp );
    m_td_total_count++;
}

void
MLTModule::dfo_busy_callback(dfmessages::TriggerInhibit& inhibit)
{
  TLOG_DEBUG(17) << "Received inhibit message with busy status " << inhibit.busy << " and run number "
                 << inhibit.run_number;
  if (inhibit.run_number == m_run_number) {
    TLOG_DEBUG(18) << "Changing our flag for the DFO busy state from " << m_dfo_is_busy.load() << " to "
                   << inhibit.busy;
    m_dfo_is_busy = inhibit.busy;
    LivetimeCounter::State state = (inhibit.busy) ? LivetimeCounter::State::kDead : LivetimeCounter::State::kLive;
    m_livetime_counter->set_state(state);
  }
}

void
MLTModule::print_opmon_stats()
{
  TLOG() << "MLT opmon counters summary:";
  TLOG() << "------------------------------";
  TLOG() << "Received TD messages: \t" << m_td_msg_received_count;
  TLOG() << "Sent TDs: \t\t\t" << m_td_sent_count;
  TLOG() << "Inhibited TDs: \t\t" << m_td_inhibited_count;
  TLOG() << "Paused TDs: \t\t\t" << m_td_paused_count;
  TLOG() << "Queue timeout TDs: \t\t" << m_td_queue_timeout_expired_err_count;
  TLOG() << "Total TDs: \t\t\t" << m_td_total_count;
  TLOG() << "------------------------------";
  TLOG() << "Livetime::Live: \t" << m_lc_kLive;
  TLOG() << "Livetime::Paused: \t" << m_lc_kPaused;
  TLOG() << "Livetime::Dead: \t" << m_lc_kDead;
  TLOG();
}

} // namespace trigger
} // namespace dunedaq

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::MLTModule)

// Local Variables:
// c-basic-offset: 2
// End:
