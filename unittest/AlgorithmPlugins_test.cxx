/**
 * @file AlgorithmPlugins_test.cxx AlgorithmPlugins class Unit Tests
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have received with this code.
 */

#include "trigger/AlgorithmPlugins.hpp"

/**
 * @brief Name of this test module
 */
#define BOOST_TEST_MODULE AlgorithmPlugins_test //NOLINT

#include "boost/test/unit_test.hpp"

using namespace dunedaq;

BOOST_AUTO_TEST_SUITE(BOOST_TEST_MODULE);

BOOST_AUTO_TEST_CASE(TAFactory)
{
  // Get a few algorithms
  std::unique_ptr<triggeralgs::TriggerActivityMaker> prescale_maker = trigger::make_ta_maker("TAMakerPrescaleAlgorithm");
  std::unique_ptr<triggeralgs::TriggerActivityMaker> h_muon_maker = trigger::make_ta_maker("TAMakerHorizontalMuonAlgorithm");
  std::unique_ptr<triggeralgs::TriggerActivityMaker> dbscan_maker = trigger::make_ta_maker("TriggerActivityMakerDBSCANPlugin");
  std::unique_ptr<triggeralgs::TriggerActivityMaker> fake_maker = trigger::make_ta_maker("TriggerActivityMakerFakerPlugin");

  // Only the fake_maker should be nullptr
  BOOST_TEST(static_cast<bool>(prescale_maker != nullptr));
  BOOST_TEST(static_cast<bool>(h_muon_maker != nullptr));
  BOOST_TEST(static_cast<bool>(dbscan_maker != nullptr));
  BOOST_TEST(static_cast<bool>(fake_maker == nullptr));

  // Naive TPs and TAMaker usage.
  std::vector<triggeralgs::TriggerActivity> prescale_ta;
  std::vector<triggeralgs::TriggerActivity> h_muon_ta;
  std::vector<triggeralgs::TriggerActivity> dbscan_ta;

  triggeralgs::TriggerPrimitive tp;

  for (int idx = 0; idx < 10; idx++) {
    tp.type = triggeralgs::TriggerPrimitive::Type::kTPC;
    tp.algorithm = triggeralgs::TriggerPrimitive::Algorithm::kSimpleThreshold;
    tp.time_start = idx;
    tp.time_peak = 1+idx;
    tp.time_over_threshold = 2;
    tp.adc_integral = 1000+idx;
    tp.adc_peak = 1000+idx;
    tp.channel = 0+idx;
    tp.detid = 0;
    (*prescale_maker)(tp, prescale_ta);
    (*h_muon_maker)(tp, h_muon_ta);
    (*dbscan_maker)(tp, dbscan_ta);
  }

  // Prescale will populate the TA vector
  bool prescale_test = prescale_ta[0].algorithm == triggeralgs::TriggerActivity::Algorithm::kPrescale;
  BOOST_TEST(prescale_test);

  // More complex algorithms may not populate the TA vector (for this naive test)
  if (h_muon_ta.size()) {
    bool h_muon_test = h_muon_ta[0].algorithm == triggeralgs::TriggerActivity::Algorithm::kHorizontalMuon;
    BOOST_TEST(h_muon_test);
  }

  if (dbscan_ta.size()) {
    bool dbscan_test = dbscan_ta[0].algorithm == triggeralgs::TriggerActivity::Algorithm::kDBSCAN;
    BOOST_TEST(dbscan_test);
  }
}

BOOST_AUTO_TEST_SUITE_END()
