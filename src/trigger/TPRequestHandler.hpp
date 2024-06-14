/**
 * @file TPRequestHandler.hpp Trigger matching mechanism 
 * used for skip list based LBs in readout models
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef TRIGGER_SRC_TRIGGER_TPREQUESTHANDLER_HPP_
#define TRIGGER_SRC_TRIGGER_TPREQUESTHANDLER_HPP_

#include "iomanager/IOManager.hpp"
#include "iomanager/Sender.hpp"

#include "appmodel/DataHandlerModule.hpp"

#include "readoutlibs/ReadoutLogging.hpp"
#include "readoutlibs/utils/ReusableThread.hpp"
#include "readoutlibs/FrameErrorRegistry.hpp"
#include "readoutlibs/ReadoutIssues.hpp"
#include "readoutlibs/ReadoutLogging.hpp"
#include "readoutlibs/models/DefaultSkipListRequestHandler.hpp"

#include "trigger/TriggerPrimitiveTypeAdapter.hpp"
#include "trigger/TPSet.hpp"
 
#include <atomic>
#include <memory>
#include <string>

using dunedaq::readoutlibs::logging::TLVL_WORK_STEPS;


namespace dunedaq {
ERS_DECLARE_ISSUE(trigger,
                  DroppedTPSet,
                  "Failed to send TPs from  " << s_ts << " to " << e_ts,
                  ((uint64_t)s_ts) 
		  ((uint64_t)e_ts))
ERS_DECLARE_ISSUE(trigger,
                   TPHandlerMsg,
                   infomsg,
                   ((std::string) infomsg))

namespace trigger {

class TPRequestHandler : public dunedaq::readoutlibs::DefaultSkipListRequestHandler<TriggerPrimitiveTypeAdapter>
{
public:
  using inherited2 = readoutlibs::DefaultSkipListRequestHandler<TriggerPrimitiveTypeAdapter>;

  // Constructor that binds LB and error registry

  TPRequestHandler(std::unique_ptr<readoutlibs::SkipListLatencyBufferModel<TriggerPrimitiveTypeAdapter>>& latency_buffer,
                                std::unique_ptr<readoutlibs::FrameErrorRegistry>& error_registry)
    : readoutlibs::DefaultSkipListRequestHandler<TriggerPrimitiveTypeAdapter>(
        latency_buffer,
        error_registry)
  {
    TLOG_DEBUG(TLVL_WORK_STEPS) << "TPRequestHandler created...";
  }
 
  void conf(const appmodel::DataHandlerModule* conf) override;
  void start(const nlohmann::json& args) override;
  void periodic_data_transmission() override;
  
private:
  using timestamp_t = std::uint64_t;
  std::shared_ptr<iomanager::SenderConcept<dunedaq::trigger::TPSet>> m_tpset_sink;
  uint64_t m_run_number;
  uint64_t m_next_tpset_seqno;

  timestamp_t m_oldest_ts=0;
  timestamp_t m_newest_ts=0;
  timestamp_t m_start_win_ts=0;
  timestamp_t m_end_win_ts=0;
  bool m_first_cycle = true;
  uint64_t m_ts_set_sender_offset_ticks = 6250000; // 100 ms delay in transmission

};

} // namespace trigger
} // namespace dunedaq


#endif // TRIGGER_SRC_TRIGGER_TPREQUESTHANDLER_HPP_
