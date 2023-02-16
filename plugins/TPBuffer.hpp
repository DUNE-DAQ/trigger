/**
 * @file TPBuffer.cpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2021.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_PLUGINS_TPBUFFER_HPP_
#define TRIGGER_PLUGINS_TPBUFFER_HPP_

#include "iomanager/Receiver.hpp"
#include "readoutlibs/FrameErrorRegistry.hpp"
#include "readoutlibs/models/DefaultSkipListRequestHandler.hpp"
#include "readoutlibs/models/SkipListLatencyBufferModel.hpp"
#include "triggeralgs/TriggerPrimitive.hpp"
#include "utilities/WorkerThread.hpp"

#include "trigger/Issues.hpp"
#include "trigger/TPSet.hpp"

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace dunedaq {
namespace trigger {
class TPBuffer : public dunedaq::appfwk::DAQModule
{
public:
  explicit TPBuffer(const std::string& name);

  TPBuffer(const TPBuffer&) = delete;
  TPBuffer& operator=(const TPBuffer&) = delete;
  TPBuffer(TPBuffer&&) = delete;
  TPBuffer& operator=(TPBuffer&&) = delete;

  void init(const nlohmann::json& iniobj) override;
  void get_info(opmonlib::InfoCollector& ci, int level) override;

private:

  struct TPWrapper
  {
    triggeralgs::TriggerPrimitive primitive;

    // Don't really want this default ctor, but IterableQueueModel requires it
    TPWrapper() {}
    
    TPWrapper(triggeralgs::TriggerPrimitive p)
      : primitive(p)
    {}
    
    // comparable based on first timestamp
    bool operator<(const TPWrapper& other) const
    {
      return this->primitive.time_start < other.primitive.time_start;
    }

    uint64_t get_first_timestamp() const // NOLINT(build/unsigned)
    {
      return primitive.time_start;
    }

    void set_first_timestamp(uint64_t ts) // NOLINT(build/unsigned)
    {
      primitive.time_start = ts;
    }

    uint64_t get_timestamp() const // NOLINT(build/unsigned)
    {
      return primitive.time_start;
    }


    size_t get_payload_size() { return sizeof(triggeralgs::TriggerPrimitive); }

    size_t get_num_frames() { return 1; }

    size_t get_frame_size() { return get_payload_size(); }

    triggeralgs::TriggerPrimitive* begin()
    {
      return &primitive;
    }
    
    triggeralgs::TriggerPrimitive* end()
    {
      return &primitive + 1;
    }

    //static const constexpr size_t fixed_payload_size = 5568;
    static const constexpr daqdataformats::SourceID::Subsystem subsystem = daqdataformats::SourceID::Subsystem::kTrigger;
    static const constexpr daqdataformats::FragmentType fragment_type = daqdataformats::FragmentType::kTriggerPrimitive;
    // No idea what this should really be set to
    static const constexpr uint64_t expected_tick_difference = 16; // NOLINT(build/unsigned)
  };

  /**
   * @struct LatencyBuffer
   * @brief Holds latency measurements.
   */
  struct LatencyBuffer
  {
    /// @brief start_time of the TP as it enters triggerapp
    std::vector<uint64_t> tpset_start_time;
    /// @brief adc_integral of the TP as it enters triggerapp
    std::vector<uint32_t> tpset_adc_integral;
    /// @brief walltime of the TP as it enters triggerapp
    std::vector<uint64_t> tpset_in_timestamp;
    /// @brief walltime of the TP as it enters the buffer
    std::vector<uint64_t> tpset_buff_timestamp;
    /// @brief number of TP latencies currently stored
    size_t tpset_count;
    /// @brief maxumum number of TP latencies to be stored
    size_t tpset_count_max;


    /// @brief window_time_start of the DataReuqest for TP
    std::vector<uint64_t> tpdr_window_begin;
    /// @brief end_time of the DataReuqest for TP
    std::vector<uint64_t> tpdr_window_end;
    /// @brief walltime for when DataReuqest was received
    std::vector<uint64_t> tpdr_requested_timestamp;
    /// @brief walltime for when DataReuqest was handled by TPBuffer
    std::vector<uint64_t> tpdr_handled_timestamp;
    /// @brief number of DataReuqest for TP latencies stored
    size_t tpdr_count;
    /// @brief max number of TP DataRequest to be stored
    size_t tpdr_count_max;

    /**
     * @brief Initialize the LatencyBuffer based on max sizes
     *
     * @parameter _tpset_count_max max number of input TP latencies
     * @parameter _tpdr_count_max max number of output DataRequest latencies
     */
    void init(size_t _tpset_count_max = 1e6, size_t _tpdr_count_max = 1e3)
    {
      // Init the TPSets
      tpset_count_max     = _tpset_count_max;
      tpset_count         = 0;
      tpset_start_time    = std::vector<uint64_t>(tpset_count_max, 0);
      tpset_adc_integral  = std::vector<uint32_t>(tpset_count_max, 0);
      tpset_in_timestamp  = std::vector<uint64_t>(tpset_count_max, 0);
      tpset_buff_timestamp= std::vector<uint64_t>(tpset_count_max, 0);

      // Init the DataRequests for the TPs
      tpdr_count_max          = _tpdr_count_max;
      tpdr_count              = 0;
      tpdr_window_begin       = std::vector<uint64_t>(tpdr_count_max, 0);
      tpdr_window_end         = std::vector<uint64_t>(tpdr_count_max, 0);
      tpdr_requested_timestamp = std::vector<uint64_t>(tpdr_count_max, 0);
      tpdr_handled_timestamp  = std::vector<uint64_t>(tpdr_count_max, 0);
    }

    /**
     * @brief fills the timestamps of a TP that entered the trigger app
     *
     * @parameter _start_time the start_time of a TriggerPrimitive
     * @parameter _adc_integral the adc_integral of a TriggerPrimitive
     * @parameter _in_timestamp the chrono walltime saved when TP enters triggerapp
     * @parameter _buff_timestamp the chrono walltime saved when TP enters TPBuffer
     */
    void FillTPIn(uint64_t _start_time, uint32_t _adc_integral,
                  uint64_t _in_timestamp, uint64_t _buff_timestamp)
    {
      if(tpset_count < tpset_count_max){
        // Fill the TP latency (using data held by the TPSet itself).
        tpset_start_time[tpset_count]     = _start_time;
        tpset_adc_integral[tpset_count]   = _adc_integral;
        tpset_in_timestamp[tpset_count]   = _in_timestamp;
        tpset_buff_timestamp[tpset_count] = _buff_timestamp;
        tpset_count++;
      }
    }

    /**
     * @brief fills the timestamps of a DataRequest for a TP
     *
     * @parameter _start_time the start_time of the DataRequest
     * @parameter _end_time the end_time of the DataRequest
     * @parameter _requested_timestamp walltime for received DataRequest 
     * @parameter _handled_timestamp walltime for handled DataRequest 
     */
    void FillTPDataRequest(uint64_t _start_time, uint64_t _end_time,
                           uint64_t _requested_timestamp, uint64_t _handled_timestamp)
    {
      if(tpdr_count < tpdr_count_max){
        // Get the current timestamp
        // Fill the data request latencies
        tpdr_window_begin[tpdr_count]       = _start_time;
        tpdr_window_end[tpdr_count]         = _end_time;
        tpdr_requested_timestamp[tpdr_count]= _requested_timestamp;
        tpdr_handled_timestamp[tpdr_count]  = _handled_timestamp;
        tpdr_count++;
      }
    }

    /// @brief Prints all the stored latencies
    void PrintAll()
    {
      // Print the input TPs first
      for(size_t i = 0; i < tpset_count; ++i){
        TLOG() << "TPs Received. time_start: "    << tpset_start_time[i]
                            << " ADC integral: "  << tpset_adc_integral[i]
                            << " real_time_in: "  << tpset_in_timestamp[i]
                            << " real_time_buff: "<< tpset_buff_timestamp[i];
      }

      // Now print the DataRequests for the TPs
      for(size_t i = 0; i < tpdr_count; ++i){
        TLOG() << "TPs Requested: window_begin: " << tpdr_window_begin[i]
                               << " window_end: "   << tpdr_window_end[i]
                               << " real_time_req: "<< tpdr_requested_timestamp[i]
                               << " real_time_han: "<< tpdr_handled_timestamp[i];
      }
    }
  };

  /// @brief Object holding latency measurements
  LatencyBuffer m_latencies;

  void do_conf(const nlohmann::json& config);
  void do_start(const nlohmann::json& obj);
  void do_stop(const nlohmann::json& obj);
  void do_scrap(const nlohmann::json& obj);
  void do_work(std::atomic<bool>&);

  dunedaq::utilities::WorkerThread m_thread;

  using tps_source_t = iomanager::ReceiverConcept<trigger::TPSet>;
  std::shared_ptr<tps_source_t> m_input_queue_tps{nullptr};

  using dr_source_t = iomanager::ReceiverConcept<dfmessages::DataRequest>;
  std::shared_ptr<dr_source_t> m_input_queue_dr{nullptr};

  std::chrono::milliseconds m_queue_timeout;

  using buffer_object_t = TPWrapper;
  using latency_buffer_t = readoutlibs::SkipListLatencyBufferModel<buffer_object_t>;
  std::unique_ptr<latency_buffer_t> m_latency_buffer_impl{nullptr};
  using request_handler_t = readoutlibs::DefaultSkipListRequestHandler<buffer_object_t>;
  std::unique_ptr<request_handler_t> m_request_handler_impl{nullptr};

  // Don't actually use this, but it's currently needed as arg to request handler ctor
  std::unique_ptr<readoutlibs::FrameErrorRegistry> m_error_registry;
};
} // namespace trigger
} // namespace dunedaq

namespace dunedaq {
namespace readoutlibs {

template<>
uint64_t
get_frame_iterator_timestamp(triggeralgs::TriggerPrimitive* prim)
{
  return prim->time_start;
}

}
}

#endif // TRIGGER_PLUGINS_TPBUFFER_HPP_
