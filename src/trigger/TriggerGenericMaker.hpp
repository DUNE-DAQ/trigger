/**
 * @file TriggerGenericMaker.hpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_SRC_TRIGGER_TRIGGERGENERICMAKER_HPP_
#define TRIGGER_SRC_TRIGGER_TRIGGERGENERICMAKER_HPP_

#include "trigger/Issues.hpp"
#include "trigger/Set.hpp"
#include "trigger/TimeSliceInputBuffer.hpp"
#include "trigger/TimeSliceOutputBuffer.hpp"
#include "trigger/triggergenericmakerinfo/InfoNljs.hpp"

#include "appfwk/DAQModule.hpp"
#include "appfwk/DAQModuleHelper.hpp"
#include "daqdataformats/SourceID.hpp"
#include "iomanager/IOManager.hpp"
#include "iomanager/Receiver.hpp"
#include "iomanager/Sender.hpp"
#include "logging/Logging.hpp"
#include "trgdataformats/Types.hpp"
#include "utilities/WorkerThread.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace dunedaq::trigger {

// Forward declare the class encapsulating partial specifications of do_work
template<class IN, class OUT, class MAKER>
class TriggerGenericWorker;

// This template class reads IN items from queues, passes them to MAKER objects,
// and writes the resulting OUT objects to another queue. The behavior of
// passing IN objects to the MAKER and creating OUT objects from the MAKER is
// encapsulated by TriggerGenericWorker<IN,OUT,MAKER> templates, defined later
// in this file
template<class IN, class OUT, class MAKER>
class TriggerGenericMaker : public dunedaq::appfwk::DAQModule
{
  friend class TriggerGenericWorker<IN, OUT, MAKER>;

public:
  explicit TriggerGenericMaker(const std::string& name);


  virtual ~TriggerGenericMaker() {}

  TriggerGenericMaker(const TriggerGenericMaker&) = delete;
  TriggerGenericMaker& operator=(const TriggerGenericMaker&) = delete;
  TriggerGenericMaker(TriggerGenericMaker&&) = delete;
  TriggerGenericMaker& operator=(TriggerGenericMaker&&) = delete;

  void init(const nlohmann::json& obj) override;
 

  void get_info(opmonlib::InfoCollector& ci, int /*level*/) override;


protected:
  void set_algorithm_name(const std::string& name) { m_algorithm_name = name; }

  // Only applies to makers that output Set<B>
  void set_sourceid(uint32_t element_id) // NOLINT(build/unsigned)
  {
    m_sourceid = element_id;
  }

  // Only applies to makers that output Set<B>
  void set_windowing(daqdataformats::timestamp_t window_time, daqdataformats::timestamp_t buffer_time)
  {
    m_window_time = window_time;
    m_buffer_time = buffer_time;
  }

private:
  dunedaq::utilities::WorkerThread m_thread;

  using metric_counter_type = decltype(triggergenericmakerinfo::Info::received_count);
  std::atomic<metric_counter_type> m_received_count;
  std::atomic<metric_counter_type> m_sent_count;

  using source_t = dunedaq::iomanager::ReceiverConcept<IN>;
  std::shared_ptr<source_t> m_input_queue;

  using sink_t = dunedaq::iomanager::SenderConcept<OUT>;
  std::shared_ptr<sink_t> m_output_queue;

  std::chrono::milliseconds m_queue_timeout;

  std::string m_algorithm_name;

  uint32_t m_sourceid; // NOLINT(build/unsigned)

  daqdataformats::timestamp_t m_buffer_time;
  daqdataformats::timestamp_t m_window_time;

  std::shared_ptr<MAKER> m_maker;
  nlohmann::json m_maker_conf;

  TriggerGenericWorker<IN, OUT, MAKER> worker;

  // This should return a shared_ptr to the MAKER created from conf command arguments.
  // Should also call set_algorithm_name and set_geoid/set_windowing (if desired)
  virtual std::shared_ptr<MAKER> make_maker(const nlohmann::json& obj) = 0;

  void do_start(const nlohmann::json& /*obj*/);


  void do_stop(const nlohmann::json& /*obj*/) { m_thread.stop_working_thread(); }

  void do_configure(const nlohmann::json& obj);


  void do_work(std::atomic<bool>& m_running_flag);


  bool receive(IN& in);


  bool send(OUT&& out);

};

// To handle the different unpacking schemes implied by different templates,
// do_work is broken out into its own template class that is a friend of
// TriggerGenericMaker. C++ still does not support partial specification of a
// single method in a template class, so this approach is the least redundant
// way to achieve that functionality

// The base template assumes the MAKER has an operator() with the signature
// operator()(IN, std::vector<OUT>)
template<class IN, class OUT, class MAKER>
class TriggerGenericWorker
{
public:
  explicit TriggerGenericWorker(TriggerGenericMaker<IN, OUT, MAKER>& parent);


  TriggerGenericMaker<IN, OUT, MAKER>& m_parent;

  void reconfigure() {}

  void reset() {}

  void process(IN& in);


  void drain(bool) {}
};

