# Set moo schema search path
from dunedaq.env import get_moo_model_path
import moo.io
moo.io.default_load_path = get_moo_model_path()

# Load configuration types
import moo.otypes

moo.otypes.load_types('trigger/triggerprimitivemaker.jsonnet')
moo.otypes.load_types('trigger/triggeractivitymaker.jsonnet')
moo.otypes.load_types('trigger/triggerzipper.jsonnet')
moo.otypes.load_types('trigger/faketpcreatorheartbeatmaker.jsonnet')
moo.otypes.load_types('trigger/tasetsink.jsonnet')

# Import new types
import dunedaq.trigger.triggerprimitivemaker as tpm
import dunedaq.trigger.triggeractivitymaker as tam
import dunedaq.trigger.triggerzipper as tzip
import dunedaq.trigger.faketpcreatorheartbeatmaker as ftpchm
import dunedaq.trigger.tasetsink as tasetsink

from daqconf.core.app import App, ModuleGraph
from daqconf.core.daqmodule import DAQModule
from daqconf.core.conf_utils import Direction

#FIXME maybe one day, triggeralgs will define schemas... for now allow a dictionary of 4byte int, 4byte floats, and strings
moo.otypes.make_type(schema='number', dtype='i4', name='temp_integer', path='temptypes')
moo.otypes.make_type(schema='number', dtype='f4', name='temp_float', path='temptypes')
moo.otypes.make_type(schema='string', name='temp_string', path='temptypes')
def make_moo_record(conf_dict,name,path='temptypes'):
    fields = []
    for pname,pvalue in conf_dict.items():
        typename = None
        if type(pvalue) == int:
            typename = 'temptypes.temp_integer'
        elif type(pvalue) == float:
            typename = 'temptypes.temp_float'
        elif type(pvalue) == str:
            typename = 'temptypes.temp_string'
        else:
            raise Exception(f'Invalid config argument type: {type(value)}')
        fields.append(dict(name=pname,item=typename))
    moo.otypes.make_type(schema='record', fields=fields, name=name, path=path)

#===============================================================================
def get_dbscan_ta_to_sink_app(
        INPUT_FILES: [str],
        OUTPUT_FILE: str,
        SLOWDOWN_FACTOR: float,
        NUMBER_OF_LOOPS: int,
        ACTIVITY_CONFIG: dict = dict(min_pts=3),
        DO_TASET_CHECKS: bool = True
):
    # Derived parameters
    CLOCK_FREQUENCY_HZ = 50_000_000 / SLOWDOWN_FACTOR
    TPSET_WIDTH = 10_000
    
    modules = []

    make_moo_record(ACTIVITY_CONFIG,'ActivityConf','temptypes')
    import temptypes

    n_streams = len(INPUT_FILES)

    tp_streams = [tpm.TPStream(filename=input_file,
                               region_id = 0,
                               element_id = istream,
                               output_sink_name = f"output{istream}")
                  for istream,input_file in enumerate(INPUT_FILES)]

    modules.append(DAQModule(name = "tpm",
                             plugin = "TriggerPrimitiveMaker",
                             conf = tpm.ConfParams(tp_streams = tp_streams,
                                                   number_of_loops=NUMBER_OF_LOOPS, # Infinite
                                                   tpset_time_offset=0,
                                                   tpset_time_width=TPSET_WIDTH,
                                                   clock_frequency_hz=CLOCK_FREQUENCY_HZ,
                                                   maximum_wait_time_us=1000,)))

    for istream in range(n_streams):
        modules.append(DAQModule(name = f"ftpchm{istream}",
                                 plugin = "FakeTPCreatorHeartbeatMaker",
                                 conf = ftpchm.Conf(heartbeat_interval = 50000)))

    modules.append(DAQModule(name = "zip",
                             plugin = "TPZipper",
                             conf = tzip.ConfParams(cardinality=n_streams,
                                                    max_latency_ms=10,
                                                    region_id=0,
                                                    element_id=0,)))
    
    modules.append(DAQModule(name = "tam",
                             plugin = "TriggerActivityMaker",
                             conf = tam.Conf(activity_maker="TriggerActivityMakerDBSCANPlugin",
                                             geoid_region=0, # Fake placeholder
                                             geoid_element=0, # Fake placeholder
                                             window_time=10000, # should match whatever makes TPSets, in principle
                                             buffer_time=6250000, # 10ms in 62.5 MHz ticks
                                             activity_maker_config=temptypes.ActivityConf(**ACTIVITY_CONFIG))))
    modules.append(DAQModule(name = "ta_sink",
                             plugin = "TASetSink",
                             conf = tasetsink.Conf(output_filename=OUTPUT_FILE,
                                                   do_checks = DO_TASET_CHECKS)))

    mgraph = ModuleGraph(modules)

    for istream in range(n_streams):
        mgraph.connect_modules(f"tpm.output{istream}", f"ftpchm{istream}.tpset_source")
        mgraph.connect_modules(f"ftpchm{istream}.tpset_sink", f"zip.input")
    mgraph.connect_modules("zip.output", "tam.input")
    mgraph.connect_modules("tam.output", "ta_sink.taset_source")
    
    return App(modulegraph = mgraph, host="localhost", name="TASinkApp")
