
#ifndef TRIGGER_SRC_TRIGGER_TRIGGERGENERICMAKER_HXX_
#define TRIGGER_SRC_TRIGGER_TRIGGERGENERICMAKER_HXX_

namespace dunedaq {
namespace trigger {

//---
template<class IN, class OUT, class MAKER>
TriggerGenericMaker<IN, OUT, MAKER>::TriggerGenericMaker(const std::string& name)
  : DAQModule(name)
  , m_thread(std::bind(&TriggerGenericMaker::do_work, this, std::placeholders::_1))
  , m_received_count(0)
  , m_sent_count(0)
  , m_input_queue(nullptr)
  , m_output_queue(nullptr)
  , m_queue_timeout(100)
  , m_algorithm_name("[uninitialized]")
  , m_sourceid(dunedaq::daqdataformats::SourceID::s_invalid_id)
  , m_buffer_time(0)
  , m_window_time(625000)
  , worker(*this) // should be last; may use other members
{
  register_command("start", &TriggerGenericMaker::do_start);
  register_command("stop", &TriggerGenericMaker::do_stop);
  register_command("conf", &TriggerGenericMaker::do_configure);
}


//---
template<class IN, class OUT, class MAKER>
void
TriggerGenericMaker<IN, OUT, MAKER>::init(const nlohmann::json& obj)
{
  m_input_queue = get_iom_receiver<IN>(appfwk::connection_uid(obj, "input"));
  m_output_queue = get_iom_sender<OUT>(appfwk::connection_uid(obj, "output"));
}


//---
template<class IN, class OUT, class MAKER>
void
TriggerGenericMaker<IN, OUT, MAKER>::get_info(opmonlib::InfoCollector& ci, int /*level*/)
{
  triggergenericmakerinfo::Info i;

  i.received_count = m_received_count.load();
  i.sent_count = m_sent_count.load();
  if (m_maker) {
    i.data_vs_system_ms = m_maker->m_data_vs_system_time;
  } else
    i.data_vs_system_ms = 0;

  ci.add(i);
}


//---
template<class IN, class OUT, class MAKER>
void
TriggerGenericMaker<IN, OUT, MAKER>::do_start(const nlohmann::json& /*obj*/)
{
  m_received_count = 0;
  m_sent_count = 0;
  m_maker = make_maker(m_maker_conf);
  worker.reconfigure();
  m_thread.start_working_thread(get_name());
}


//---
template<class IN, class OUT, class MAKER>
void
TriggerGenericMaker<IN, OUT, MAKER>::do_configure(const nlohmann::json& obj)
{
  // P. Rodrigues 2022-07-13
  // We stash the config here and don't actually create the maker
  // algorithm until start time, so that the algorithm doesn't
  // persist between runs and hold onto its state from the previous
  // run
  m_maker_conf = obj;

  // worker should be notified that configuration potentially changed
  worker.reconfigure();
}


//---
template<class IN, class OUT, class MAKER>
void
TriggerGenericMaker<IN, OUT, MAKER>::do_work(std::atomic<bool>& m_running_flag)
{
  // Loop until a stop is received
  while (m_running_flag.load()) {
    // While there are items in the input queue, continue draining even if
    // the running_flag is false, but stop _immediately_ when input is empty
    IN in;
    while (receive(in)) {
      if (m_running_flag.load()) {
        worker.process(in);
      }
    }
  }
  // P. Rodrigues 2022-06-01. The argument here is whether to drop
  // buffered outputs. We choose 'true' because some significant
  // time can pass between the last input sent by readout and when
  // we receive a stop. (This happens because stop is sent serially
  // to readout units before trigger, and each RU takes ~1s to
  // stop). So by the time we receive a stop command, our buffered
  // outputs are stale and will cause tardy warnings from the zipper
  // downstream
  worker.drain(true);
  TLOG() << get_name() << ": Exiting do_work() method, received " << m_received_count << " inputs and successfully sent " << m_sent_count << " outputs. ";
  worker.reset();
}


//---
template<class IN, class OUT, class MAKER>
bool
TriggerGenericMaker<IN, OUT, MAKER>::receive(IN& in)
{
  try {
    in = m_input_queue->receive(m_queue_timeout);
  } catch (const dunedaq::iomanager::TimeoutExpired& excpt) {
    // it is perfectly reasonable that there might be no data in the queue
    // some fraction of the times that we check, so we just continue on and try again
    return false;
  }
  ++m_received_count;
  return true;
}


//---
template<class IN, class OUT, class MAKER>
bool
TriggerGenericMaker<IN, OUT, MAKER>::send(OUT&& out)
{
  try {
    m_output_queue->send(std::move(out), m_queue_timeout);
  } catch (const dunedaq::iomanager::TimeoutExpired& excpt) {
    ers::warning(excpt);
    return false;
  }
  ++m_sent_count;
  return true;
}

} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_SRC_TRIGGER_TRIGGERGENERICMAKER_HXX_
