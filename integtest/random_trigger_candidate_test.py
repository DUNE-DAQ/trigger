import pytest
import math
import copy
import urllib.request

import integrationtest.data_file_checks as data_file_checks
import integrationtest.dro_map_gen as dro_map_gen
import integrationtest.log_file_checks as log_file_checks
import integrationtest.config_file_gen as config_file_gen

# Values that help determine the running conditions
number_of_data_producers=3
data_rate_slowdown_factor=1 # 10 for ProtoWIB/DuneWIB
run_duration=20  # seconds
readout_window_time_before=1000
readout_window_time_after=1000

# Default values for validation parameters
expected_number_of_data_files=1
check_for_logfile_errors=True
expected_event_count=run_duration
expected_event_count_tolerance=2
readout_window_scale_factor=1.5
wibeth_frag_params={"fragment_type_description": "WIBEth",
                    "fragment_type": "WIBEth",
                    "hdf5_source_subsystem": "Detector_Readout",
                    "expected_fragment_count": number_of_data_producers,
                    "min_size_bytes": 7272, "max_size_bytes": 14472}
wibeth_frag_params_scaled={"fragment_type_description": "ScaledWIBEth",
                           "fragment_type": "WIBEth",
                           "hdf5_source_subsystem": "Detector_Readout",
                           "expected_fragment_count": number_of_data_producers,
                           "min_size_bytes": 72+( math.ceil(readout_window_scale_factor)   *7200),
                           "max_size_bytes": 72+((math.ceil(readout_window_scale_factor)+1)*7200)}
rtcm_frag_params ={"fragment_type_description": "Trigger Candidate",
                   "fragment_type": "Trigger_Candidate",
                   "hdf5_source_subsystem": "Trigger",
                   "expected_fragment_count": 1, 
                   "min_size_bytes": 72, "max_size_bytes": 216}
ignored_logfile_problems={}

# The variable declarations of confgen_name, confgen_arguments, and 
# nanorc_command_list *must* be present as globals in the test
# file. They're read by the "fixtures" in conftest.py to determine how
# to run the config generation and nanorc

# The name of the python module for the config generation
confgen_name="fddaqconf_gen"
# The arguments to pass to the config generator, excluding the json
# output directory (the test framework handles that)

dro_map_contents = dro_map_gen.generate_dromap_contents(number_of_data_producers)

conf_dict = config_file_gen.get_default_config_dict()
conf_dict["detector"]["op_env"] = "integtest"
conf_dict["daq_common"]["data_rate_slowdown_factor"] = data_rate_slowdown_factor
conf_dict["detector"]["clock_speed_hz"] = 62500000 # DuneWIB/WIBEth
conf_dict["readout"]["use_fake_cards"] = True
conf_dict["trigger"]["trigger_window_before_ticks"] = readout_window_time_before
conf_dict["trigger"]["trigger_window_after_ticks"]  = readout_window_time_after

# Enable random trigger candidate maker
conf_dict["trigger"]["use_random_maker"] = True
conf_dict["trigger"]["rtcm_timestamp_method"] = "kTimeSync"
conf_dict["trigger"]["rtcm_time_distribution"] = "kUniform"
conf_dict["trigger"]["rtcm_trigger_interval_ticks"] = 62500000

conf_dict["readout"]["data_files"] = []
datafile_conf = {}
datafile_conf["data_file"] = "asset://?checksum=e96fd6efd3f98a9a3bfaba32975b476e" # WIBEth
datafile_conf["detector_id"] = 3
conf_dict["readout"]["data_files"].append(datafile_conf)

# Disable fake HSI
conf_dict["hsi"]["use_fake_hsi"] = False
conf_dict["hsi"]["use_timing_hsi"] = False
conf_dict["hsi"]["random_trigger_rate_hz"] = 1.0

# Readout map config
conf_dict["trigger"]["mlt_use_readout_map"] = True
conf_dict["trigger"]["mlt_td_readout_map"] = []
rmap_conf = {}
rmap_conf["candidate_type"] = 4
rmap_conf["time_before"] = readout_window_time_before
rmap_conf["time_after"] = readout_window_time_after
conf_dict["trigger"]["mlt_td_readout_map"].append(rmap_conf)

# conf_dict with readout window scaling
conf_dict_scaled = copy.deepcopy(conf_dict)
conf_dict_scaled["trigger"]["trigger_window_before_ticks"] = readout_window_scale_factor*readout_window_time_before
conf_dict_scaled["trigger"]["trigger_window_after_ticks"]  = readout_window_scale_factor*readout_window_time_after
conf_dict_scaled["trigger"]["mlt_td_readout_map"] = []
rmap_conf_scaled = copy.deepcopy(rmap_conf)
rmap_conf_scaled["time_before"] = readout_window_scale_factor*readout_window_time_before
rmap_conf_scaled["time_after"] = readout_window_scale_factor*readout_window_time_after
conf_dict_scaled["trigger"]["mlt_td_readout_map"].append(rmap_conf_scaled)

confgen_arguments={"RandomTriggerCandidateMaker": conf_dict,
                   "RandomTriggerCandidateMakerScaled": conf_dict_scaled
                  }

# The commands to run in nanorc, as a list
nanorc_command_list="integtest-partition boot conf start 101 wait 1 enable_triggers wait ".split() + [str(run_duration)] + "disable_triggers wait 2 stop_run wait 2 scrap terminate".split()

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

    fragment_check_list=[rtcm_frag_params]
    if run_nanorc.confgen_config["trigger"]["mlt_td_readout_map"][0]["time_before"] == 1000:
        fragment_check_list.append(wibeth_frag_params) # WIBEth
    else:
        fragment_check_list.append(wibeth_frag_params_scaled) # ScaledWIBEth

    for idx in range(len(run_nanorc.data_files)):
        data_file=data_file_checks.DataFile(run_nanorc.data_files[idx])
        assert data_file_checks.sanity_check(data_file)
        assert data_file_checks.check_file_attributes(data_file)
        assert data_file_checks.check_event_count(data_file, expected_event_count, expected_event_count_tolerance)
        for jdx in range(len(fragment_check_list)):
            assert data_file_checks.check_fragment_count(data_file, fragment_check_list[jdx])
            assert data_file_checks.check_fragment_sizes(data_file, fragment_check_list[jdx])

