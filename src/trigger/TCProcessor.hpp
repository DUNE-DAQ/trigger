/**
 * @file TCProcessor.hpp TA specific Task based raw processor
 *
 * This is part of the DUNE DAQ , copyright 2022.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef TRIGGER_SRC_TRIGGER_TCPROCESSOR_HPP_
#define TRIGGER_SRC_TRIGGER_TCPROCESSOR_HPP_

#include "iomanager/IOManager.hpp"
#include "iomanager/Sender.hpp"
#include "logging/Logging.hpp"

#include "appmodel/DataHandlerModule.hpp"
#include "appmodel/TCReadoutMap.hpp"
#include "appmodel/ROIGroupConf.hpp"
#include "appmodel/SourceIDConf.hpp"

#include "datahandlinglibs/models/TaskRawDataProcessorModel.hpp"

#include "trigger/Issues.hpp"
#include "trigger/TCWrapper.hpp"

#include "daqdataformats/SourceID.hpp"
#include "dfmessages/TriggerDecision.hpp"

#include "trgdataformats/TriggerCandidateData.hpp"
#include "trgdataformats/Types.hpp"
#include "triggeralgs/TriggerCandidate.hpp"

namespace dunedaq {
namespace trigger {

class TCProcessor : public datahandlinglibs::TaskRawDataProcessorModel<TCWrapper>
{

public:
  using inherited = datahandlinglibs::TaskRawDataProcessorModel<TCWrapper>;
  using tcptr = TCWrapper*;
  using consttcptr = const TCWrapper*;

  explicit TCProcessor(std::unique_ptr<datahandlinglibs::FrameErrorRegistry>& error_registry, bool post_processing_enabled);

  ~TCProcessor();

  void start(const nlohmann::json& args) override;

  void stop(const nlohmann::json& args) override;

  void conf(const appmodel::DataHandlerModule* conf) override;

  //  void get_info(opmonlib::InfoCollector& ci, int level) override;

protected:

  void make_td(const TCWrapper* tc);

  private:
  void send_trigger_decisions();
  std::thread m_send_trigger_decisions_thread;

  // TD requests
  std::vector<daqdataformats::SourceID> m_mandatory_links;
  std::map<int, std::vector<daqdataformats::SourceID>> m_group_links;
  nlohmann::json m_group_links_data;
  int m_total_group_links;
  void parse_group_links(const nlohmann::json& data);
  void print_group_links();

  dfmessages::ComponentRequest create_request_for_link(daqdataformats::SourceID link,
                                                       triggeralgs::timestamp_t start,
                                                       triggeralgs::timestamp_t end);
  std::vector<dfmessages::ComponentRequest> create_all_decision_requests(std::vector<daqdataformats::SourceID> links,
                                                                         triggeralgs::timestamp_t start,
                                                                         triggeralgs::timestamp_t end);
  void add_requests_to_decision(dfmessages::TriggerDecision& decision,
                                std::vector<dfmessages::ComponentRequest> requests);

  // ROI
  bool m_use_roi_readout;
  struct roi_group
  {
    int n_links;
    float prob;
    triggeralgs::timestamp_t time_window;
    std::string mode;
  };
  std::map<int, roi_group> m_roi_conf;
  std::vector<const appmodel::ROIGroupConf*> m_roi_conf_data;
  void parse_roi_conf(const std::vector<const appmodel::ROIGroupConf*>& data);
  void print_roi_conf(std::map<int, roi_group> roi_conf);
  std::vector<int> m_roi_conf_ids;
  std::vector<float> m_roi_conf_probs;
  std::vector<float> m_roi_conf_probs_c;
  float get_random_num_float(float limit);
  int get_random_num_int();
  int pick_roi_group_conf();
  void roi_readout_make_requests(dfmessages::TriggerDecision& decision);

  int m_repeat_trigger_count{ 1 };
  std::atomic<bool> m_hsi_passthrough;
  std::atomic<bool> m_tc_merging;

  dfmessages::trigger_number_t m_last_trigger_number;

  dfmessages::run_number_t m_run_number;

  std::atomic<bool> m_running_flag{ false };

  // New buffering
  struct PendingTD
  {
    std::vector<triggeralgs::TriggerCandidate> contributing_tcs;
    triggeralgs::timestamp_t readout_start;
    triggeralgs::timestamp_t readout_end;
    int64_t walltime_expiration;
  };
  std::vector<PendingTD> m_pending_tds;
  std::mutex m_td_vector_mutex;

  void add_tc(const triggeralgs::TriggerCandidate tc);
  void add_tc_ignored(const triggeralgs::TriggerCandidate tc);
  void call_tc_decision(const PendingTD& pending_td);
  bool check_overlap(const triggeralgs::TriggerCandidate& tc, const PendingTD& pending_td);
  //bool check_overlap_td(const PendingTD& pending_td);
  bool check_td_readout_length(const PendingTD&);
  void clear_td_vectors();
  std::vector<PendingTD> get_ready_tds(std::vector<PendingTD>& pending_tds);
  int64_t m_buffer_timeout;
  int64_t m_td_readout_limit;
  std::atomic<bool> m_send_timed_out_tds;
  int m_earliest_tc_index;
  int get_earliest_tc_index(const PendingTD& pending_td);

  // Bitwords logic
  bool m_use_bitwords;
  nlohmann::json m_trigger_bitwords_json;
  bool m_bitword_check;
  std::bitset<16> m_TD_bitword;
  std::vector<std::bitset<16>> m_trigger_bitwords;
  std::bitset<16> get_TD_bitword(const PendingTD& ready_td);
  void print_trigger_bitwords(std::vector<std::bitset<16>> trigger_bitwords);
  bool check_trigger_bitwords();
  void print_bitword_flags(nlohmann::json m_trigger_bitwords_json);
  void set_trigger_bitwords();
  void set_trigger_bitwords(const std::vector<std::string>& _bitwords);

  // Readout map config
  bool m_use_readout_map;
  std::vector<const appmodel::TCReadoutMap*>  m_readout_window_map_data;
  std::map<trgdataformats::TriggerCandidateData::Type, std::pair<triggeralgs::timestamp_t, triggeralgs::timestamp_t>>
    m_readout_window_map;
  void parse_readout_map(const std::vector<const appmodel::TCReadoutMap*>& data);
  void print_readout_map(std::map<trgdataformats::TriggerCandidateData::Type,
                                  std::pair<triggeralgs::timestamp_t, triggeralgs::timestamp_t>> map);

  // Create the next trigger decision
  dfmessages::TriggerDecision create_decision(const PendingTD& pending_td);
  dfmessages::trigger_type_t m_trigger_type_shifted;

  // Optional list of TC types to ignore
  std::vector<unsigned int> m_ignored_tc_types;
  bool m_ignoring_tc_types;
  bool check_trigger_type_ignore(unsigned int tc_type);


 // output queue for TDs
  std::shared_ptr<iomanager::SenderConcept<dfmessages::TriggerDecision>> m_td_sink;

  // opmon
  std::atomic<uint64_t> m_new_tds{ 0 };  // NOLINT(build/unsigned)
  std::atomic<uint64_t> m_tds_dropped{ 0 };
  std::atomic<uint64_t> m_td_dropped_tc_count{ 0 };
  std::atomic<uint64_t> m_tc_ignored_count{ 0 };

};

} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_SRC_TRIGGER_TCPROCESSOR_HPP_
