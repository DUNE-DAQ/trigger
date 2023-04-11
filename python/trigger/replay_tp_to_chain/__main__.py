from rich.console import Console

from daqconf.core.system import System
from daqconf.core.sourceid import *

from tempfile import NamedTemporaryFile

# Add -h as default help option
CONTEXT_SETTINGS = dict(help_option_names=['-h', '--help'])

console = Console()

# Set moo schema search path
from dunedaq.env import get_moo_model_path
import moo.io
moo.io.default_load_path = get_moo_model_path()

import moo.otypes
moo.otypes.load_types('detchannelmaps/hardwaremapservice.jsonnet')
import dunedaq.detchannelmaps.hardwaremapservice as hwms

import click

@click.command(context_settings=CONTEXT_SETTINGS)
@click.option('-s', '--slowdown-factor', default=1.0)
@click.option('-f', '--input-file', type=click.Path(exists=True, dir_okay=False), multiple=True)
@click.option('--trigger-activity-plugin', default=['TriggerActivityMakerPrescalePlugin'], multiple=True, help="List of activity algorithm plugins")
@click.option('--trigger-activity-config', default=['dict(prescale=100)'], multiple=True, help="Trigger activity algorithm config (string containing python dictionary)")
@click.option('--trigger-candidate-plugin', default=['TriggerCandidateMakerPrescalePlugin'], multiple=True, help="Trigger candidate algorithm plugin")
@click.option('--trigger-candidate-config', default=['dict(prescale=100)'], multiple=True, help="Trigger candidate algorithm config (string containing python dictionary)")
@click.option('-l', '--number-of-loops', default='-1', help="Number of times to loop over the input files (-1 for infinite)")
@click.argument('json_dir', type=click.Path())
def cli(slowdown_factor, input_file, trigger_activity_plugin, trigger_activity_config, trigger_candidate_plugin, trigger_candidate_config, number_of_loops, json_dir):
    """
      JSON_DIR: Json file output folder
    """
    the_system = System()

    console.log("Loading faketp config generator")
    from .replay_tp_app import get_replay_app
    from daqconf.apps.dataflow_gen import get_dataflow_app
    from daqconf.apps.trigger_gen import get_trigger_app
    from daqconf.apps.dfo_gen import get_dfo_app

    console.log(f"Generating configs")

    ru_configs=[{"host": "localhost",
                 "card_id": 0,
                 "region_id": i,
                 "start_channel": 0,
                 "channel_count": 1} for i in range(len(input_file))]

    the_system.apps["replay"] = get_replay_app(
        INPUT_FILES = input_file,
        SLOWDOWN_FACTOR = slowdown_factor,
        NUMBER_OF_LOOPS = number_of_loops
    )

    # Create a hw map file based on the input files we were given on
    # the command line. Then we can get the SourceIDBroker to make the
    # necessary TPInfo objects for get_trigger_app()
    hw_map_file = open("hardware_map.txt", "x")
    for idx,f in enumerate(input_file):
        hw_map_file.write(f"0 0 0 {idx} 3 localhost {idx} 0 0\n")
    hw_map_file.close() # Save the file so the next line sees the changes
    hw_map_service = HardwareMapService(hw_map_file.name)
    serialized_hw_map = hw_map_service.get_hardware_map_json()
    hw_map = hwms.HardwareMap(serialized_hw_map)  # This is the object we need to pass to get_dataflow_app() now.

    the_system.apps["dataflow0"] = get_dataflow_app(
        HOSTIDX = 0,
        # OUTPUT_PATH = ".",
        # OPERATIONAL_ENVIRONMENT = op_env,
        # TPC_REGION_NAME_PREFIX = tpc_region_name_prefix,
        # MAX_FILE_SIZE = max_file_size,
        # MAX_TRIGGER_RECORD_WINDOW = max_trigger_record_window,
        # MAX_EXPECTED_TR_SEQUENCES = max_expected_tr_sequences,
        # TOKEN_COUNT = trigemu_token_count,
        # TRB_TIMEOUT = trigger_record_building_timeout,
        HARDWARE_MAP=hw_map, 
        HOST="localhost",
        # HAS_DQM=enable_dqm,
        # DEBUG=debug
    )

    # get_dfo_app() expects the dataflow conf structs to be passed to dfo_gen, so we hard code them here
    moo.otypes.load_types('daqconf/confgen.jsonnet')
    import dunedaq.daqconf.confgen as confgen

    df_apps = {
        'dataflow0': confgen.dataflowapp(
            host_df='localhost',
            max_file_size=4294967296,
            max_trigger_record_window=0,
            output_paths=['.'],
            # token_count=9
        )
    }
    df_apps['dataflow0'].source_id = 0

    the_system.apps['dfo'] = get_dfo_app(
        DF_CONF = df_apps,
        # DF_COUNT = 1,
        # TOKEN_COUNT = trigemu_token_count,
        # STOP_TIMEOUT = dfo_stop_timeout,
        HOST="localhost",
        # DEBUG=debug
    )

    # Attempt to fix replay app with source ID broker
    sourceid_broker = SourceIDBroker()
    
    # Get the list of RU processes - required to create instances of TXInfo later
    dro_infos = hw_map_service.get_all_dro_info()

    enable_firmware_tpg = False
    enable_software_tpg = True  # We always want software TPG for replay app

    sourceid_broker.register_readout_source_ids(dro_infos, TPGenMode.SWTPG)
    tp_mode = get_tpg_mode(enable_firmware_tpg,enable_software_tpg)
    sourceid_broker.generate_trigger_source_ids(dro_infos, tp_mode)
    tp_infos = sourceid_broker.get_all_source_ids("Trigger")

    the_system.apps['trigger'] = get_trigger_app(
        # SOFTWARE_TPG_ENABLED = True,
        # FIRMWARE_TPG_ENABLED = False,
        DATA_RATE_SLOWDOWN_FACTOR = slowdown_factor,
        CLOCK_SPEED_HZ = 62_500_000,
        TP_CONFIG = tp_infos,
        # RU_CONFIG = ru_configs,
        ACTIVITY_PLUGIN = trigger_activity_plugin,
        ACTIVITY_PLUGINS = trigger_activity_plugin,
        ACTIVITY_CONFIG = trigger_activity_config,
        CANDIDATE_PLUGIN = trigger_candidate_plugin,
        CANDIDATE_CONFIG = trigger_candidate_config,
        USE_HSI_INPUT = False,
        # SYSTEM_TYPE = system_type,
        # TTCM_S1=ttcm_s1,
        # TTCM_S2=ttcm_s2,
        # TRIGGER_WINDOW_BEFORE_TICKS = trigger_window_before_ticks,
        # TRIGGER_WINDOW_AFTER_TICKS = trigger_window_after_ticks,
        # HSI_TRIGGER_TYPE_PASSTHROUGH = hsi_trigger_type_passthrough,
        USE_CHANNEL_FILTER = False,
        # CHANNEL_MAP_NAME = tpg_channel_map,
        # DATA_REQUEST_TIMEOUT=trigger_data_request_timeout,
        HOST="localhost",
        # DEBUG=True
    )

    from daqconf.core.fragment_producers import connect_all_fragment_producers, set_mlt_links

    connect_all_fragment_producers(the_system)
    set_mlt_links(the_system, "trigger")

    boot = confgen.boot()

    from daqconf.core.conf_utils import make_app_command_data, make_system_command_datas, write_json_files
    from daqconf.core.metadata import write_metadata_file

    app_command_datas = {
        name : make_app_command_data(the_system, app, name, use_k8s=boot.use_k8s, use_connectivity_service=boot.use_connectivity_service)
        for name,app in the_system.apps.items()
    }

    system_command_datas = make_system_command_datas(boot, the_system)

    write_json_files(app_command_datas, system_command_datas, json_dir)

    write_metadata_file(json_dir, "replay_tp_to_chain", "./daqconf.ini")

if __name__ == '__main__':

    try:
            cli(show_default=True, standalone_mode=True)
    except Exception as e:
            console.print_exception()
