/**
 * @file TPSetBufferCreator.cpp TPSetBufferCreator class
 * implementation
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

//#include "CommonIssues.hpp"
#include "TPSetBufferCreator.hpp"
#include "trigger/Issues.hpp"

#include "daqdataformats/FragmentHeader.hpp"
#include "daqdataformats/GeoID.hpp"

#include "appfwk/DAQModuleHelper.hpp"
#include "appfwk/app/Nljs.hpp"

#include "logging/Logging.hpp"

#include <chrono>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace dunedaq {
namespace trigger {

TPSetBufferCreator::TPSetBufferCreator(const std::string& name)
  : dunedaq::appfwk::DAQModule(name)
  , m_thread(std::bind(&TPSetBufferCreator::do_work, this, std::placeholders::_1))
  , m_queueTimeout(100)
  , m_input_queue_tps()
  , m_input_queue_dr()
  , m_output_queue_frag()
  , m_tps_buffer()
{
  register_command("conf", &TPSetBufferCreator::do_configure);
  register_command("start", &TPSetBufferCreator::do_start);
  register_command("stop", &TPSetBufferCreator::do_stop);
  register_command("scrap", &TPSetBufferCreator::do_scrap);
}

void
TPSetBufferCreator::init(const nlohmann::json& init_data)
{
  TLOG() << get_name() << ": Entering init() method";

  m_input_queue_tps.reset(new tps_source_t(appfwk::queue_inst(init_data, "tpset_source")));
  m_input_queue_dr.reset(new dr_source_t(appfwk::queue_inst(init_data, "data_request_source")));
  m_output_queue_frag.reset(new fragment_sink_t(appfwk::queue_inst(init_data, "fragment_sink")));

  TLOG() << get_name() << ": Exiting init() method";
}

void
TPSetBufferCreator::get_info(opmonlib::InfoCollector& /*ci*/, int /*level*/)
{}

void
TPSetBufferCreator::do_configure(const nlohmann::json& obj)
{
  TLOG() << get_name() << ": Entering do_configure() method";

  m_conf = obj.get<tpsetbuffercreator::Conf>();

  m_tps_buffer_size = m_conf.tpset_buffer_size;

  m_tps_buffer.reset(new TPSetBuffer(m_tps_buffer_size));

  m_tps_buffer->set_buffer_size(m_tps_buffer_size);

  TLOG() << get_name() << ": Exiting do_configure() method";
}

void
TPSetBufferCreator::do_start(const nlohmann::json& /*args*/)
{
  TLOG() << get_name() << ": Entering do_start() method";
  m_thread.start_working_thread("buffer-man");
  TLOG() << get_name() << " successfully started";
  TLOG() << get_name() << ": Exiting do_start() method";
}

void
TPSetBufferCreator::do_stop(const nlohmann::json& /*args*/)
{
  TLOG() << get_name() << ": Entering do_stop() method";
  m_thread.stop_working_thread();
  TLOG() << get_name() << " successfully stopped";

  size_t sentCount = 0;
  TPSetBuffer::DataRequestOutput requested_tpset;
  if (m_dr_on_hold.size()) { // check if there are still data request on hold
    TLOG() << get_name() << ": On hold DRs: " << m_dr_on_hold.size();
    std::map<dfmessages::DataRequest, std::vector<trigger::TPSet>>::iterator it = m_dr_on_hold.begin();
    while (it != m_dr_on_hold.end()) {

      requested_tpset.txsets_in_window = it->second;
      std::unique_ptr<daqdataformats::Fragment> frag_out = convert_to_fragment(requested_tpset, it->first);
      TLOG() << get_name() << ": Sending late requested data (" << (it->first).request_information.window_begin << ", "
             << (it->first).request_information.window_end << "), containing "
             << requested_tpset.txsets_in_window.size() << " TPSets.";

      if (requested_tpset.txsets_in_window.size()) {
        frag_out->set_error_bit(daqdataformats::FragmentErrorBits::kIncomplete, true);
      } else {
        frag_out->set_error_bit(daqdataformats::FragmentErrorBits::kDataNotFound, true);
      }

      send_out_fragment(std::move(frag_out), it->first.data_destination);
      sentCount++;
      it++;
    }
  }

  m_tps_buffer->clear_buffer(); // emptying buffer

  TLOG() << get_name() << ": Exiting do_stop() method : sent " << sentCount << " incomplete fragments";
}

