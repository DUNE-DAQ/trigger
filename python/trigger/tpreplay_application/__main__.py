import argparse
import copy
import gc
import logging
import re
import resource
import sys
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from tqdm import tqdm
from typing import Any

import conffwk
import daqdataformats
import detchannelmaps
import detdataformats
import trgdataformats
from daqconf.consolidate import copy_configuration
from hdf5libs import HDF5RawDataFile


def setup_logging(verbose: bool) -> None:
    """
    Set up logging based on the verbose flag.
    If verbose flag is provided, set the logging level to DEBUG to show all logs.
    Otherwise, set it to INFO to only show INFO level logs and above.
    """
    if verbose:
        logging.basicConfig(level=logging.DEBUG, format="%(asctime)s - %(levelname)s - %(message)s")
        logging.debug("Verbose logging configured!")
    else:
        logging.basicConfig(level=logging.INFO, format="%(asctime)s - %(levelname)s - %(message)s")

def set_mem_limit(mem_limit: int) -> None:
    """
    As a safety measure we set a memory limit for the process.
    Should not be needed here as hdf5 processing is minimal.
    """
    GB = 1024**3
    memory_limit = mem_limit * GB 
    resource.setrlimit(resource.RLIMIT_AS, (memory_limit, memory_limit))
    logging.debug("Setting memory limit to %i GBs", memory_limit/GB)

def cleanup() -> None:
    gc.collect()

# custom type to hold a map of all (unique) ReadoutUnits and the corresponding planes for each
@dataclass
class ROUPlaneData:
    data: dict[str, set[int]] = field(default_factory=lambda: defaultdict(set))

    def add_value(self, rou: str, plane: int) -> None:
        self.data[rou].add(plane)

    def get_values(self, rou: str) -> set[int]:
        return self.data.get(rou, set())

    def total_plane_count(self) -> int:
        return sum(len(planes) for planes in self.data.values())

# custom type to hold variables for TPStream, used later to sort & update db
@dataclass
class TPStreamFile:
    filename: str
    stime: int
    index: int

    def __str__(self) -> str:
        return f"Name: {self.filename}, Stime: {self.stime}, Index: {self.index}"

def setup_configuration(path_str: str, sessions_file: str, verbose: bool) -> conffwk.Configuration:
    """
    Copies over relevant configuration files for a provided .data.xml file.
    The default is the example-configs.data.xml, which contains the default sessions.
    """
    logging.info("Setting up configuration files")
    logging.debug("Local path for configurations: %s", path_str)
    path = Path(path_str).resolve()  # Convert string to Path object
    path.mkdir(parents=True, exist_ok=True)
    external_logger = logging.getLogger('daqconf.consolidate')
    if not verbose: external_logger.setLevel(logging.WARNING)
    copy_configuration(path, [sessions_file])
    cleanup()

    logging.debug("Copying configuration represented by databases: %s", [sessions_file])

    return conffwk.Configuration(f"oksconflibs:{path}/example-configs.data.xml")

def get_tpreplay_app(cfg: conffwk.Configuration) -> Any:
    """
    Retrieves the instance of TPReplayApplication from configuration files.
    If it does not exist, stops the script.
    In theory, we could make one from scratch, however, there are so many objects to configure
    that doing that externally is preferred.
    """
    tpreplay_apps = cfg.get_dals("TPReplayApplication")
    if tpreplay_apps:
        tpreplay_app = tpreplay_apps[0]
        logging.debug("Loaded tpreplay application")
        return tpreplay_app
    else:
        logging.error("No 'TPReplayApplication' DAL objects found.")
        sys.exit(1)

def load_channel_map(channel_map_string: str) -> detchannelmaps._daq_detchannelmaps_py.TPCChannelMap:
    """
    Tries to create a channel map using the provided string name.
    If it fails, prints the error and exits.
    """
    try: 
        channel_map = detchannelmaps.make_map(channel_map_string)
        logging.debug(f"Channel map '{channel_map_string}' successfully created.")
        return channel_map
    except Exception as e:
        print(f"Failed to create the channel map '{channel_map_string}'. Error: {str(e)}")
        sys.exit(1)

def get_tpstream_files(filename: str) -> list[str]:
    """
    Reads in names of tpstream files from provided text file.
    """
    logging.info("Reading TPStream file list from %s", filename)
    with open(filename, "r") as file:
        tpstream_files = [line.strip() for line in file.readlines()]
    cleanup()

    logging.info("Total files to process: %i", len(tpstream_files))
    logging.debug("TPStream files loaded: %s", tpstream_files)

    return tpstream_files

