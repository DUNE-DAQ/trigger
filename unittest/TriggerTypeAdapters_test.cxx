/**
 * @file TriggerTypeAdapters_test.cxx
 *
 * Unittest for testing lower bound and request handling on trigger type adapters/wrappers.
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2022.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#define BOOST_TEST_MODULE TriggerTypeAdapters_test // NOLINT

#include "trigger/TriggerPrimitiveTypeAdapter.hpp"
#include "trigger/TAWrapper.hpp"
#include "trigger/TCWrapper.hpp"

#include "datahandlinglibs/testutils/TestUtilities.hpp"
#include "datahandlinglibs/models/SkipListLatencyBufferModel.hpp"

#include "boost/test/unit_test.hpp"

BOOST_AUTO_TEST_SUITE(TriggerTypeAdapters_test)

BOOST_AUTO_TEST_CASE(SkipListLatencyBufferModel_TriggerPrimitiveTypeAdapter_TestQueue)
{
    dunedaq::datahandlinglibs::test::test_queue_model<
            dunedaq::datahandlinglibs::SkipListLatencyBufferModel,
            dunedaq::trigger::TriggerPrimitiveTypeAdapter>();
}
BOOST_AUTO_TEST_CASE(SkipListLatencyBufferModel_TriggerPrimitiveTypeAdapter_TestRequest)
{
    dunedaq::datahandlinglibs::test::test_request_model<
            dunedaq::datahandlinglibs::SkipListLatencyBufferModel,
            dunedaq::trigger::TriggerPrimitiveTypeAdapter>();
}

BOOST_AUTO_TEST_CASE(SkipListLatencyBufferModel_TAWrapper_TestQueue)
{
    dunedaq::datahandlinglibs::test::test_queue_model<
            dunedaq::datahandlinglibs::SkipListLatencyBufferModel,
            dunedaq::trigger::TAWrapper>();
}
BOOST_AUTO_TEST_CASE(SkipListLatencyBufferModel_TAWrapper_TestRequest)
{
    dunedaq::datahandlinglibs::test::test_request_model<
            dunedaq::datahandlinglibs::SkipListLatencyBufferModel,
            dunedaq::trigger::TAWrapper>();
}

BOOST_AUTO_TEST_CASE(SkipListLatencyBufferModel_TCWrapper_TestQueue)
{
    dunedaq::datahandlinglibs::test::test_queue_model<
            dunedaq::datahandlinglibs::SkipListLatencyBufferModel,
            dunedaq::trigger::TCWrapper>();
}
BOOST_AUTO_TEST_CASE(SkipListLatencyBufferModel_TCWrapper_TestRequest)
{
    dunedaq::datahandlinglibs::test::test_request_model<
            dunedaq::datahandlinglibs::SkipListLatencyBufferModel,
            dunedaq::trigger::TCWrapper>();
}
BOOST_AUTO_TEST_SUITE_END()