void
TPSetBufferCreator::do_scrap(const nlohmann::json& /*args*/)
{
  TLOG() << get_name() << ": Entering do_scrap() method";
  m_tps_buffer.reset(nullptr); // calls dtor
  TLOG() << get_name() << ": Exiting do_scrap() method";
}

std::unique_ptr<daqdataformats::Fragment>
TPSetBufferCreator::convert_to_fragment(std::vector<TPSet>& tpsets, dfmessages::DataRequest input_data_request)
{

  using detdataformats::trigger::TriggerPrimitive;
  size_t n_tps = 0;
  for (auto const& tpset : tpsets) {
    n_tps += tpset.objects.size();
  }

  size_t payload_n_bytes = sizeof(TriggerPrimitive) * n_tps;

  auto payload = std::make_unique<uint8_t[]>(payload_n_bytes);
  TriggerPrimitive* tp_out = reinterpret_cast<TriggerPrimitive*>(payload.get());

  for (auto const& tpset : tpsets) {
    for (auto const& tp : tpset.objects) {
      if (tp.time_start >= input_data_request.request_information.window_begin &&
          tp.time_start <= input_data_request.request_information.window_end) {
        *tp_out = tp;
        ++tp_out;
      }
    }
  }

  auto ret = std::make_unique<daqdataformats::Fragment>(payload.get(), payload_n_bytes);
  auto& frag = *ret.get();

  daqdataformats::GeoID geoid(daqdataformats::GeoID::SystemType::kDataSelection, m_conf.region, m_conf.element);
  daqdataformats::FragmentHeader frag_h;
  frag_h.trigger_number = input_data_request.trigger_number;
  frag_h.trigger_timestamp = input_data_request.trigger_timestamp;
  frag_h.window_begin = input_data_request.request_information.window_begin;
  frag_h.window_end = input_data_request.request_information.window_end;
  frag_h.run_number = input_data_request.run_number;
  frag_h.element_id = geoid;
  frag_h.fragment_type = (daqdataformats::fragment_type_t)daqdataformats::FragmentType::kTriggerPrimitives;
  frag_h.sequence_number = input_data_request.sequence_number;

  frag.set_header_fields(frag_h);

  return ret;
}

void
TPSetBufferCreator::send_out_fragment(std::unique_ptr<daqdataformats::Fragment> frag_out,
                                      std::string data_destination,
                                      size_t& sentCount,
                                      std::atomic<bool>& running_flag)
{
  std::string thisQueueName = m_output_queue_frag->get_name();
  bool successfullyWasSent = false;
  // do...while so that we always try at least once to send
  // everything we generate, even if running_flag is changed
  // to false between the top of the main loop and here
  do {
    TLOG() << get_name() << ": Pushing the requested TPSet onto queue " << thisQueueName;
    try {
      m_output_queue_frag->push(std::make_pair(std::move(frag_out), data_destination), m_queueTimeout);
      successfullyWasSent = true;
      ++sentCount;
    } catch (const dunedaq::appfwk::QueueTimeoutExpired& excpt) {
      std::ostringstream oss_warn;
      oss_warn << "push to output queue \"" << thisQueueName << "\"";
      ers::warning(dunedaq::appfwk::QueueTimeoutExpired(ERS_HERE, get_name(), oss_warn.str(), m_queueTimeout.count()));
    }
  } while (!successfullyWasSent && running_flag.load());
}

