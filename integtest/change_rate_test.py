import pytest
import os
import re

import integrationtest.data_file_checks as data_file_checks
import integrationtest.log_file_checks as log_file_checks
import integrationtest.data_classes as data_classes
import integrationtest.utility_functions as utility_functions
from integrationtest.verbosity_helper import IntegtestVerbosityLevels

import functools
print = functools.partial(print, flush=True)  # always flush print() output

pytest_plugins = "integrationtest.integrationtest_drunc"

# Values that help determine the running conditions
number_of_data_producers = 1
run_duration = 200  # seconds (has to be this long for 0.01Hz rate to show that it is working)
readout_window_time_before = 1000
readout_window_time_after = 1001

# Default values for validation parameters
expected_number_of_data_files = 4
check_for_logfile_errors = True
expected_event_count = 2
expected_event_count_tolerance = 1
wib1_frag_hsi_trig_params = {
    "fragment_type_description": "WIB",
    "fragment_type": "ProtoWIB",
    "hdf5_source_subsystem": "Detector_Readout",
    "expected_fragment_count": number_of_data_producers,
    "min_size_bytes": 37656,
    "max_size_bytes": 37656,
}
wib2_frag_params = {
    "fragment_type_description": "WIB2",
    "fragment_type": "WIB",
    "hdf5_source_subsystem": "Detector_Readout",
    "expected_fragment_count": number_of_data_producers,
    "min_size_bytes": 29808,
    "max_size_bytes": 30280,
}
wibeth_frag_params = {
    "fragment_type_description": "WIBEth",
    "fragment_type": "WIBEth",
    "hdf5_source_subsystem": "Detector_Readout",
    "expected_fragment_count": number_of_data_producers,
    "min_size_bytes": 7272,
    "max_size_bytes": 14472,
}
triggercandidate_frag_params = {
    "fragment_type_description": "Trigger Candidate",
    "fragment_type": "Trigger_Candidate",
    "hdf5_source_subsystem": "Trigger",
    "expected_fragment_count": 1,
    "min_size_bytes": 128,
    "max_size_bytes": 216,
}
hsi_frag_params = {
    "fragment_type_description": "HSI",
    "fragment_type": "Hardware_Signal",
    "hdf5_source_subsystem": "HW_Signals_Interface",
    "expected_fragment_count": 0,
    "min_size_bytes": 72,
    "max_size_bytes": 100,
}
ignored_logfile_problems = {
    "-controller": [
        "Worker with pid \\d+ was terminated due to signal 1",
        "Connection '.*' not found on the application registry",
    ],
    "connectivity-service": [
        "errorlog: -",
        "Worker with pid \\d+ was terminated due to signal 1",
    ],
}


conf_dict = data_classes.integtest_params_for_generated_dunedaq_config()
conf_dict.object_databases = ["config/daqsystemtest/integrationtest-objects.data.xml"]
conf_dict.dro_map_config.n_streams = number_of_data_producers
conf_dict.op_env = "integtest"
conf_dict.config_session_name = "changerate"
conf_dict.tpg_enabled = False

conf_dict.config_substitutions.append(
    data_classes.attribute_substitution(obj_class="LatencyBuffer", updates={"size": 50000})
)

conf_dict.config_substitutions.append(
    data_classes.attribute_substitution(
        obj_class="RandomTCMakerConf",
        updates={"trigger_rate_hz": 1.0},
    )
)

conf_dict.config_substitutions.append(
    data_classes.attribute_substitution(
        obj_class="TCReadoutMap",
        updates={
            "time_before": readout_window_time_before,
            "time_after": readout_window_time_after,
        },
    )
)

confgen_arguments = {"MinimalSystem": conf_dict}
# The commands to run in dunerc, as a list
dunerc_command_list = (
    "boot conf".split()
    + " start --run-number 101 wait 1 enable-triggers wait 2 disable-triggers wait 2 drain-dataflow stop-trigger-sources stop wait 2".split()
    + " start --run-number 102 wait 1 change-rate --trigger-rate 2.0 enable-triggers wait 1 disable-triggers wait 2 drain-dataflow stop-trigger-sources stop wait 2".split()
    + " start --run-number 103 wait 1 change-rate --trigger-rate 0.1 enable-triggers wait 20 disable-triggers wait 2 drain-dataflow stop-trigger-sources stop wait 2".split()
    + " start --run-number 104 wait 1 change-rate --trigger-rate 0.01 enable-triggers wait 200 disable-triggers wait 2 drain-dataflow stop-trigger-sources stop wait 2".split()
+ "scrap terminate".split()
)

# The tests themselves


def test_dunerc_success(run_dunerc, caplog):
    # check for run control success, problems during pytest setup, etc.
    utility_functions.basic_checks(run_dunerc, caplog, print_test_name=False)


def test_log_files(run_dunerc):
    if check_for_logfile_errors:
        # Check that there are no warnings or errors in the log files
        assert log_file_checks.logs_are_error_free(
            run_dunerc.log_files, True, True, ignored_logfile_problems,
            verbosity_helper=run_dunerc.verbosity_helper
        )


def test_data_files(run_dunerc):
    # Run some tests on the output data file
    assert len(run_dunerc.data_files) == expected_number_of_data_files

    fragment_check_list = [triggercandidate_frag_params, hsi_frag_params]
    # fragment_check_list.append(wib1_frag_hsi_trig_params) # ProtoWIB
    # fragment_check_list.append(wib2_frag_params) # DuneWIB
    fragment_check_list.append(wibeth_frag_params)  # WIBEth

    for idx in range(len(run_dunerc.data_files)):
        data_file = data_file_checks.DataFile(run_dunerc.data_files[idx], run_dunerc.verbosity_helper)
        assert data_file_checks.sanity_check(data_file)
        assert data_file_checks.check_file_attributes(data_file)
        assert data_file_checks.check_event_count(
            data_file, expected_event_count, expected_event_count_tolerance
        )
        for jdx in range(len(fragment_check_list)):
            assert data_file_checks.check_fragment_count(
                data_file, fragment_check_list[jdx]
            )
            assert data_file_checks.check_fragment_sizes(
                data_file, fragment_check_list[jdx]
            )


def test_cleanup(run_dunerc):
    utility_functions.remove_hdf5_files_if_requested(run_dunerc, this_test_requests_hdf5_file_removal=False)
