#ifndef TRIGGER_SRC_TRIGGER_TRIGGERGENERICWORKER_HXX_
#define TRIGGER_SRC_TRIGGER_TRIGGERGENERICMAKER_HXX_

namespace dunedaq {
namespace trigger {


//-----------------------------------------------------------------------------
//
// template<class IN, class OUT, class MAKER>
// class TriggerGenericWorker<IN, OUT, MAKER>
//
//-----------------------------------------------------------------------------

//---
template<class IN, class OUT, class MAKER>
TriggerGenericWorker<IN, OUT, MAKER>::TriggerGenericWorker(TriggerGenericMaker<IN, OUT, MAKER>& parent)
  : m_parent(parent)
{
}

//---
template<class IN, class OUT, class MAKER>
void 
TriggerGenericWorker<IN, OUT, MAKER>::process(IN& in)
{
std::vector<OUT> out_vec; // one input -> many outputs
try {
    m_parent.m_maker->operator()(in, out_vec);
} catch (...) { // NOLINT TODO Benjamin Land <BenLand100@github.com> May 28-2021 can we restrict the possible
                // exceptions triggeralgs might raise?
    ers::fatal(AlgorithmFatalError(ERS_HERE, m_parent.get_name(), m_parent.m_algorithm_name));
    return;
}

while (out_vec.size()) {
    if (!m_parent.send(std::move(out_vec.back()))) {
    ers::error(AlgorithmFailedToSend(ERS_HERE, m_parent.get_name(), m_parent.m_algorithm_name));
    // out_vec.back() is dropped
    }
    out_vec.pop_back();
}
}

//-----------------------------------------------------------------------------
// 
// template<class A, class B, class MAKER>
// class TriggerGenericWorker<Set<A>, Set<B>, MAKER>
//
//-----------------------------------------------------------------------------

//---
template<class A, class B, class MAKER>
TriggerGenericWorker<Set<A>, Set<B>, MAKER>::TriggerGenericWorker(TriggerGenericMaker<Set<A>, Set<B>, MAKER>& parent)
  : m_parent(parent)
  , m_in_buffer(parent.get_name(), parent.m_algorithm_name)
  , m_out_buffer(parent.get_name(), parent.m_algorithm_name, parent.m_buffer_time)
{
}

//---
template<class A, class B, class MAKER>
void
TriggerGenericWorker<Set<A>, Set<B>, MAKER>::reconfigure()
{
  m_out_buffer.set_window_time(m_parent.m_window_time);
  m_out_buffer.set_buffer_time(m_parent.m_buffer_time);
}

//---
template<class A, class B, class MAKER>
void
TriggerGenericWorker<Set<A>, Set<B>, MAKER>::reset()
{
  m_prev_start_time = 0;
  m_out_buffer.reset();
}

//---
template<class A, class B, class MAKER>
void
TriggerGenericWorker<Set<A>, Set<B>, MAKER>::

