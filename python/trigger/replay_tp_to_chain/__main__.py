from rich.console import Console

from daqconf.core.system import System

# Add -h as default help option
CONTEXT_SETTINGS = dict(help_option_names=['-h', '--help'])

console = Console()

# Set moo schema search path
from dunedaq.env import get_moo_model_path
import moo.io
moo.io.default_load_path = get_moo_model_path()

import click

@click.command(context_settings=CONTEXT_SETTINGS)
@click.option('-s', '--slowdown-factor', default=1.0)
@click.option('-f', '--input-file', type=click.Path(exists=True, dir_okay=False), multiple=True)
@click.option('--trigger-activity-plugin', default='TriggerActivityMakerPrescalePlugin', help="Trigger activity algorithm plugin")
@click.option('--trigger-activity-config', default='dict(prescale=100)', help="Trigger activity algorithm config (string containing python dictionary)")
@click.option('--trigger-candidate-plugin', default='TriggerCandidateMakerPrescalePlugin', help="Trigger candidate algorithm plugin")
@click.option('--trigger-candidate-config', default='dict(prescale=100)', help="Trigger candidate algorithm config (string containing python dictionary)")
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

    the_system.apps["dataflow0"] = get_dataflow_app(
        HOSTIDX = 0,
        OUTPUT_PATH = ".",
        # OPERATIONAL_ENVIRONMENT = op_env,
        # TPC_REGION_NAME_PREFIX = tpc_region_name_prefix,
        # MAX_FILE_SIZE = max_file_size,
        # MAX_TRIGGER_RECORD_WINDOW = max_trigger_record_window,
        # MAX_EXPECTED_TR_SEQUENCES = max_expected_tr_sequences,
        # TOKEN_COUNT = trigemu_token_count,
        # TRB_TIMEOUT = trigger_record_building_timeout,
        HOST="localhost",
        # HAS_DQM=enable_dqm,
        # DEBUG=debug
    )

    the_system.apps['dfo'] = get_dfo_app(
        DF_COUNT = 1,
        # TOKEN_COUNT = trigemu_token_count,
        # STOP_TIMEOUT = dfo_stop_timeout,
        HOST="localhost",
        # DEBUG=debug
    )

    the_system.apps['trigger'] = get_trigger_app(
        SOFTWARE_TPG_ENABLED = True,
        FIRMWARE_TPG_ENABLED = False,
        DATA_RATE_SLOWDOWN_FACTOR = slowdown_factor,
        CLOCK_SPEED_HZ = 50_000_000,
        RU_CONFIG = ru_configs,
        ACTIVITY_PLUGIN = trigger_activity_plugin,
        ACTIVITY_CONFIG = eval(trigger_activity_config),
        CANDIDATE_PLUGIN = trigger_candidate_plugin,
        CANDIDATE_CONFIG = eval(trigger_candidate_config),
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
        # DEBUG=debug
    )

    from daqconf.core.fragment_producers import connect_all_fragment_producers, set_mlt_links

    connect_all_fragment_producers(the_system)
    set_mlt_links(the_system, "trigger")
    
    from daqconf.core.conf_utils import make_app_command_data, make_system_command_datas, write_json_files
    from daqconf.core.metadata import write_metadata_file
    
    app_command_datas = {
        name : make_app_command_data(the_system, app, name)
        for name,app in the_system.apps.items()
    }

    system_command_datas = make_system_command_datas(the_system)

    write_json_files(app_command_datas, system_command_datas, json_dir)

    write_metadata_file(json_dir, "replay_tp_to_chain")

if __name__ == '__main__':

    try:
            cli(show_default=True, standalone_mode=True)
    except Exception as e:
            console.print_exception()
