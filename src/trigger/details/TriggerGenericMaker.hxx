
#ifndef TRIGGER_SRC_TRIGGER_TRIGGERGENERICMAKER_HXX_
#define TRIGGER_SRC_TRIGGER_TRIGGERGENERICMAKER_HXX_

namespace dunedaq {
namespace trigger {

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

template<class IN, class OUT, class MAKER>
void
TriggerGenericMaker<IN, OUT, MAKER>::init(const nlohmann::json& obj)
{
  m_input_queue = get_iom_receiver<IN>(appfwk::connection_uid(obj, "input"));
  m_output_queue = get_iom_sender<OUT>(appfwk::connection_uid(obj, "output"));
}

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

} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_SRC_TRIGGER_TRIGGERGENERICMAKER_HXX_
