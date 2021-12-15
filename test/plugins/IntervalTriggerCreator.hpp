/**
 * This is part of the DUNE DAQ Software Suite, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_TEST_PLUGINS_INTERVALTRIGGERCREATOR_HPP_
#define TRIGGER_TEST_PLUGINS_INTERVALTRIGGERCREATOR_HPP_

#include "trigger/TokenManager.hpp"

#include "daqdataformats/GeoID.hpp"
#include "dfmessages/TimeSync.hpp"
#include "dfmessages/TriggerDecision.hpp"
#include "dfmessages/TriggerDecisionToken.hpp"
#include "dfmessages/TriggerInhibit.hpp"
#include "dfmessages/Types.hpp"

#include "timinglibs/TimestampEstimator.hpp"

#include "appfwk/DAQModule.hpp"
#include "appfwk/DAQSink.hpp"
#include "appfwk/DAQSource.hpp"

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace dunedaq {

namespace trigger {

/**
 * @brief IntervalTriggerCreator creates TriggerDecisions at regular intervals, based on input from a TimeSync queue
 */
class IntervalTriggerCreator : public dunedaq::appfwk::DAQModule
{
public:
  /**
   * @brief IntervalTriggerCreator Constructor
   * @param name Instance name for this IntervalTriggerCreator instance
   */
  explicit IntervalTriggerCreator(const std::string& name);

  // clang-format off
  IntervalTriggerCreator(const IntervalTriggerCreator&) = delete;            ///< IntervalTriggerCreator is not copy-constructible
  IntervalTriggerCreator& operator=(const IntervalTriggerCreator&) = delete; ///< IntervalTriggerCreator is not copy-assignable
  IntervalTriggerCreator(IntervalTriggerCreator&&) = delete;                 ///< IntervalTriggerCreator is not move-constructible
  IntervalTriggerCreator& operator=(IntervalTriggerCreator&&) = delete;      ///< IntervalTriggerCreator is not move-assignable
  // clang-format on

  void init(const nlohmann::json& iniobj) override;
  void get_info(opmonlib::InfoCollector& ci, int level) override;

private:
  // Commands
  void do_configure(const nlohmann::json& obj);
  void do_start(const nlohmann::json& obj);
  void do_stop(const nlohmann::json& obj);
  void do_scrap(const nlohmann::json& obj);

  void send_trigger_decisions();
  std::thread m_send_trigger_decisions_thread;

  std::unique_ptr<timinglibs::TimestampEstimator> m_timestamp_estimator;

  // Create the next trigger decision
  dfmessages::TriggerDecision create_decision(dfmessages::timestamp_t timestamp);

  // Queue sources and sinks
  std::unique_ptr<appfwk::DAQSource<dfmessages::TimeSync>> m_time_sync_source;
  std::unique_ptr<appfwk::DAQSink<dfmessages::TriggerDecision>> m_trigger_decision_sink;

  // Variables controlling how we produce triggers

  // Triggers are produced for timestamps:
  //    m_trigger_offset + n*m_trigger_interval_ticks;
  // with n integer.
  //
  // A trigger for timestamp t is emitted approximately
  // `m_trigger_delay_ticks` ticks after the timestamp t is
  // estimated to occur, so we can try not to emit trigger requests
  // for data that's in the future
  dfmessages::timestamp_t m_trigger_offset{ 0 };
  std::atomic<dfmessages::timestamp_t> m_trigger_interval_ticks{ 0 };
  int trigger_delay_ticks_{ 0 };

  // The offset and width of the windows to be requested in the trigger
  daqdataformats::timestamp_diff_t m_trigger_window_offset{ 0 };
  dfmessages::timestamp_t m_min_readout_window_ticks{ 0 };
  dfmessages::timestamp_t m_max_readout_window_ticks{ 0 };

  // The trigger type for the trigger requests
  dfmessages::trigger_type_t m_trigger_type{ 0xff };

  // The link IDs which should be read out in the trigger decision
  std::vector<dfmessages::GeoID> m_links;
  int m_min_links_in_request;
  int m_max_links_in_request;

  int m_repeat_trigger_count{ 1 };

  uint64_t m_clock_frequency_hz; // NOLINT

  // At stop, send this number of triggers in one go. The idea here is
  // to put lots of triggers into the system to check that the stop
  // sequence is clean, in the sense of all the in-flight triggers
  // getting to disk
  int m_stop_burst_count{ 0 };

  dfmessages::trigger_number_t m_last_trigger_number;
  dfmessages::run_number_t m_run_number;

  // Are we in the RUNNING state?
  std::atomic<bool> m_running_flag{ false };
  // Are we in a configured state, ie after conf and before scrap?
  std::atomic<bool> m_configured_flag{ false };
};
} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_TEST_PLUGINS_INTERVALTRIGGERCREATOR_HPP_

// Local Variables:
// c-basic-offset: 2
// End:
