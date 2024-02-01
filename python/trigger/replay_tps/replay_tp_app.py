# Set moo schema search path
from dunedaq.env import get_moo_model_path
import moo.io
moo.io.default_load_path = get_moo_model_path()

from pprint import pprint
# Load configuration types
import moo.otypes

moo.otypes.load_types('trigger/triggerprimitivemaker.jsonnet')

# Import new types
import dunedaq.trigger.triggerprimitivemaker as tpm

from daqconf.core.app import App, ModuleGraph
from daqconf.core.daqmodule import DAQModule
from daqconf.core.conf_utils import Direction

def get_replay_app(INPUT_FILES: [str],
                   SLOWDOWN_FACTOR: float,
                   NUMBER_OF_LOOPS: int,
                   N_STREAMS: int):

    clock_frequency_hz = 62_500_000 / SLOWDOWN_FACTOR
    modules = []

    #n_streams = N_STREAMS
    n_streams = 1

    tp_streams = [tpm.TPStream(filename= input_file,
                               # region_id = 0,
                               element_id = istream,
                               output_sink_name = f"output{istream}")
                  for istream, input_file in enumerate(INPUT_FILES)]

    modules.append(
        DAQModule(
            name = "tpm",
            plugin = "TriggerPrimitiveMaker",
            conf = tpm.ConfParams(
                tp_streams = tp_streams,
                number_of_loops=NUMBER_OF_LOOPS,
                tpset_time_offset=0,
                tpset_time_width=10000,
                clock_frequency_hz=clock_frequency_hz,
                maximum_wait_time_us=1000,
            )
        )
    )

    mgraph = ModuleGraph(modules)
    for istream in range(n_streams):
        # mgraph.add_endpoint(f"tpsets_rulocalhost_{istream}_link0", f"tpm.output{istream}", Direction.OUT, topic=["TPSets"])
        mgraph.add_endpoint(
            external_name = f"tpsets_tplink{istream}",
            data_type = 'TPSet',
            internal_name = f"tpm.output{istream}",
            inout = Direction.OUT,
            is_pubsub = True
        )

    return App(modulegraph=mgraph, host="localhost", name="ReplayApp")