  process_slice(const std::vector<A>& time_slice, std::vector<B>& out_vec)
{
  // time_slice is a full slice (all Set<A> combined), time ordered, vector of A
  // call operator for each of the objects in the vector
  for (const A& x : time_slice) {
    try {
      m_parent.m_maker->operator()(x, out_vec);
    } catch (...) { // NOLINT TODO Benjamin Land <BenLand100@github.com> May 28-2021 can we restrict the possible
                    // exceptions triggeralgs might raise?
      ers::fatal(AlgorithmFatalError(ERS_HERE, m_parent.get_name(), m_parent.m_algorithm_name));
      return;
    }
  }
}

//---
template<class A, class B, class MAKER>
void
TriggerGenericWorker<Set<A>, Set<B>, MAKER>::process(Set<A>& in)
{
  std::vector<B> elems; // Bs to buffer for the next window
  switch (in.type) {
    case Set<A>::Type::kPayload: {
      if (m_prev_start_time != 0 && in.start_time < m_prev_start_time) {
        ers::warning(OutOfOrderSets(ERS_HERE, m_parent.get_name(), m_prev_start_time, in.start_time));
      }
      m_prev_start_time = in.start_time;
      std::vector<A> time_slice;
      daqdataformats::timestamp_t start_time, end_time;
      if (!m_in_buffer.buffer(in, time_slice, start_time, end_time)) {
        return; // no complete time slice yet (`in` was part of buffered slice)
      }
      process_slice(time_slice, elems);
    } break;
    case Set<A>::Type::kHeartbeat: {
      // PAR 2022-04-27 We've got a heartbeat for time T, so we know
      // we won't receive any more inputs for times t < T. Therefore
      // we can flush all items in the input buffer, which have
      // times t < T, because the input is time-ordered. We put the
      // heartbeat in the output buffer, which will handle it
      // appropriately

      std::vector<A> time_slice;
      daqdataformats::timestamp_t start_time, end_time;
      if (m_in_buffer.flush(time_slice, start_time, end_time)) {
        if (end_time > in.start_time) {
          // This should never happen, but we check here so we at least get some output if it did
          ers::fatal(OutOfOrderSets(ERS_HERE, m_parent.get_name(), end_time, in.start_time));
        }
        process_slice(time_slice, elems);
      }

      Set<B> heartbeat;
      heartbeat.type = Set<B>::Type::kHeartbeat;
      heartbeat.start_time = in.start_time;
      heartbeat.end_time = in.end_time;
      heartbeat.origin = daqdataformats::SourceID(daqdataformats::SourceID::Subsystem::kTrigger, m_parent.m_sourceid);

      TLOG_DEBUG(4) << "Buffering heartbeat with start time " << heartbeat.start_time;
      m_out_buffer.buffer_heartbeat(heartbeat);

      // flush the maker
      try {
        // TODO Benjamin Land <BenLand100@github.com> July-14-2021 flushed events go into the buffer... until a window
        // is ready?
        m_parent.m_maker->flush(in.end_time, elems);
      } catch (...) { // NOLINT TODO Benjamin Land <BenLand100@github.com> May-28-2021 can we restrict the possible
                      // exceptions triggeralgs might raise?
        ers::fatal(AlgorithmFatalError(ERS_HERE, m_parent.get_name(), m_parent.m_algorithm_name));
        return;
      }
    } break;
    case Set<A>::Type::kUnknown:
      ers::error(UnknownSetError(ERS_HERE, m_parent.get_name(), m_parent.m_algorithm_name));
      break;
  }

  // add new elements to output buffer
  if (elems.size() > 0) {
    m_out_buffer.buffer(elems);
  }

  size_t n_output_windows = 0;
  // emit completed windows
  while (m_out_buffer.ready()) {
    ++n_output_windows;
    Set<B> out;
    m_out_buffer.flush(out);
    out.seqno = m_parent.m_sent_count;
    out.origin = daqdataformats::SourceID(daqdataformats::SourceID::Subsystem::kTrigger, m_parent.m_sourceid);

    if (out.type == Set<B>::Type::kHeartbeat) {
      TLOG_DEBUG(4) << "Sending heartbeat with start time " << out.start_time;
      if (!m_parent.send(std::move(out))) {
        ers::error(AlgorithmFailedToSend(ERS_HERE, m_parent.get_name(), m_parent.m_algorithm_name));
        // out is dropped
      }
    }
    // Only form and send Set<B> if it has a nonzero number of objects
    else if (out.type == Set<B>::Type::kPayload && out.objects.size() != 0) {
      TLOG_DEBUG(4) << "Output set window ready with start time " << out.start_time << " end time " << out.end_time << " and " << out.objects.size() << " members";
      if (!m_parent.send(std::move(out))) {
        ers::error(AlgorithmFailedToSend(ERS_HERE, m_parent.get_name(), m_parent.m_algorithm_name));
        // out is dropped
      }
    }
  }
  TLOG_DEBUG(4) << "process() done. Advanced output buffer by " << n_output_windows << " output windows";
}

//---
template<class A, class B, class MAKER>
void
TriggerGenericWorker<Set<A>, Set<B>, MAKER>::drain(bool drop)
{
  // First, send anything in the input buffer to the algorithm, and add any
  // results to output buffer
  std::vector<A> time_slice;
  daqdataformats::timestamp_t start_time, end_time;
  if (m_in_buffer.flush(time_slice, start_time, end_time)) {
    std::vector<B> elems;
    process_slice(time_slice, elems);
    if (elems.size() > 0) {
      m_out_buffer.buffer(elems);
    }
  }
  // Second, drain the output buffer onto the queue. These may not be "fully
  // formed" windows, but at this point we're getting no more data anyway.
  while (!m_out_buffer.empty()) {
    Set<B> out;
    m_out_buffer.flush(out);
    out.seqno = m_parent.m_sent_count;
    out.origin = daqdataformats::SourceID(daqdataformats::SourceID::Subsystem::kTrigger, m_parent.m_sourceid);

    if (out.type == Set<B>::Type::kHeartbeat) {
      if (!drop) {
        if (!m_parent.send(std::move(out))) {
          ers::error(AlgorithmFailedToSend(ERS_HERE, m_parent.get_name(), m_parent.m_algorithm_name));
          // out is dropped
        }
      }
    }
    // Only form and send Set<B> if it has a nonzero number of objects
    else if (out.type == Set<B>::Type::kPayload && out.objects.size() != 0) {
      TLOG_DEBUG(1) << "Output set window ready with start time " << out.start_time << " end time " << out.end_time << " and " << out.objects.size() << " members";
      if (!drop) {
        if (!m_parent.send(std::move(out))) {
          ers::error(AlgorithmFailedToSend(ERS_HERE, m_parent.get_name(), m_parent.m_algorithm_name));
          // out is dropped
        }
      }
    }
  }
}

//-----------------------------------------------------------------------------
//
// template<class A, class OUT, class MAKER>
// class TriggerGenericWorker<Set<A>, OUT, MAKER>
//
//-----------------------------------------------------------------------------


//---
template<class A, class OUT, class MAKER>
TriggerGenericWorker<Set<A>, OUT, MAKER>::TriggerGenericWorker(TriggerGenericMaker<Set<A>, OUT, MAKER>& parent)
  : m_parent(parent)
  , m_in_buffer(parent.get_name(), parent.m_algorithm_name)
{
}

//---
template<class A, class OUT, class MAKER>
void
TriggerGenericWorker<Set<A>, OUT, MAKER>::process_slice(const std::vector<A>& time_slice, std::vector<OUT>& out_vec)
{
  // time_slice is a full slice (all Set<A> combined), time ordered, vector of A
  // call operator for each of the objects in the vector
  for (const A& x : time_slice) {
    try {
      m_parent.m_maker->operator()(x, out_vec);
    } catch (...) { // NOLINT TODO Benjamin Land <BenLand100@github.com> May 28-2021 can we restrict the possible
                    // exceptions triggeralgs might raise?
      ers::fatal(AlgorithmFatalError(ERS_HERE, m_parent.get_name(), m_parent.m_algorithm_name));
      return;
    }
  }
}

//---
template<class A, class OUT, class MAKER>
void
TriggerGenericWorker<Set<A>, OUT, MAKER>::process(Set<A>& in)
{
  std::vector<OUT> out_vec; // either a whole time slice, heartbeat flushed, or empty
  switch (in.type) {
    case Set<A>::Type::kPayload: {
      std::vector<A> time_slice;
      daqdataformats::timestamp_t start_time, end_time;
      if (!m_in_buffer.buffer(in, time_slice, start_time, end_time)) {
        return; // no complete time slice yet (`in` was part of buffered slice)
      }
      process_slice(time_slice, out_vec);
    } break;
    case Set<A>::Type::kHeartbeat:
      // TODO BJL May-28-2021 should anything happen with the heartbeat when OUT is not a Set<T>?
      //
      // PAR 2022-01-21 We've got a heartbeat for time T, so we know
      // we won't receive any more inputs for times t < T. Therefore
      // we can flush all items in the input buffer, which have
      // times t < T, because the input is time-ordered
      try {
        std::vector<A> time_slice;
        daqdataformats::timestamp_t start_time, end_time;
        if (m_in_buffer.flush(time_slice, start_time, end_time)) {
          if (end_time > in.start_time) {
            // This should never happen, but we check here so we at least get some output if it did
            ers::fatal(OutOfOrderSets(ERS_HERE, m_parent.get_name(), end_time, in.start_time));
          }
          process_slice(time_slice, out_vec);
        }
        m_parent.m_maker->flush(in.end_time, out_vec);
      } catch (...) { // NOLINT TODO Benjamin Land <BenLand100@github.com> May 28-2021 can we restrict the possible
                      // exceptions triggeralgs might raise?
        ers::fatal(AlgorithmFatalError(ERS_HERE, m_parent.get_name(), m_parent.m_algorithm_name));
        return;
      }
      break;
    case Set<A>::Type::kUnknown:
      ers::error(UnknownSetError(ERS_HERE, m_parent.get_name(), m_parent.m_algorithm_name));
      break;
  }

  while (out_vec.size()) {
    if (!m_parent.send(std::move(out_vec.back()))) {
      ers::error(AlgorithmFailedToSend(ERS_HERE, m_parent.get_name(), m_parent.m_algorithm_name));
      // out.back() is dropped
    }
    out_vec.pop_back();
  }
}

//---
template<class A, class OUT, class MAKER>
void
TriggerGenericWorker<Set<A>, OUT, MAKER>::drain(bool drop)
{
  // Send anything in the input buffer to the algorithm, and put any results
  // on the output queue
  std::vector<A> time_slice;
  daqdataformats::timestamp_t start_time, end_time;
  if (m_in_buffer.flush(time_slice, start_time, end_time)) {
    std::vector<OUT> out_vec;
    process_slice(time_slice, out_vec);
    while (out_vec.size()) {
      if (!drop) {
        if (!m_parent.send(std::move(out_vec.back()))) {
          ers::error(AlgorithmFailedToSend(ERS_HERE, m_parent.get_name(), m_parent.m_algorithm_name));
          // out.back() is dropped
        }
      }
      out_vec.pop_back();
    }
  }
}

} // namespace trigger
} // namespace dunedaq

#endif /* TRIGGER_SRC_TRIGGER_TRIGGERGENERICWORKER_HXX_ */