import copy

import conffwk
import hdf5libs
import daqdataformats
import trgdataformats
import detchannelmaps
from pathlib import Path
import integrationtest.data_classes as data_classes
from daqconf.consolidate import copy_configuration
from hdf5libs import HDF5RawDataFile

sessions_file = "config/daqsystemtest/example-configs.data.xml"
path_str = "replay-run"
path_object = Path(path_str)
path_object.mkdir(parents=True, exist_ok=True)
local_object_databases = copy_configuration(path_object, [sessions_file])

config_file = path_str + "/example-configs.data.xml"
cfg = conffwk.Configuration(f"oksconflibs:{config_file}")

replay_session = cfg.get_dal('Session', "local-replay-config")
trg_segment    = trg_seg = cfg.get_dal('Segment', "trg-segment")
replay_app     = cfg.get_dal('Application', "replay")

print("##### SESSION #####")
print(replay_session)
print("####################")
print()
print("##### TRG-SEGMENT #####")
print(trg_segment)
print("####################")
print()
print("##### REPLAY APP #####")
print(replay_app)
print("####################")
print()

