#include "trigger/TPRequestHandler.hpp"
#include "appdal/ReadoutModuleConf.hpp"
#include "appdal/RequestHandler.hpp"

#include "rcif/cmd/Nljs.hpp"

namespace dunedaq {
namespace trigger {

void
TPRequestHandler::conf(const appdal::ReadoutModule* conf) {

   for (auto output : conf->get_outputs()) {
      if (output->get_data_type() == "TPSet") {
         try {
            m_tpset_sink = iomanager::IOManager::get()->get_sender<dunedaq::trigger::TPSet>(output->UID());
         } catch (const ers::Issue& excpt) {
            throw readoutlibs::ResourceQueueError(ERS_HERE, "tp queue", "DefaultRequestHandlerModel", excpt);
         }
      }
   }
   inherited2::conf(conf);
}

void 
TPRequestHandler::start(const nlohmann::json& args) {

   m_oldest_ts=0;
   m_newest_ts=0;
   m_start_win_ts=0;
   m_end_win_ts=0;
   m_first_cycle = true;
	
   inherited2::start(args);
   rcif::cmd::StartParams start_params = args.get<rcif::cmd::StartParams>();
   m_run_number = start_params.run;

}

void
TPRequestHandler::periodic_data_transmission() {
   dunedaq::dfmessages::DataRequest dr;

   {
      std::unique_lock<std::mutex> lock(m_cv_mutex);
      m_cv.wait(lock, [&] { return !m_cleanup_requested; });
      m_requests_running++;
   }
   m_cv.notify_all();
   if(m_latency_buffer->occupancy() != 0) {
       // Prepare response
       RequestResult rres(ResultCode::kUnknown, dr);
       std::vector<std::pair<void*, size_t>> frag_pieces;

       // Get the newest TP
       SkipListAcc acc(inherited2::m_latency_buffer->get_skip_list());
       auto tail = acc.last();
       auto head = acc.first();
       m_newest_ts = (*tail).get_first_timestamp();
       m_oldest_ts = (*head).get_first_timestamp();
       
       if (m_first_cycle) {
    	  m_start_win_ts = m_oldest_ts;
	  m_first_cycle = false;
       }
       if (m_newest_ts - m_start_win_ts > m_ts_set_sender_offset_ticks) {
         m_end_win_ts = m_newest_ts - m_ts_set_sender_offset_ticks;
         frag_pieces = get_fragment_pieces(m_start_win_ts, m_end_win_ts, rres);
         auto num_tps = frag_pieces.size();
         trigger::TPSet tpset;
         tpset.run_number = m_run_number;
         tpset.type = num_tps>0 ? trigger::TPSet::Type::kPayload : trigger::TPSet::Type::kHeartbeat;
         tpset.origin = m_sourceid;
         tpset.start_time = m_start_win_ts; // provisory timestamp, will be filled with first TP
         tpset.end_time = m_end_win_ts; // provisory timestamp, will be filled with last TP
         tpset.seqno = m_next_tpset_seqno++; // NOLINT(runtime/increment_decrement)
       // reserve the space for efficiency
         if (num_tps > 0) {    
            tpset.objects.reserve(frag_pieces.size());
            bool first_tp = true;
            for( auto f : frag_pieces) {
               trgdataformats::TriggerPrimitive tp = *(static_cast<trgdataformats::TriggerPrimitive*>(f.first));
	       
               if(first_tp) {
                  tpset.start_time = tp.time_start;
                  first_tp = false;
               }
	       tpset.end_time = tp.time_start;
               tpset.objects.emplace_back(std::move(tp)); 
            }
	      } 
         if(!m_tpset_sink->try_send(std::move(tpset), iomanager::Sender::s_no_block)) {
            ers::warning(DroppedTPSet(ERS_HERE, m_start_win_ts, m_end_win_ts));
            m_num_periodic_send_failed++;
         }
         m_num_periodic_sent++;

         //remember what we sent for the next loop
         m_start_win_ts = m_end_win_ts;
       }
    }
    {
      std::lock_guard<std::mutex> lock(m_cv_mutex);
      m_requests_running--;
    }
    m_cv.notify_all();  
   return;
}

} // namespace fdreadoutlibs
} // namespace dunedaq