void
TPSetBufferCreator::send_out_fragment(std::unique_ptr<daqdataformats::Fragment> frag_out, std::string data_destination)
{
  std::string thisQueueName = m_output_queue_frag->get_name();
  bool successfullyWasSent = false;
  do {
    TLOG() << get_name() << ": Pushing the requested TPSet onto queue " << thisQueueName;
    try {
      m_output_queue_frag->push(std::make_pair(std::move(frag_out), data_destination), m_queueTimeout);
      successfullyWasSent = true;
    } catch (const dunedaq::appfwk::QueueTimeoutExpired& excpt) {
      std::ostringstream oss_warn;
      oss_warn << "push to output queue \"" << thisQueueName << "\"";
      ers::warning(dunedaq::appfwk::QueueTimeoutExpired(ERS_HERE, get_name(), oss_warn.str(), m_queueTimeout.count()));
    }
  } while (!successfullyWasSent);
}

void
TPSetBufferCreator::do_work(std::atomic<bool>& running_flag)
{
  TLOG() << get_name() << ": Entering do_work() method";
  size_t addedCount = 0;
  size_t addFailedCount = 0;
  size_t requestedCount = 0;
  size_t sentCount = 0;

  bool first = true;

  while (running_flag.load()) {

    trigger::TPSet input_tpset;
    dfmessages::DataRequest input_data_request;
    TPSetBuffer::DataRequestOutput requested_tpset;

    // Block that receives TPSets and add them in buffer and check for pending data requests
    try {
      m_input_queue_tps->pop(input_tpset, m_queueTimeout);
      if (first) {
        TLOG() << get_name() << ": Got first TPSet, with start_time=" << input_tpset.start_time
               << " and end_time=" << input_tpset.end_time;
        first = false;
      }

      if (m_tps_buffer->add(input_tpset)) {
        ++addedCount;
        // TLOG() << "TPSet start_time=" << input_tpset.start_time << " and end_time=" << input_tpset.end_time<< " added
        // ("<< addedCount << ")! And buffer size is: " << m_tps_buffer->get_stored_size() << " /
        // "<<m_tps_buffer->get_buffer_size();
      } else {
        ++addFailedCount;
      }

      if (!m_dr_on_hold.empty()) { // check if new data is part of data request on hold

        // TLOG() << "On hold DRs: "<<m_dr_on_hold.size();

        std::map<dfmessages::DataRequest, std::vector<trigger::TPSet>>::iterator it = m_dr_on_hold.begin();

        while (it != m_dr_on_hold.end()) {
          // TLOG() << "Checking TPSet (sart_time= "<<input_tpset.start_time <<", end_time= "<< input_tpset.end_time <<"
          // w.r.t. DR on hold ("<< it->first.window_begin  <<", "<< it->first.window_end  <<"). TPSet count:
          // "<<it->second.size();
          if ((it->first.request_information.window_begin < input_tpset.end_time &&
               it->first.request_information.window_begin > input_tpset.start_time) ||
              (it->first.request_information.window_end > input_tpset.start_time &&
               it->first.request_information.window_end < input_tpset.end_time) ||
              (it->first.request_information.window_end > input_tpset.end_time &&
               it->first.request_information.window_begin <
                 input_tpset.start_time)) { // new tpset is whithin data request windown?
            it->second.push_back(input_tpset);
            // TLOG() << "Adding TPSet (sart_time="<<input_tpset.start_time <<" on DR on hold ("<<
            // it->first.window_begin  <<", "<< it->first.window_end  <<"). TPSet count: "<<it->second.size();
          }
          if (it->first.request_information.window_end <
              input_tpset
                .start_time) { // If more TPSet aren't expected to arrive then push and remove pending data request
            requested_tpset.txsets_in_window = std::move(it->second);
            std::unique_ptr<daqdataformats::Fragment> frag_out =
              convert_to_fragment(requested_tpset.txsets_in_window, it->first);
            TLOG() << get_name() << ": Sending late requested data (" << (it->first).request_information.window_begin
                   << ", " << (it->first).request_information.window_end << "), containing "
                   << requested_tpset.txsets_in_window.size() << " TPSets.";
            if (requested_tpset.txsets_in_window.empty()) {
              frag_out->set_error_bit(daqdataformats::FragmentErrorBits::kDataNotFound, true);
            }

            send_out_fragment(std::move(frag_out), it->first.data_destination, sentCount, running_flag);
            it = m_dr_on_hold.erase(it);
            continue;
          }
          it++;
        }
      } // end if(!m_dr_on_hold.empty())

    } catch (const dunedaq::appfwk::QueueTimeoutExpired& excpt) {
    }

    // Block that receives data requests and return fragments from buffer
    try {
      m_input_queue_dr->pop(input_data_request, std::chrono::milliseconds(0));
      requested_tpset = m_tps_buffer->get_txsets_in_window(input_data_request.request_information.window_begin,
                                                           input_data_request.request_information.window_end);
      ++requestedCount;

      TLOG_DEBUG(1) << get_name() << ": Got request number " << input_data_request.request_number << ", trigger number "
                    << input_data_request.trigger_number << " begin/end ("
                    << input_data_request.request_information.window_begin << ", "
                    << input_data_request.request_information.window_end << ")";

      auto frag_out = convert_to_fragment(requested_tpset, input_data_request);

      switch (requested_tpset.ds_outcome) {
        case TPSetBuffer::kEmpty:
          TLOG() << get_name() << ": Requested data (" << input_data_request.request_information.window_begin << ", "
                 << input_data_request.request_information.window_end << ") not in buffer, which contains "
                 << m_tps_buffer->get_stored_size() << " TPSets between (" << m_tps_buffer->get_earliest_start_time()
                 << ", " << m_tps_buffer->get_latest_end_time() << "). Returning empty fragment.";
          frag_out->set_error_bit(daqdataformats::FragmentErrorBits::kDataNotFound, true);
          send_out_fragment(std::move(frag_out), input_data_request.data_destination, sentCount, running_flag);
          break;
        case TPSetBuffer::kLate:
          TLOG() << get_name() << ": Requested data (" << input_data_request.request_information.window_begin << ", "
                 << input_data_request.request_information.window_end << ") has not arrived in buffer, which contains "
                 << m_tps_buffer->get_stored_size() << " TPSets between (" << m_tps_buffer->get_earliest_start_time()
                 << ", " << m_tps_buffer->get_latest_end_time() << "). Holding request until more data arrives.";
          m_dr_on_hold.insert(std::make_pair(input_data_request, requested_tpset.txsets_in_window));
          break; // don't send anything yet. Wait for more data to arrived.
        case TPSetBuffer::kSuccess:
          TLOG() << get_name() << ": Sending requested data (" << input_data_request.request_information.window_begin
                 << ", " << input_data_request.request_information.window_end << "), containing "
                 << requested_tpset.txsets_in_window.size() << " TPSets.";

          send_out_fragment(std::move(frag_out), input_data_request.data_destination, sentCount, running_flag);
          break;
        default:
          TLOG() << get_name() << ": Data request failed!";
      }

    } catch (const dunedaq::appfwk::QueueTimeoutExpired& excpt) {
      // skip if no data request in the queue
      continue;
    }
  } // end while(running_flag.load())

  TLOG() << get_name() << ": Exiting the do_work() method: received " << addedCount << " Sets and " << requestedCount
         << " data requests. " << addFailedCount << " Sets failed to add. Sent " << sentCount << " fragments";

  TLOG() << get_name() << ": Exiting do_work() method";
} // NOLINT Function length

} // namespace trigger
} // namespace dunedaq

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::TPSetBufferCreator)

// Local Variables:
// c-basic-offset: 2
// End:
