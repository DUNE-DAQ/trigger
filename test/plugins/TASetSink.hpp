/**
 * @file TASetSink.hpp
 *
 * TASetSink is a DAQModule that emulates the data flow application for the
 * sole purpose of testing trigger modules standalone.
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_TEST_PLUGINS_TASETSINK_HPP_
#define TRIGGER_TEST_PLUGINS_TASETSINK_HPP_

#include "trigger/TASet.hpp"
#include "trigger/tasetsink/Nljs.hpp"

#include "appfwk/DAQModule.hpp"
#include "iomanager/Receiver.hpp"

#include <fstream>
#include <memory>

namespace dunedaq {

namespace trigger {

/**
 * @brief TASetSink receives TASets from a queue and prints them out
 */
class TASetSink : public dunedaq::appfwk::DAQModule
{
public:
  /**
   * @brief TASetSink Constructor
   * @param name Instance name for this TASetSink instance
   */
  explicit TASetSink(const std::string& name);

  TASetSink(const TASetSink&) = delete;            ///< TASetSink is not copy-constructible
  TASetSink& operator=(const TASetSink&) = delete; ///< TASetSink is not copy-assignable
  TASetSink(TASetSink&&) = delete;                 ///< TASetSink is not move-constructible
  TASetSink& operator=(TASetSink&&) = delete;      ///< TASetSink is not move-assignable

  virtual void init(std::shared_ptr<dunedaq::appfwk::ModuleConfiguration>) override;

private:
  // Commands
  void do_start(const nlohmann::json& obj);
  void do_stop(const nlohmann::json& obj);
  void do_conf(const nlohmann::json& obj);

  void do_work();

  std::atomic<bool> m_running_flag{ false };
  std::thread m_thread;
  std::ofstream m_outfile;
  
  tasetsink::Conf m_conf;
  // Queue sources and sinks
  using source_t = iomanager::ReceiverConcept<TASet>;
  std::shared_ptr<source_t> m_taset_source;
};
} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_TEST_PLUGINS_TASETSINK_HPP_
