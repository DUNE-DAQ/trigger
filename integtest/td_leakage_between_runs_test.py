import pytest
import urllib.request

import integrationtest.data_file_checks as data_file_checks
import integrationtest.dro_map_gen as dro_map_gen
import integrationtest.log_file_checks as log_file_checks
import integrationtest.config_file_gen as config_file_gen

# Values that help determine the running conditions
number_of_data_producers=2
run_duration=20  # seconds
readout_window_time_before=1000
readout_window_time_after=1000

# Default values for validation parameters
expected_number_of_data_files=3
check_for_logfile_errors=True
expected_event_count=52
expected_event_count_tolerance=5
wibeth_frag_params={"fragment_type_description": "WIBEth",
                    "fragment_type": "WIBEth",
                    "hdf5_source_subsystem": "Detector_Readout",
                    "expected_fragment_count": number_of_data_producers,
                    "min_size_bytes": 7272, "max_size_bytes": 14472}
triggercandidate_frag_params={"fragment_type_description": "Trigger Candidate",
                              "fragment_type": "Trigger_Candidate",
                              "hdf5_source_subsystem": "Trigger",
                              "expected_fragment_count": 1,
                              "min_size_bytes": 72, "max_size_bytes": 216}
ignored_logfile_problems={}

# The next several variable declarations *must* be present as globals in the test
# file. They're read by the "fixtures" in conftest.py to determine how
# to run the config generation and nanorc

# The name of the python module for the config generation (required)
confgen_name="fddaqconf_gen"

# The detector readout map (optional in general, but an integral part of this test)
dro_map_contents = dro_map_gen.generate_dromap_contents(number_of_data_producers)

# The arguments to pass to the config generator, excluding the json
# output directory (the test framework handles that)
conf_dict = config_file_gen.get_default_config_dict()
conf_dict["detector"]["op_env"] = "integtest"
conf_dict["detector"]["clock_speed_hz"] = 62500000
conf_dict["readout"]["use_fake_cards"] = True

conf_dict["trigger"]["use_custom_maker"] = True
conf_dict["trigger"]["ctcm_trigger_intervals"] = [62500000, 31250000, 46875000]
conf_dict["trigger"]["ctcm_trigger_types"] = [1, 2, 3]
conf_dict["trigger"]["ctcm_timestamp_method"] = "kTimeSync"
conf_dict["trigger"]["mlt_merge_overlapping_tcs"] = True
conf_dict["trigger"]["mlt_send_timed_out_tds"] = True
conf_dict["trigger"]["mlt_buffer_timeout"] = 1000
conf_dict["trigger"]["mlt_td_readout_map"] = []
rmap_conf = {}
rmap_conf["tc_type_name"] = "kTiming"
rmap_conf["time_before"] = readout_window_time_before
rmap_conf["time_after"] = readout_window_time_after
conf_dict["trigger"]["mlt_td_readout_map"].append(rmap_conf)
rmap_conf = {}
rmap_conf["tc_type_name"] = "kTPCLowE"
rmap_conf["time_before"] = readout_window_time_before
rmap_conf["time_after"] = readout_window_time_after
conf_dict["trigger"]["mlt_td_readout_map"].append(rmap_conf)
rmap_conf = {}
rmap_conf["tc_type_name"] = "kSupernova"
rmap_conf["time_before"] = readout_window_time_before
rmap_conf["time_after"] = readout_window_time_after
conf_dict["trigger"]["mlt_td_readout_map"].append(rmap_conf)
conf_dict["hsi"]["use_fake_hsi"] = False
conf_dict["hsi"]["use_timing_hsi"] = False

conf_dict["readout"]["data_files"] = []
datafile_conf = {}
datafile_conf["data_file"] = "asset://?checksum=e96fd6efd3f98a9a3bfaba32975b476e" # WIBEth
datafile_conf["detector_id"] = 3
conf_dict["readout"]["data_files"].append(datafile_conf)

confgen_arguments={"MinimalSystem": conf_dict}  # (required)

# The commands to run in nanorc, as a list (required)
nanorc_command_list="integtest-partition boot conf start_run 101 wait ".split() + [str(run_duration)] + " stop_run wait 2 start_run 102 wait ".split() + [str(run_duration)] + " stop_run wait 2 start_run 103 wait ".split() + [str(run_duration)] + " stop_run wait 2 scrap terminate".split()

# The tests themselves

def test_nanorc_success(run_nanorc):
    # Check that nanorc completed correctly
    assert run_nanorc.completed_process.returncode==0

def test_log_files(run_nanorc):
    if check_for_logfile_errors:
        # Check that there are no warnings or errors in the log files
        assert log_file_checks.logs_are_error_free(run_nanorc.log_files, True, True, ignored_logfile_problems)

def test_data_files(run_nanorc):
    # Run some tests on the output data file
    assert len(run_nanorc.data_files)==expected_number_of_data_files

    fragment_check_list=[triggercandidate_frag_params]
    fragment_check_list.append(wibeth_frag_params)

    for idx in range(len(run_nanorc.data_files)):
        data_file=data_file_checks.DataFile(run_nanorc.data_files[idx])
        assert data_file_checks.sanity_check(data_file)
        assert data_file_checks.check_file_attributes(data_file)
        assert data_file_checks.check_event_count(data_file, expected_event_count, expected_event_count_tolerance)
        for jdx in range(len(fragment_check_list)):
            assert data_file_checks.check_fragment_count(data_file, fragment_check_list[jdx])
            assert data_file_checks.check_fragment_sizes(data_file, fragment_check_list[jdx])
