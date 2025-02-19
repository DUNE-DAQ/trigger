import copy
import re

import conffwk
import daqdataformats
import detchannelmaps
import trgdataformats

from daqconf.consolidate import copy_configuration
from hdf5libs import HDF5RawDataFile
from pathlib import Path

# make local copies of db files
sessions_file = "config/daqsystemtest/example-configs.data.xml"
path_str = "replay-run"
path_object = Path(path_str)
path_object.mkdir(parents=True, exist_ok=True)
local_object_databases = copy_configuration(path_object, [sessions_file])

# load db
config_file = path_str + "/example-configs.data.xml"
cfg = conffwk.Configuration(f"oksconflibs:{config_file}")

# get required objects
all_replay_apps     = cfg.get_dals("TriggerReplayApplication")
replay_app = all_replay_apps[0] # this may need improving if we'll have more apps

# printout
print("##### REPLAY APP #####")
print(replay_app)
print("####################")
print()

# get tpstream files from provided file
# Open the file and read lines into a list
with open("files2.txt", "r") as file:
    files = file.readlines()
files = [line.strip() for line in files]
# get full paths if not provided

print("##### TPStream files #####")
print(files)
print("####################")
print()
# should also do other checks here?
# there are many in c++

# get tpmm object
tppm_conf = replay_app.tpmm_conf
channel_map_name = tppm_conf.channel_map
channel_map = detchannelmaps.make_map(channel_map_name)
planes_to_filter_conf = tppm_conf.filter_out_plane
planes_to_filter = set()
for plane in planes_to_filter_conf:
    planes_to_filter.add( plane.plane )

print("##### Planes to filter #####")
print(planes_to_filter)
print("####################")
print()

conf_tpstreams = tppm_conf.tp_streams
tpstream_files = []
for a_file in conf_tpstreams:
    tpstream_files.append(a_file.filename)

# extract source IDs from files
source_ids_all = set()
for tpstream_file in files:
    loaded_file = HDF5RawDataFile(tpstream_file)
    first_record = loaded_file.get_all_record_ids()[0] # assumption that the first record contains all relevant frags (for all source ids)
    source_ids = loaded_file.get_source_ids_for_fragment_type( first_record, "Trigger_Primitive" )

    # figure out sourceID-to-plane for filtering
    for sid in source_ids:
        frag = loaded_file.get_frag(first_record, sid)
        tp = trgdataformats.TriggerPrimitive(frag.get_data(0))
        plane = channel_map.get_plane_from_offline_channel( tp.channel )
        # apply filtering
        if plane not in planes_to_filter:
            source_ids_all.add( sid.id )

# printout source ids
print("##### SourceIDs #####")
print("Total: ", len(source_ids_all))
for sid in source_ids_all:
    print(sid)
print("####################")
print()

### get objects needed from config and update
# tot planes
tppm_conf.total_planes = len(source_ids_all)
# files
a_tp_stream = tppm_conf.tp_streams[0] # get example tp stream (TODO: if does not exist, create new one from scratch)
tp_streams = []
for counter, a_file in enumerate(files):
    temp_tp_stream = copy.deepcopy(a_tp_stream)
    temp_tp_stream.id = "def-tp-stream-" + str(counter)
    temp_tp_stream.filename = a_file
    tp_streams.append(temp_tp_stream)
tppm_conf.tp_streams = tp_streams
# source ids
# should we assume the same source ID across different files is the same plane ?!
a_sid = replay_app.tp_source_ids[0] # get example sid obj (TODO: if does not exist, create new one from scratch)
all_sids = []
base_string = "replay-tp-srcid-100000"
match = re.search(r'(\d+)$', base_string)
start_number = int(match.group(1))
for i in range(1, len(source_ids_all)+1):
    temp_sid = copy.deepcopy(a_sid)
    new_number = start_number + i
    new_string = re.sub(r'\d+$', f"{new_number:06d}", base_string)
    temp_sid.id = new_string
    temp_sid.sid = i
    temp_sid.subsystem = "Trigger"
    all_sids.append(temp_sid)
replay_app.tp_source_ids = all_sids

# store this update to local files
db_modules = conffwk.Configuration("oksconflibs:" + path_str + "/moduleconfs.data.xml")
db_trigger = conffwk.Configuration("oksconflibs:" + path_str + "/trigger-segment.data.xml")
for tpstream in tppm_conf.tp_streams:
    db_modules.update_dal(tpstream) # create objects
for sid in replay_app.tp_source_ids:
    db_trigger.update_dal(sid)
db_trigger.update_dal(replay_app)    
db_modules.update_dal(tppm_conf)
db_modules.commit()
db_trigger.commit()

