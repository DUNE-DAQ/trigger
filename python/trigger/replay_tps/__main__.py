#!/usr/bin/env python3
import click
import math
import os.path
from rich.console import Console
from pathlib import Path

import integrationtest.dro_map_gen as dro_map_gen
import daqconf.detreadoutmap as dromap
from daqconf.core.config_file import generate_cli_from_schema
from daqconf.core.sourceid import SourceIDBroker
from daqconf.core.system import System
from daqconf.core.fragment_producers import  connect_all_fragment_producers, set_mlt_links#, remove_mlt_link
from daqconf.core.conf_utils import make_app_command_data, make_system_command_datas, write_json_files
from daqconf.core.metadata import write_metadata_file, write_config_file

def expand_conf(config_data, debug=False):
    """Expands the moo configuration record into sub-records,
    re-casting its members into the corresponding moo objects.

    Args:
        config_data (_type_): Configuration object
        debug (bool, optional): Enable verbose reports. Defaults to False.

    Returns:
        _type_: _description_
    """

    import dunedaq.fddaqconf.confgen as confgen
    import dunedaq.daqconf.bootgen as bootgen
    import dunedaq.daqconf.detectorgen as detectorgen
    import dunedaq.daqconf.daqcommongen as daqcommongen
    #import dunedaq.daqconf.timinggen as timinggen
    #import dunedaq.daqconf.hsigen as hsigen
    import dunedaq.fddaqconf.readoutgen as readoutgen
    import dunedaq.daqconf.triggergen as triggergen
    import dunedaq.daqconf.dataflowgen as dataflowgen

    ## Hack, we shouldn't need to do that, in the future it should be, boot = config_data.boot
    boot = bootgen.boot(**config_data.boot)
    if debug: console.log(f"boot configuration object: {boot.pod()}")

    detector = detectorgen.detector(**config_data.detector)
    if debug: console.log(f"detector configuration object: {detector.pod()}")

    daq_common = daqcommongen.daq_common(**config_data.daq_common)
    if debug: console.log(f"daq_common configuration object: {daq_common.pod()}")

    #timing = timinggen.timing(**config_data.timing)
    #if debug: console.log(f"timing configuration object: {timing.pod()}")

    #hsi = hsigen.hsi(**config_data.hsi)
    #if debug: console.log(f"hsi configuration object: {hsi.pod()}")

    #ctb_hsi = confgen.ctb_hsi(**config_data.ctb_hsi)
    #if debug: console.log(f"ctb_hsi configuration object: {ctb_hsi.pod()}")

    readout = readoutgen.readout(**config_data.readout)
    if debug: console.log(f"readout configuration object: {readout.pod()}")

    trigger = triggergen.trigger(**config_data.trigger)
    if debug: console.log(f"trigger configuration object: {trigger.pod()}")

    dataflow = dataflowgen.dataflow(**config_data.dataflow)
    if debug: console.log(f"dataflow configuration object: {dataflow.pod()}")

    return (
        boot,
        detector,
        daq_common,
        #timing,
        #hsi,
        #ctb_hsi,
        readout,
        trigger,
        dataflow
    )

def validate_conf(boot, readout, dataflow):
    """Validate the consistency of confgen parameters

    Args:
        boot (_type_): _description_
        readout (_type_): _description_
        dataflow (_type_): _description_

    Raises:
        Exception: _description_
        Exception: _description_
        Exception: _description_
    """
    if readout.enable_tpg and readout.use_fake_data_producers:
        raise Exception("Fake data producers don't support software tpg")

    if dataflow.enable_tpset_writing and not readout.enable_tpg:
        raise Exception("TP writing can only be used when either software or firmware TPG is enabled")

    if boot.process_manager == 'k8s' and not boot.k8s_image:
        raise Exception("You need to define k8s_image if running with k8s")

    return

def create_df_apps(
        dataflow,
        sourceid_broker
    ):

    import dunedaq.daqconf.dataflowgen as dataflowgen

    if len(dataflow.apps) == 0:
        console.log(f"No Dataflow apps defined, adding default dataflow0")
        dataflow.apps = [dataflowgen.dataflowapp()]

    host_df = []
    appconfig_df = {}
    df_app_names = []
    for d in dataflow.apps:
        console.log(f"Parsing dataflow app config {d}")

        ## Hack, we shouldn't need to do that, in the future, it should be appconfig = d
        appconfig = dataflowgen.dataflowapp(**d)

        dfapp = appconfig.app_name
        if dfapp in df_app_names:
            appconfig_df[dfapp].update(appconfig)
        else:
            df_app_names.append(dfapp)
            appconfig_df[dfapp] = appconfig
            appconfig_df[dfapp].source_id = sourceid_broker.get_next_source_id("TRBuilder")
            sourceid_broker.register_source_id("TRBuilder", appconfig_df[dfapp].source_id, None)
            host_df += [appconfig.host_df]
    return host_df, appconfig_df, df_app_names

def detector_readout_map(readout, sourceid_broker, map_file, number_of_links, debug):
    if map_file == None:
        print("Generating DRO map file as none provided")
        dro_map_contents = dro_map_gen.generate_dromap_contents(n_streams=1, n_apps=number_of_links, det_id = 3) # default HD_TPC
        ### This is a weird hack, don't blame me!
        temp_map_file = "temp_map.json"
        with open(temp_map_file, 'w', encoding='utf8') as f:
            f.write(dro_map_contents)
        map_file = temp_map_file
    
    dro_map_file = map_file
    dro_map = dromap.DetReadoutMapService()
    dro_map.load(dro_map_file)
    if debug: print("map:", dro_map) 

    ru_descs = dro_map.get_ru_descriptors()
    readout.enable_tpg = True

    if debug:
        print(ru_descs)
        print(dro_map.streams)

    sourceid_broker.register_readout_source_ids(dro_map.streams)
    sourceid_broker.generate_trigger_source_ids(ru_descs, readout.enable_tpg)
    tp_infos = sourceid_broker.get_all_source_ids("Trigger")

    number_of_rus = 0
    number_of_ru_streams = 0
    for ru_name, ru_desc in ru_descs.items():
        console.log(f"Will generate a RU process on {ru_name} ({ru_desc.iface}, {ru_desc.kind}), {len(ru_desc.streams)} streams active")
        number_of_rus += 1
        number_of_ru_streams += len(ru_desc.streams)

    return tp_infos, number_of_ru_streams, ru_descs, dro_map, number_of_rus, map_file

def replay_app(the_system, input_file, slowdown_factor, number_of_loops, tpset_time_offset, tpset_time_width, maximum_wait_time_us, number_of_rus):
    from .replay_tp_app import get_replay_app
    the_system.apps["replay"] = get_replay_app(
        INPUT_FILES = input_file,
        SLOWDOWN_FACTOR = slowdown_factor,
        NUMBER_OF_LOOPS = number_of_loops,
        N_STREAMS = number_of_rus,
        TIME_OFFSET = tpset_time_offset, 
        TIME_WIDTH = tpset_time_width,
        WAIT_TIME = maximum_wait_time_us
    )

    return

def trigger_app(the_system, daq_common, get_trigger_app, trigger, detector, tp_infos, debug):
    trigger_data_request_timeout = daq_common.data_request_timeout_ms
    the_system.apps['trigger'] = get_trigger_app(
        trigger=trigger,
        detector=detector,
        daq_common=daq_common,
        tp_infos=tp_infos,
        trigger_data_request_timeout=trigger_data_request_timeout,
        use_hsi_input=False,
        DEBUG=debug)

    return

def dfo_apps(the_system, get_dfo_app, appconfig_df, daq_common, ru_descs, number_of_ru_streams, readout, dataflow, debug):
    max_expected_tr_sequences = 1
    for df_config in appconfig_df.values():
        if df_config.max_trigger_record_window >= 1:
            df_max_sequences = ((trigger.trigger_window_before_ticks + trigger.trigger_window_after_ticks) / df_config.max_trigger_record_window)
            if df_max_sequences > max_expected_tr_sequences:
                max_expected_tr_sequences = df_max_sequences

    MINIMUM_BASIC_TRB_TIMEOUT = 200  # msec
    TRB_TIMEOUT_SAFETY_FACTOR = 2
    DFO_TIMEOUT_SAFETY_FACTOR = 2
    MINIMUM_DFO_TIMEOUT = 10000
    trigger_data_request_timeout = daq_common.data_request_timeout_ms
    readout_data_request_timeout = daq_common.data_request_timeout_ms
    trigger_record_building_timeout = max(MINIMUM_BASIC_TRB_TIMEOUT, TRB_TIMEOUT_SAFETY_FACTOR * max(readout_data_request_timeout, trigger_data_request_timeout))
    if len(ru_descs) >= 1:
        effective_number_of_data_producers = number_of_ru_streams
        if readout.enable_tpg:
            effective_number_of_data_producers *= len(ru_descs)  # add in TPSet producers from Trigger (one per RU)
            effective_number_of_data_producers += len(ru_descs)  # add in TA producers from Trigger (one per RU)
            trigger_record_building_timeout = int(math.sqrt(effective_number_of_data_producers) * trigger_record_building_timeout)
    trigger_record_building_timeout += 15 * TRB_TIMEOUT_SAFETY_FACTOR * max_expected_tr_sequences
    dfo_stop_timeout = max(DFO_TIMEOUT_SAFETY_FACTOR * trigger_record_building_timeout, MINIMUM_DFO_TIMEOUT)

    the_system.apps['dfo'] = get_dfo_app(
        FREE_COUNT = max(1, dataflow.token_count / 2),
        BUSY_COUNT = dataflow.token_count,
        DF_CONF = appconfig_df,
        STOP_TIMEOUT = dfo_stop_timeout,
        HOST=dataflow.host_dfo,
        DEBUG=debug)

    return max_expected_tr_sequences, trigger_record_building_timeout

def dataflow_apps(the_system, get_dataflow_app, appconfig_df, dataflow, detector, max_expected_tr_sequences, trigger_record_building_timeout, dro_map, debug):
    file_label = None
    idx = 0
    for app_name,df_config in appconfig_df.items():
        dfidx = df_config.source_id
        the_system.apps[app_name] = get_dataflow_app(
            df_config = df_config,
            dataflow = dataflow,
            detector = detector,
            HOSTIDX=dfidx,
            APP_NAME=app_name,
            FILE_LABEL = file_label,
            MAX_EXPECTED_TR_SEQUENCES = max_expected_tr_sequences,
            TRB_TIMEOUT = trigger_record_building_timeout,
            SRC_GEO_ID_MAP=dro_map.get_src_geo_map(),
            DEBUG=debug
        )

        idx += 1

    return

def def_boot_order(the_system, df_app_names, debug):
    ru_app_names=[]
    all_apps_except_ru = []
    all_apps_except_ru_and_df = []

    for name,app in the_system.apps.items():
        if app.name=="__app":
            app.name=name

        if app.name not in ru_app_names:
            all_apps_except_ru += [app]
        if app.name not in ru_app_names+df_app_names:
            all_apps_except_ru_and_df += [name]

        # HACK
        boot_order = ru_app_names + df_app_names + [app for app in all_apps_except_ru_and_df]
        if debug:
            console.log(f'Boot order: {boot_order}')

    return

def write_final_files(app_command_datas, system_command_datas, json_dir, map_file, output_dir, config_file, boot, detector, daq_common, dataflow, trigger, debug):
    write_json_files(app_command_datas, system_command_datas, output_dir, verbose=debug)
    console.log(f"MDAapp config generated in {output_dir}")

    write_metadata_file(json_dir, "replay_tps", "./daqconf.ini")
  
    import shutil
    import dunedaq.fddaqconf.confgen as confgen

    write_config_file(
            output_dir,
            config_file.name if config_file else "default.json",
            confgen.fddaqconf_gen(  # :facepalm:
                boot = boot,
                detector = detector,
                daq_common = daq_common,
                dataflow = dataflow,
                trigger = trigger,
            ) # </facepalm>
        )

    shutil.copyfile(map_file, output_dir/'dromap.json')

    return

def export(the_system, debug_dir):
    the_system.export(debug_dir / "system.dot")
    for name in the_system.apps:
        the_system.apps[name].export(debug_dir / f"{name}.dot")

    return

def mlt_links(the_system, tp_infos, debug):
    set_mlt_links(the_system, tp_infos, "trigger", verbose=debug)

    mlt_mandatory_links=the_system.apps["trigger"].modulegraph.get_module("mlt").conf.mandatory_links
    mlt_groups_links=the_system.apps["trigger"].modulegraph.get_module("mlt").conf.groups_links
    if debug:
        console.log(f"After set_mlt_links, mlt_mandatory_links are {mlt_mandatory_links}")
        console.log(f"Groups links are {mlt_groups_links}")

    return

def print_cli_config(config, slowdown_factor, number_of_loops, tpset_time_offset, tpset_time_width, maximum_wait_time_us, input_file, map_file, number_of_links, debug, json_dir):
    print("CONFIGURATION")
    print("Config:", config)
    print("slowdown-factor:", slowdown_factor)
    print("number-of-loops:", number_of_loops)
    print("tpset-time-offset:", tpset_time_offset)
    print("tpset-time-width:", tpset_time_width)
    print("maximum-wait-time-us:", maximum_wait_time_us)
    print("input-file:", input_file)
    print("map-file:", map_file)
    print("number-of-links:", number_of_links)
    print("debug:", debug)
    print("json_dir:", json_dir)
    return

def check_file_extension(input_file):
    if input_file.lower().endswith(".hdf5"):
        return
    else: 
        print("Invalid file provided. HDF5 tpstream file required.")
        raise SystemExit(-1)