def check_files(files: list[str]) -> None:
    """
    Very basic checks on the provided TPStream files.
    """
    for a_file in files:
        ### Checks
        # check file exist
        path = Path(a_file)
        if not path.exists():
            logging.error("File %s does not exist!", a_file)
            sys.exit(1)
        # check it's hdf5
        if not path.suffix.lower() in ('.h5', '.hdf5'):
            logging.error("File %s does not seem to be hdf5 file!", a_file)
            sys.exit(1)

def extract_rous_and_planes(files: list[str], channel_map: 'detchannelmaps._daq_detchannelmaps_py.TPCChannelMap', planes_to_filter: set[int]) -> (list[TPStreamFile], ROUPlaneData):
    """
    This function goes over the provided TPStream files.
    It extracts the readout units used to generate the data in the files.
    It also extracts unfiltered planes for each readout unit.
    """
    logging.info("Extracting ROUs and planes from files")
    all_tpstream_files = []
    rou_plane_data = ROUPlaneData()

    valid_subdetectors = {
        int(detdataformats.DetID.Subdetector.kHD_TPC),
        int(detdataformats.DetID.Subdetector.kVD_BottomTPC),
        int(detdataformats.DetID.Subdetector.kVD_TopTPC),
        int(detdataformats.DetID.Subdetector.kNDLAr_TPC)
    }

    for tpstream_file in files:
        logging.debug("Processing file: %s", tpstream_file)
        loaded_file = HDF5RawDataFile(tpstream_file)
        # check is tpstream
        if not loaded_file.is_timeslice_type:
            logging.error("File %s is not a TP Stream file!", tpstream_file)
            sys.exit(1)
        # check has records
        all_record_ids = loaded_file.get_all_record_ids()
        if not all_record_ids:
            logging.error("File %s does not have valid records!", tpstream_file)
            sys.exit(1)

        # loop over all records :/
        first_record = True
        for a_record in tqdm(all_record_ids, desc="Processing records"):

            # check has source IDs
            source_ids = loaded_file.get_source_ids_for_fragment_type(a_record, "Trigger_Primitive")
            if len(source_ids) == 0:
                logging.error("File %s does not have valid SourceIDs!", tpstream_file)
                sys.exit(1)
            logging.debug("SIDs: %s", source_ids)

            for i, sid in enumerate(source_ids):
                frag = loaded_file.get_frag(a_record, sid)
                # check frag has data
                if frag.get_data_size() < 1:
                    logging.error("File %s has an empty fragment!", tpstream_file)
                    sys.exit(1)
                tp = trgdataformats.TriggerPrimitive(frag.get_data(0))

                if first_record == True:
                    all_tpstream_files.append(TPStreamFile(tpstream_file, tp.time_start, 0))
                    logging.debug("First time start: %s", tp.time_start)
                    first_record = False

                # check subdetector
                subdet = tp.detid
                if subdet not in valid_subdetectors:
                    logging.debug("Subdetector %s is not in the map of valid subdetectors!", subdet.to_string)
                    continue

                plane = channel_map.get_plane_from_offline_channel(tp.channel)
                if plane not in planes_to_filter:
                    rou = channel_map.get_tpc_element_from_offline_channel(tp.channel)
                    rou_plane_data.add_value(rou, plane)
                    logging.debug("Extracted rou: %s for plane: %s", rou, plane)
                else:
                    logging.debug("Plane %s filtered", plane)
                cleanup()
                del frag, tp
        cleanup()
        del loaded_file, a_record, source_ids

    # No need for an if verbose check here, as logging level is already set
    logging.info("Extracted ROUs and planes: %s", rou_plane_data.data)

    return all_tpstream_files, rou_plane_data

def update_tpstream_indices(tpstream_files: list[TPStreamFile]) -> list[TPStreamFile]:
    """
    Sorts the files by the time of the very first TP.
    The data will be sorted in TPRM, but this speeds it up.
    """
    logging.info("Sorting TPStream files by time and assigning indices")
    sorted_files = sorted(tpstream_files, key=lambda x: x.stime)
    for i, tp_file in enumerate(sorted_files):
        tp_file.index = i + 1
    cleanup()
    logging.debug("Sorted TPSTreamFile objects: %s", sorted_files)
    return sorted_files

def update_tpstream_dal_objects(a_tp_stream: Any, cfg: conffwk.Configuration, sorted_tpstream_files: list[TPStreamFile]) -> list[Any]:
    """
    Creates TPStreamConf dal objects for the provided TP Stream files.
    Additional safety to create these from scratch if an example instance is not found.
    """
    if not a_tp_stream:
        logging.warning("No template TPStream object found")
        # get template and create from scratch
        a_tp_stream_template = cfg.create_obj('TPStreamConf', "template-TPStreamConf")
        cache = {"TPStreamConf": {}}
        a_tp_stream = a_tp_stream_template.as_dal(cache)
    tp_streams = []
    for a_file in sorted_tpstream_files:
        temp_tp_stream = copy.deepcopy(a_tp_stream)
        temp_tp_stream.id = f"def-tp-stream-{a_file.index}"
        temp_tp_stream.filename = a_file.filename
        temp_tp_stream.index = a_file.index
        tp_streams.append(temp_tp_stream)
        logging.debug("Created TPStream: %s", temp_tp_stream)
    return tp_streams

def update_planes_dal_objects(a_plane: Any, cfg: conffwk.Configuration, planes_to_filter: list[int]) -> list[Any]:
    """
    Creates PlaneNumberConf dal objects for the provided set of planes.
    Additional safety to create these from scratch if an example instance is not found.
    """
    if not a_plane:
        logging.warning("No template PlaneNumberConf object found")
        # get template and create from scratch
        a_plane_template = cfg.create_obj('PlaneNumberConf', "template-PlaneNumberConf")
        cache = {"PlaneNumberConf": {}}
        a_plane = a_plane_template.as_dal(cache)
    planes = []
    for i in range(0, len(planes_to_filter)):
        temp_plane = copy.deepcopy(a_plane)
        temp_plane.id = f"tpreplay-plane-filter-{i+1}"
        temp_plane.plane = planes_to_filter[i]
        planes.append(temp_plane)
        logging.debug("Created PlaneNumberConf: %s", temp_plane)
    return planes

def update_sid_dal_objects(a_sid: Any, cfg: conffwk.Configuration, total_unique_planes: int) -> list[Any]:
    """
    Creates SourceIDConf dal objects needed for each unique plane.
    Additional safety to create these from scratch if an example instance is not found.
    """
    if not a_sid:
        logging.warning("No template Source ID object found")
        # get template and create from scratch
        a_sid_template = cfg.create_obj('SourceIDConf', "template-SourceIDConf")
        cache = {"SourceIDConf": {}}
        a_sid = a_sid_template.as_dal(cache)
    all_sids = []
    base_string = "tpreplay-tp-srcid-100000"
    start_number = int(re.search(r'(\d+)$', base_string).group(1))
    for i in range(1, total_unique_planes + 1):
        temp_sid = copy.deepcopy(a_sid)
        temp_sid.id = re.sub(r'\d+$', f"{start_number + i:06d}", base_string)
        temp_sid.sid = i
        temp_sid.subsystem = "Trigger"
        all_sids.append(temp_sid)
        logging.debug("Created SID config: %s", temp_sid)
    return all_sids

def update_RandomTCmaker_obj(cfg: conffwk.Configuration) -> Any:
    """
    Changes the trigger_rate_hz for RandomTCMakerConf to 0 by default for replay.
    """
    randomTCmakers = cfg.get_dals("RandomTCMakerConf")
    if randomTCmakers:
        randomTCmaker = randomTCmakers[0]
        logging.debug("Loaded randomTCmaker object")
        randomTCmaker.trigger_rate_hz = 0
        logging.debug("Updated randomTCmaker object")
        return randomTCmaker

    else:
        logging.error("No 'RandomTCMakerConf' DAL objects found.")
        return None

def update_configuration(cfg: conffwk.Configuration, tpreplay_app: Any, tprm_conf: Any, sorted_tpstream_files: list[TPStreamFile], total_unique_planes: int, planes_to_filter: list[int], path_str: str) -> None:
    """
    Takes all changes and updates the local database files.
    [total_planes in TPRM
     tp_streams in TPRM
     planes in TPRM
     tp_source_ids in tpreplay
     ]
    """
    logging.info("Updating configuration with new TPStream data")
    tprm_conf.total_planes = total_unique_planes

    a_tp_stream = tprm_conf.tp_streams[0] if tprm_conf.tp_streams else None
    tprm_conf.tp_streams = update_tpstream_dal_objects(a_tp_stream, cfg, sorted_tpstream_files)
    logging.info("Total of %i TPStream configs created", len(tprm_conf.tp_streams))

    if planes_to_filter:
        a_plane = tprm_conf.filter_out_plane[0] if tprm_conf.filter_out_plane else None
        tprm_conf.filter_out_plane = update_planes_dal_objects(a_plane, cfg, list(planes_to_filter))
        logging.info("Total of %i PlaneNumberConf configs created", len(tprm_conf.filter_out_plane))
    else:
        tprm_conf.filter_out_plane = []

    a_sid = tpreplay_app.tp_source_ids[0] if tpreplay_app.tp_source_ids else None
    tpreplay_app.tp_source_ids = update_sid_dal_objects(a_sid, cfg, total_unique_planes)
    logging.info("Total of %i SID configs created", len(tpreplay_app.tp_source_ids))

    randomTCmaker = update_RandomTCmaker_obj(cfg)

    cleanup()
    logging.debug("Committing updated configuration to database")

    db_modules = conffwk.Configuration(f"oksconflibs:{path_str}/moduleconfs.data.xml")
    db_trigger = conffwk.Configuration(f"oksconflibs:{path_str}/trigger-segment.data.xml")
    for tpstream in tprm_conf.tp_streams:
        db_modules.update_dal(tpstream)
    for plane in tprm_conf.filter_out_plane or []:
        db_modules.update_dal(plane)
    for sid in tpreplay_app.tp_source_ids:
        db_trigger.update_dal(sid)
    db_trigger.update_dal(tpreplay_app)
    db_modules.update_dal(tprm_conf)
    if randomTCmaker is not None: 
        db_modules.update_dal(randomTCmaker)
    db_modules.commit()
    db_trigger.commit()
    logging.info("Local database updated!")
    cleanup()

def main():
    parser = argparse.ArgumentParser(description="To be used with TP Replay Application. Process TPStream data and update configuration.",
            formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument("--files", type=str, required=True, help="Text file with (full) paths to HDF5 TPStream file location. One per line.")
    parser.add_argument("--filter-planes", type=int, nargs='*', choices=[0, 1, 2], default=[], help="list of planes to filter out. Can be empty or contain any combination of 0 (U), 1 (V), and 2 (X).")
    parser.add_argument("--channel-map", type=str, default='PD2HDChannelMap', 
                        help="Specify the channel map to use. Available examples include: PD2HDChannelMap, PD2VDBottomTPCChannelMap, VDColdboxChannelMap, HDColdboxChannelMap.\n"
                             "For more details, visit: https://github.com/DUNE-DAQ/detchannelmaps/blob/develop/docs/channel-maps-table.md")
    parser.add_argument("--config", type=str, default="config/daqsystemtest/example-configs.data.xml", help="Path to OKS configuration file.")
    parser.add_argument("--path", type=str, default="tpreplay-run", help="Path for local output for configuration files.")
    parser.add_argument("--mem-limit", type=int, default=25, help="This will set a memory limit [GB] to protect the machine.")
    parser.add_argument("--verbose", action="store_true", help="Enable verbose logging.")
    args = parser.parse_args()

    # Set up logging based on the verbose flag
    setup_logging(args.verbose)

    # Set memory limit
    set_mem_limit(args.mem_limit) 

    logging.info("Starting TPStream processing script")
    cfg = setup_configuration(args.path, args.config, args.verbose)
    tpreplay_app = get_tpreplay_app(cfg)
    logging.debug("TP Replay application configuration: %s", tpreplay_app)
    tprm_conf = tpreplay_app.tprm_conf
    logging.debug("TPRM configuration: %s", tprm_conf)
    channel_map = load_channel_map(args.channel_map)
    planes_to_filter = set(args.filter_planes)
    logging.debug("Planes to filter: %s", planes_to_filter)

    files = get_tpstream_files(args.files)
    check_files(files)
    all_tpstream_files, rou_plane_data = extract_rous_and_planes(files, channel_map, planes_to_filter)
    sorted_tpstream_files = update_tpstream_indices(all_tpstream_files)

    total_unique_planes = rou_plane_data.total_plane_count()
    logging.info("Total plane count: %d", total_unique_planes)
    update_configuration(cfg, tpreplay_app, tprm_conf, sorted_tpstream_files, total_unique_planes, planes_to_filter, args.path)

if __name__ == "__main__":
    main()

