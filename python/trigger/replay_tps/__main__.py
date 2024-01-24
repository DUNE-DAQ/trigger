#!/usr/bin/env python3
import click
from rich.console import Console
from daqconf.core.config_file import generate_cli_from_schema
import os.path
import math
from pathlib import Path
from daqconf.core.sourceid import SourceIDBroker
from daqconf.core.system import System
import integrationtest.dro_map_gen as dro_map_gen
import daqconf.detreadoutmap as dromap

debug = False

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
    import dunedaq.daqconf.timinggen as timinggen
    import dunedaq.daqconf.hsigen as hsigen
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

    timing = timinggen.timing(**config_data.timing)
    if debug: console.log(f"timing configuration object: {timing.pod()}")

    hsi = hsigen.hsi(**config_data.hsi)
    if debug: console.log(f"hsi configuration object: {hsi.pod()}")

    ctb_hsi = confgen.ctb_hsi(**config_data.ctb_hsi)
    if debug: console.log(f"ctb_hsi configuration object: {ctb_hsi.pod()}")

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
        timing,
        hsi,
        ctb_hsi,
        readout,
        trigger,
        dataflow
    )

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

# Add -h as default help option
CONTEXT_SETTINGS = dict(help_option_names=['-h', '--help'])
@click.command(context_settings=CONTEXT_SETTINGS)
@generate_cli_from_schema('fddaqconf/confgen.jsonnet', 'fddaqconf_gen', 'daqconf.dataflowgen.dataflowapp')
@click.argument('json_dir', type=click.Path())
def cli(
    config,
    json_dir
    ):
    
    config_data = config[0]
    config_file = Path(config[1] if config[1] is not None else "fddaqconf_default.json")

    (
        boot,
        detector,
        daq_common,
        timing,
        hsi,
        ctb_hsi,
        readout,
        trigger,
        dataflow
    ) = expand_conf(config_data, debug)
  
    print(config_data)

    console.log("Loading dataflow config generator")
    from daqconf.apps.dataflow_gen import get_dataflow_app
    console.log("Loading readout config generator")
    from fddaqconf.apps.readout_gen import FDReadoutAppGenerator
    console.log("Loading trigger config generator")
    from daqconf.apps.trigger_gen import get_trigger_app
    console.log("Loading DFO config generator")
    from daqconf.apps.dfo_gen import get_dfo_app
    #console.log("Loading timing hsi config generator")
    #from daqconf.apps.hsi_gen import get_timing_hsi_app
    #console.log("Loading fake hsi config generator")
    #from daqconf.apps.fake_hsi_gen import get_fake_hsi_app
    #console.log("Loading ctb config generator")
    #from ctbmodules.apps.ctb_hsi_gen import get_ctb_hsi_app
    #console.log("Loading timing partition controller config generator")
    #from daqconf.apps.tprtc_gen import get_tprtc_app

    sourceid_broker = SourceIDBroker()

    #--------------------------------------------------------------------------
    # Create dataflow applications
    #--------------------------------------------------------------------------
    host_df, appconfig_df, df_app_names = create_df_apps(dataflow=dataflow, sourceid_broker=sourceid_broker)

    #--------------------------------------------------------------------------
    # Generation starts here
    #--------------------------------------------------------------------------
    console.log(f"Generating configs for hosts trigger={trigger.host_trigger} DFO={dataflow.host_dfo} dataflow={host_df} timing_hsi={hsi.host_timing_hsi} fake_hsi={hsi.host_fake_hsi} ctb_hsi={ctb_hsi.host_ctb_hsi}")

    the_system = System()

    #--------------------------------------------------------------------------
    # Load Detector Readout map
    #--------------------------------------------------------------------------
    number_of_data_producers=2
    dro_map_contents = dro_map_gen.generate_dromap_contents(number_of_data_producers)
    #dro_map_contents = readout.pop("dro_map", dro_map_contents)
    dro_map_file = "map.json"

    dro_map = dromap.DetReadoutMapService()
    if dro_map_file:
        dro_map.load(dro_map_file)

    ru_descs = dro_map.get_ru_descriptors()

    print(ru_descs)
    print(dro_map.streams)
    print(readout.enable_tpg)
    readout.enable_tpg = True
    print(readout.enable_tpg)
    # tp_mode = get_tpg_mode(readout.enable_firmware_tpg,readout.enable_tpg)
    sourceid_broker.register_readout_source_ids(dro_map.streams)
    sourceid_broker.generate_trigger_source_ids(ru_descs, readout.enable_tpg)
    tp_infos = sourceid_broker.get_all_source_ids("Trigger")

    number_of_ru_streams = 0
    for ru_name, ru_desc in ru_descs.items():
        console.log(f"Will generate a RU process on {ru_name} ({ru_desc.iface}, {ru_desc.kind}), {len(ru_desc.streams)} streams active")
        number_of_ru_streams += len(ru_desc.streams)

    #--------------------------------------------------------------------------
    # Replay
    #--------------------------------------------------------------------------
    input_file = "someinput.file"
    from .replay_tp_app import get_replay_app
    the_system.apps["replay"] = get_replay_app(
        INPUT_FILES = input_file,
        SLOWDOWN_FACTOR = 1,
        NUMBER_OF_LOOPS = 1
    )

    #--------------------------------------------------------------------------
    # Trigger
    #--------------------------------------------------------------------------
    trigger_data_request_timeout = daq_common.data_request_timeout_ms
    the_system.apps['trigger'] = get_trigger_app(
        trigger=trigger,
        detector=detector,
        daq_common=daq_common,
        tp_infos=tp_infos,
        trigger_data_request_timeout=trigger_data_request_timeout,
        use_hsi_input=False,
        DEBUG=debug)

    #--------------------------------------------------------------------------
    # DFO
    #--------------------------------------------------------------------------
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

    #--------------------------------------------------------------------------
    # Dataflow applications generatioo
    #--------------------------------------------------------------------------
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

    #--------------------------------------------------------------------------
    # App generation completed
    #--------------------------------------------------------------------------
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

    #     console.log(f"MDAapp config generated in {json_dir}")
    from daqconf.core.conf_utils import make_app_command_data
    from daqconf.core.fragment_producers import  connect_all_fragment_producers, set_mlt_links#, remove_mlt_link

    if debug:
        the_system.export(debug_dir / "system_no_frag_prod_connection.dot")

    connect_all_fragment_producers(the_system, verbose=debug)
    set_mlt_links(the_system, tp_infos, "trigger", verbose=debug)

    mlt_mandatory_links=the_system.apps["trigger"].modulegraph.get_module("mlt").conf.mandatory_links
    mlt_groups_links=the_system.apps["trigger"].modulegraph.get_module("mlt").conf.groups_links
    if debug:
        console.log(f"After set_mlt_links, mlt_mandatory_links are {mlt_mandatory_links}")
        console.log(f"Groups links are {mlt_groups_links}")

    ####################################################################
    # Application command data generation
    ####################################################################
    use_k8s = False

    from daqconf.core.conf_utils import make_app_command_data, make_system_command_datas, write_json_files
    from daqconf.core.metadata import write_metadata_file

    # Arrange per-app command data into the format used by util.write_json_files()
    app_command_datas = {
        name : make_app_command_data(the_system, app,name, verbose=debug, use_k8s=use_k8s, use_connectivity_service=boot.use_connectivity_service, connectivity_service_interval=boot.connectivity_service_interval)
        for name,app in the_system.apps.items()
    }

    system_command_datas = make_system_command_datas(boot, the_system)

    write_json_files(app_command_datas, system_command_datas, json_dir)

    write_metadata_file(json_dir, "replay_tps", "./daqconf.ini")

if __name__ == '__main__':
    print( "NEW REPLAY" )
    console = Console()

    try:
        cli(show_default=True, standalone_mode=True)
    except Exception as e:
        console.print_exception()
        raise SystemExit(-1)