# Add -h as default help option
CONTEXT_SETTINGS = dict(help_option_names=['-h', '--help'])
@click.command(context_settings=CONTEXT_SETTINGS)
@generate_cli_from_schema('fddaqconf/confgen.jsonnet', 'fddaqconf_gen', 'daqconf.dataflowgen.dataflowapp')
@click.option('-s', '--slowdown-factor', default=1.0)
@click.option('-l', '--number-of-loops', default='-1', help="Number of times to loop over the input files (-1 for infinite)")
@click.option('-to', '--tpset-time-offset', default=0)
@click.option('-tw', '--tpset-time-width', default=10000)
@click.option('-wt', '--maximum-wait-time-us', default=1000)
@click.option('-f', '--input-file', type=click.Path(exists=True, dir_okay=False), multiple=True, required=True, help="TPStream HDF5 file")
@click.option('-map', '--map-file', type=click.Path(exists=True, dir_okay=False), help="DRO map file")
@click.option('-n', '--number-of-links', default=1, help="If DRO map not provided, will generate one for this number of links")
@click.option('--debug', default=False, is_flag=True, help="Switch to get a lot of printout and dot files")
@click.argument('json_dir', type=click.Path())
def cli(
    config,
    slowdown_factor,
    number_of_loops,
    tpset_time_offset,
    tpset_time_width,
    maximum_wait_time_us,
    input_file,
    map_file,
    number_of_links,
    debug,
    json_dir
    ):
    """
      JSON_DIR: Json file output folder
    """

    #--------------------------------------------------------------------------
    # Check output directory does not exist
    #--------------------------------------------------------------------------
    output_dir = Path(json_dir)
    if output_dir.exists():
        raise RuntimeError(f"Directory {output_dir} already exists")

    #--------------------------------------------------------------------------
    # Prepare debug
    #--------------------------------------------------------------------------
    if debug:
        output_dir = Path(json_dir)
        debug_dir = output_dir / 'debug'
        debug_dir.mkdir(parents=True)
        print_cli_config(config, slowdown_factor, number_of_loops, tpset_time_offset, tpset_time_width, maximum_wait_time_us, input_file, map_file, number_of_links, debug, json_dir)

    #--------------------------------------------------------------------------
    # Prepare config
    #--------------------------------------------------------------------------
    config_data = config[0]
    config_file = Path(config[1] if config[1] is not None else "fddaqconf_default.json")

    if debug:
        console.log(f"Configuration for fddaqconf: {config_data.pod()}")

    #--------------------------------------------------------------------------
    # Expand config (fill in defaults)
    #--------------------------------------------------------------------------
    (
        boot,
        detector,
        daq_common,
        #timing,
        #hsi,
        #ctb_hsi,
        readout,
        trigger,
        dataflow
    ) = expand_conf(config_data, debug)

    if map_file is not None:
        readout.detector_readout_map_file = map_file
        console.log(f"readout.detector_readout_map_file set to {readout.detector_readout_map_file}")

    #--------------------------------------------------------------------------
    # Validate configuration
    #--------------------------------------------------------------------------
    validate_conf(boot, readout, dataflow)

    #--------------------------------------------------------------------------
    # Check input files
    #--------------------------------------------------------------------------
    for each_file in input_file:
        check_file_extension(each_file)

    console.log("Loading dataflow config generator")
    from daqconf.apps.dataflow_gen import get_dataflow_app
    console.log("Loading readout config generator")
    from fddaqconf.apps.readout_gen import FDReadoutAppGenerator
    console.log("Loading trigger config generator")
    from daqconf.apps.trigger_gen import get_trigger_app
    console.log("Loading DFO config generator")
    from daqconf.apps.dfo_gen import get_dfo_app
    #console.log("Loading fake hsi config generator")
    #from daqconf.apps.fake_hsi_gen import get_fake_hsi_app

    #--------------------------------------------------------------------------
    # Create dataflow applications
    #--------------------------------------------------------------------------
    sourceid_broker = SourceIDBroker()
    sourceid_broker.debug = debug
    host_df, appconfig_df, df_app_names = create_df_apps(dataflow=dataflow, sourceid_broker=sourceid_broker)

    #--------------------------------------------------------------------------
    # Generation starts here
    #--------------------------------------------------------------------------
    console.log(f"Generating configs for hosts trigger={trigger.host_trigger} DFO={dataflow.host_dfo} dataflow={host_df}")
    the_system = System()

    #--------------------------------------------------------------------------
    # Load Detector Readout map
    #--------------------------------------------------------------------------
    tp_infos, number_of_ru_streams, ru_descs, dro_map, number_of_rus, map_file = detector_readout_map(readout, sourceid_broker, map_file, number_of_links, debug)

    #--------------------------------------------------------------------------
    # Replay
    #--------------------------------------------------------------------------
    replay_app(the_system, input_file, slowdown_factor, number_of_loops, tpset_time_offset, tpset_time_width, maximum_wait_time_us, number_of_rus)

    #--------------------------------------------------------------------------
    # Trigger
    #--------------------------------------------------------------------------
    trigger_app(the_system, daq_common, get_trigger_app, trigger, detector, tp_infos, debug)

    #--------------------------------------------------------------------------
    # DFO
    #--------------------------------------------------------------------------
    max_expected_tr_sequences, trigger_record_building_timeout = dfo_apps(the_system, get_dfo_app, appconfig_df, daq_common, ru_descs, number_of_ru_streams, readout, dataflow, debug)

    #--------------------------------------------------------------------------
    # Dataflow applications generation
    #--------------------------------------------------------------------------
    dataflow_apps(the_system, get_dataflow_app, appconfig_df, dataflow, detector, max_expected_tr_sequences, trigger_record_building_timeout, dro_map, debug)

    #--------------------------------------------------------------------------
    # App generation completed
    #--------------------------------------------------------------------------
    def_boot_order(the_system, df_app_names, debug)

    if debug:
        the_system.export(debug_dir / "system_no_frag_prod_connection.dot")

    connect_all_fragment_producers(the_system, verbose=debug)
    mlt_links(the_system, tp_infos, debug)
    if debug: export(the_system, debug_dir)

    ####################################################################
    # Application command data generation
    ####################################################################
    use_k8s = False

    # Arrange per-app command data into the format used by util.write_json_files()
    app_command_datas = {
        name : make_app_command_data(the_system, app,name, verbose=debug, use_k8s=use_k8s, use_connectivity_service=boot.use_connectivity_service, connectivity_service_interval=boot.connectivity_service_interval)
        for name,app in the_system.apps.items()
    }

    system_command_datas = make_system_command_datas(boot, the_system)

    ####################################################################
    # Write / Store final config, (meta)data, map
    ####################################################################
    write_final_files(app_command_datas, system_command_datas, json_dir, map_file, output_dir, config_file, boot, detector, daq_common, dataflow, trigger, debug)

if __name__ == '__main__':
    print( "##############################" )
    print( "### NEW TRIGGER REPLAY APP ###" )
    print( "##############################" )
    console = Console()

    try:
        cli(show_default=True, standalone_mode=True)
    except Exception as e:
        console.print_exception()
        raise SystemExit(-1)
