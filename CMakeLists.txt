cmake_minimum_required(VERSION 3.12)
project(trigger VERSION 1.8.0)

find_package(daq-cmake REQUIRED)

daq_setup_environment()

find_package(ers REQUIRED)
find_package(Boost COMPONENTS iostreams unit_test_framework REQUIRED)
find_package(serialization REQUIRED)
find_package(logging REQUIRED)
find_package(appfwk REQUIRED)
find_package(triggeralgs REQUIRED)
find_package(dfmessages REQUIRED)
find_package(utilities REQUIRED)
find_package(iomanager REQUIRED)
find_package(daqdataformats REQUIRED)
find_package(detdataformats REQUIRED)
find_package(trgdataformats REQUIRED)
find_package(detchannelmaps REQUIRED)
find_package(hdf5libs REQUIRED)
find_package(readoutlibs REQUIRED)
find_package(CLI11 REQUIRED)

##############################################################################
# Main library 

daq_add_library(TokenManager.cpp LivetimeCounter.cpp
  LINK_LIBRARIES
  appfwk::appfwk
  logging::logging
  dfmessages::dfmessages
  triggeralgs::triggeralgs
  utilities::utilities
  iomanager::iomanager
  detdataformats::detdataformats
  trgdataformats::trgdataformats
  hdf5libs::hdf5libs
  Boost::iostreams # Boost::iostreams comes in via readoutlibs
  detchannelmaps::detchannelmaps)

##############################################################################
# Codegen

## As a one liner, this call is a major source of git conflicts due to
## us all changing it.  Please keep one file per line to give git
## merge a chance to succeed.
daq_codegen(
  faketpcreatorheartbeatmaker.jsonnet
  moduleleveltrigger.jsonnet
  randomtriggercandidatemaker.jsonnet
  customtriggercandidatemaker.jsonnet
  timingtriggercandidatemaker.jsonnet
  ctbtriggercandidatemaker.jsonnet
  triggerprimitivemaker.jsonnet
  triggeractivitymaker.jsonnet
  triggercandidatemaker.jsonnet
  triggerdecisionmaker.jsonnet
  triggerzipper.jsonnet
  tpsetbuffercreator.jsonnet
  tasetsink.jsonnet
  tpchannelfilter.jsonnet
  TEMPLATES Structs.hpp.j2 Nljs.hpp.j2 )

daq_codegen(
  txbuffer.jsonnet DEP_PKGS readoutlibs TEMPLATES Structs.hpp.j2 Nljs.hpp.j2 )
  
daq_codegen( *info.jsonnet DEP_PKGS opmonlib TEMPLATES opmonlib/InfoStructs.hpp.j2 opmonlib/InfoNljs.hpp.j2 )
##############################################################################
# Plugins

daq_add_plugin(TimingTriggerCandidateMaker duneDAQModule SCHEMA LINK_LIBRARIES trigger)
daq_add_plugin(CTBTriggerCandidateMaker duneDAQModule SCHEMA LINK_LIBRARIES trigger)
daq_add_plugin(TriggerPrimitiveMaker duneDAQModule LINK_LIBRARIES trigger)
daq_add_plugin(TriggerActivityMaker duneDAQModule LINK_LIBRARIES trigger)
daq_add_plugin(TriggerCandidateMaker duneDAQModule LINK_LIBRARIES trigger)
daq_add_plugin(TriggerDecisionMaker duneDAQModule LINK_LIBRARIES trigger)
daq_add_plugin(RandomTriggerCandidateMaker duneDAQModule LINK_LIBRARIES trigger)
daq_add_plugin(CustomTriggerCandidateMaker duneDAQModule LINK_LIBRARIES trigger)
daq_add_plugin(ModuleLevelTrigger duneDAQModule LINK_LIBRARIES trigger)
daq_add_plugin(TPSetSink duneDAQModule LINK_LIBRARIES trigger TEST)
daq_add_plugin(TASetSink duneDAQModule LINK_LIBRARIES trigger TEST)
daq_add_plugin(FakeTPCreatorHeartbeatMaker duneDAQModule LINK_LIBRARIES trigger)
daq_add_plugin(TPSetBufferCreator duneDAQModule LINK_LIBRARIES trigger)
daq_add_plugin(TPBuffer duneDAQModule LINK_LIBRARIES trigger readoutlibs::readoutlibs)
daq_add_plugin(TABuffer duneDAQModule LINK_LIBRARIES trigger readoutlibs::readoutlibs)
daq_add_plugin(TCBuffer duneDAQModule LINK_LIBRARIES trigger readoutlibs::readoutlibs)
daq_add_plugin(TPChannelFilter duneDAQModule LINK_LIBRARIES trigger)
daq_add_plugin(TPSetTee duneDAQModule LINK_LIBRARIES trigger)
daq_add_plugin(TASetTee duneDAQModule LINK_LIBRARIES trigger)
daq_add_plugin(TCTee duneDAQModule LINK_LIBRARIES trigger)

daq_add_plugin(TPZipper duneDAQModule LINK_LIBRARIES trigger)
daq_add_plugin(TAZipper duneDAQModule LINK_LIBRARIES trigger)
daq_add_plugin(TCZipper duneDAQModule LINK_LIBRARIES trigger)


# Algorithm plugins
daq_add_plugin(TriggerDecisionMakerSupernovaPlugin duneTDMaker LINK_LIBRARIES trigger)

##############################################################################
# Integration tests

daq_add_application( set_serialization_speed set_serialization_speed.cxx TEST LINK_LIBRARIES trigger)
daq_add_application( taset_serialization taset_serialization.cxx TEST LINK_LIBRARIES trigger)
daq_add_application( print_trigger_type print_trigger_type.cxx TEST LINK_LIBRARIES trigger hdf5libs::hdf5libs CLI11::CLI11)
daq_add_application( print_ds_fragments print_ds_fragments.cxx TEST LINK_LIBRARIES trigger hdf5libs::hdf5libs CLI11::CLI11)
daq_add_application( generate_tpset_from_hdf5 generate_tpset_from_hdf5.cxx TEST LINK_LIBRARIES trigger hdf5libs::hdf5libs CLI11::CLI11)

##############################################################################
# Unit Tests

daq_add_unit_test(TokenManager_test              LINK_LIBRARIES trigger)
daq_add_unit_test(TxSet_test                     LINK_LIBRARIES trigger)
daq_add_unit_test(BufferManager_test             LINK_LIBRARIES trigger)
daq_add_unit_test(TriggerZipper_test             LINK_LIBRARIES trigger)
daq_add_unit_test(TriggerObjectOverlay_test      LINK_LIBRARIES trigger)
daq_add_unit_test(AlgorithmPlugins_test          LINK_LIBRARIES trigger)

##############################################################################

daq_install()