// Partial specialization for IN = Set<A>, OUT = Set<B> and assumes the MAKER has:
// operator()(A, std::vector<B>)
template<class A, class B, class MAKER>
class TriggerGenericWorker<Set<A>, Set<B>, MAKER>
{
public: // NOLINT
  explicit TriggerGenericWorker(TriggerGenericMaker<Set<A>, Set<B>, MAKER>& parent);


  TriggerGenericMaker<Set<A>, Set<B>, MAKER>& m_parent;

  TimeSliceInputBuffer<A> m_in_buffer;
  TimeSliceOutputBuffer<B> m_out_buffer;

  daqdataformats::timestamp_t m_prev_start_time = 0;

  void reconfigure();

  void reset();

  void process_slice(const std::vector<A>& time_slice, std::vector<B>& out_vec);


  void process(Set<A>& in);


  void drain(bool drop);

};

// Partial specialization for IN = Set<A> and assumes the the MAKER has:
// operator()(A, std::vector<OUT>)
template<class A, class OUT, class MAKER>
class TriggerGenericWorker<Set<A>, OUT, MAKER>
{
public: // NOLINT
  explicit TriggerGenericWorker(TriggerGenericMaker<Set<A>, OUT, MAKER>& parent);

  TriggerGenericMaker<Set<A>, OUT, MAKER>& m_parent;

  TimeSliceInputBuffer<A> m_in_buffer;

  void reconfigure() {}

  void reset() {}

  void process_slice(const std::vector<A>& time_slice, std::vector<OUT>& out_vec);

  void process(Set<A>& in);
  // void process(Set<A>& in)
  // {
  //   std::vector<OUT> out_vec; // either a whole time slice, heartbeat flushed, or empty
  //   switch (in.type) {
  //     case Set<A>::Type::kPayload: {
  //       std::vector<A> time_slice;
  //       daqdataformats::timestamp_t start_time, end_time;
  //       if (!m_in_buffer.buffer(in, time_slice, start_time, end_time)) {
  //         return; // no complete time slice yet (`in` was part of buffered slice)
  //       }
  //       process_slice(time_slice, out_vec);
  //     } break;
  //     case Set<A>::Type::kHeartbeat:
  //       // TODO BJL May-28-2021 should anything happen with the heartbeat when OUT is not a Set<T>?
  //       //
  //       // PAR 2022-01-21 We've got a heartbeat for time T, so we know
  //       // we won't receive any more inputs for times t < T. Therefore
  //       // we can flush all items in the input buffer, which have
  //       // times t < T, because the input is time-ordered
  //       try {
  //         std::vector<A> time_slice;
  //         daqdataformats::timestamp_t start_time, end_time;
  //         if (m_in_buffer.flush(time_slice, start_time, end_time)) {
  //           if (end_time > in.start_time) {
  //             // This should never happen, but we check here so we at least get some output if it did
  //             ers::fatal(OutOfOrderSets(ERS_HERE, m_parent.get_name(), end_time, in.start_time));
  //           }
  //           process_slice(time_slice, out_vec);
  //         }
  //         m_parent.m_maker->flush(in.end_time, out_vec);
  //       } catch (...) { // NOLINT TODO Benjamin Land <BenLand100@github.com> May 28-2021 can we restrict the possible
  //                       // exceptions triggeralgs might raise?
  //         ers::fatal(AlgorithmFatalError(ERS_HERE, m_parent.get_name(), m_parent.m_algorithm_name));
  //         return;
  //       }
  //       break;
  //     case Set<A>::Type::kUnknown:
  //       ers::error(UnknownSetError(ERS_HERE, m_parent.get_name(), m_parent.m_algorithm_name));
  //       break;
  //   }

  //   while (out_vec.size()) {
  //     if (!m_parent.send(std::move(out_vec.back()))) {
  //       ers::error(AlgorithmFailedToSend(ERS_HERE, m_parent.get_name(), m_parent.m_algorithm_name));
  //       // out.back() is dropped
  //     }
  //     out_vec.pop_back();
  //   }
  // }

  void drain(bool drop);
  // void drain(bool drop)
  // {
  //   // Send anything in the input buffer to the algorithm, and put any results
  //   // on the output queue
  //   std::vector<A> time_slice;
  //   daqdataformats::timestamp_t start_time, end_time;
  //   if (m_in_buffer.flush(time_slice, start_time, end_time)) {
  //     std::vector<OUT> out_vec;
  //     process_slice(time_slice, out_vec);
  //     while (out_vec.size()) {
  //       if (!drop) {
  //         if (!m_parent.send(std::move(out_vec.back()))) {
  //           ers::error(AlgorithmFailedToSend(ERS_HERE, m_parent.get_name(), m_parent.m_algorithm_name));
  //           // out.back() is dropped
  //         }
  //       }
  //       out_vec.pop_back();
  //     }
  //   }
  // }
};

} // namespace dunedaq::trigger

#include "details/TriggerGenericMaker.hxx"
#include "details/TriggerGenericWorker.hxx"

#endif // TRIGGER_SRC_TRIGGER_TRIGGERGENERICMAKER_HPP_
