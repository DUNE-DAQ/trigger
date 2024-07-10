/**
 * @file TriggerCandidateMaker.hpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_PLUGINS_TRIGGERCANDIDATEMAKER_HPP_
#define TRIGGER_PLUGINS_TRIGGERCANDIDATEMAKER_HPP_

#include "trigger/TriggerGenericMaker.hpp"

#include "trigger/TASet.hpp"
#include "triggeralgs/TriggerActivity.hpp"
#include "triggeralgs/TriggerCandidate.hpp"
#include "triggeralgs/TriggerCandidateMaker.hpp"

#include <memory>
#include <string>

namespace dunedaq::trigger {

class TriggerCandidateMaker
  : public TriggerGenericMaker<triggeralgs::TriggerActivity,
                               triggeralgs::TriggerCandidate,
                               triggeralgs::TriggerCandidateMaker>
{
public:
  explicit TriggerCandidateMaker(const std::string& name)
    : TriggerGenericMaker(name)
  {
  }

  TriggerCandidateMaker(const TriggerCandidateMaker&) = delete;
  TriggerCandidateMaker& operator=(const TriggerCandidateMaker&) = delete;
  TriggerCandidateMaker(TriggerCandidateMaker&&) = delete;
  TriggerCandidateMaker& operator=(TriggerCandidateMaker&&) = delete;

private:
  virtual std::unique_ptr<triggeralgs::TriggerCandidateMaker> make_maker(const nlohmann::json& obj);
};

} // namespace dunedaq::trigger

#endif // TRIGGER_PLUGINS_TRIGGERCANDIDATEMAKER_HPP_
