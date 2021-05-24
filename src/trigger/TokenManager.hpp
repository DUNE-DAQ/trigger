/**
 * @file TokenManager.hpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_SRC_TRIGGER_TOKENMANAGER_HPP_
#define TRIGGER_SRC_TRIGGER_TOKENMANAGER_HPP_

#include "appfwk/DAQSource.hpp"

#include "dfmessages/TimeSync.hpp"
#include "dfmessages/TriggerDecisionToken.hpp"
#include "dfmessages/Types.hpp"

#include <atomic>
#include <memory>
#include <set>
#include <thread>

namespace dunedaq {
namespace trigger {

/**
 * @brief TokenManager keeps track of the number of in-flight trigger decisions.
 *
 * TokenManager implements a credit-based system for trigger
 * inhibits. It is constructed with an initial number of tokens and a
 * queue of @a dfmessages::TriggerDecisionToken. When a trigger
 * decision is sent, the number of tokens is decremented, and when a
 * TriggerDecisionToken is received on the queue, the number of tokens
 * is incremented. When the count of available tokens reaches zero, no
 * further TriggerDecisions may be issued.
 */
class TokenManager
{
public:
  TokenManager(std::unique_ptr<appfwk::DAQSource<dfmessages::TriggerDecisionToken>>& token_source,
               int initial_tokens,
               dataformats::run_number_t run_number);

  virtual ~TokenManager();

  TokenManager(TokenManager const&) = delete;
  TokenManager(TokenManager&&) = default;
  TokenManager& operator=(TokenManager const&) = delete;
  TokenManager& operator=(TokenManager&&) = default;

  /**
   *  Get the number of available tokens
   */
  int get_n_tokens() const;

  /**
   * Are tokens currently available, to allow sending of new trigger decisions?
   */
  bool triggers_allowed() const { return get_n_tokens() > 0; }

  /**
   * Notify TokenManager that a trigger decision has been sent. This
   * decreases the number of available tokens by one.
   *
   * Note: you should call this function *before* pushing the corresponding TriggerDecision
   * to its output queue. If you do these steps in the other order, the TriggerComplete message
   * may be returned before TokenManager is aware of the corresponding trigger decision
   */
  void trigger_sent(dfmessages::trigger_number_t);

private:
  // The main thread
  void read_token_queue();
  std::thread m_read_queue_thread;

  // Are we running?
  std::atomic<bool> m_running_flag;
  // How many tokens are currently available?
  std::atomic<int> m_n_tokens;

  // The currently-in-flight trigger decisions, and a mutex to guard it
  std::set<dfmessages::trigger_number_t> m_open_trigger_decisions;
  std::mutex m_open_trigger_decisions_mutex;

  std::unique_ptr<appfwk::DAQSource<dfmessages::TriggerDecisionToken>>& m_token_source;
  dataformats::run_number_t m_run_number;
};

} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_SRC_TRIGGER_TOKENMANAGER_HPP_
