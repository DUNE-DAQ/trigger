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
from typing import Dict, Set, List

import conffwk
import daqdataformats
import detchannelmaps
import trgdataformats
from daqconf.consolidate import copy_configuration
from hdf5libs import HDF5RawDataFile

def setup_logging(verbose: bool):
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

def set_mem_limit(verbose: bool):
    """
    As a safety measure we set a memory limit for the process.
    Should not be needed here as hdf5 processing is minimal.
    """
    GB = 1024**3
    memory_limit = 25 * GB 
    resource.setrlimit(resource.RLIMIT_AS, (memory_limit, memory_limit))
    logging.debug("Setting memory limit to %i GBs", memory_limit/GB)

def cleanup():
    gc.collect()

# custom type to hold a map of all (unique) ReadoutUnits and the corresponding planes for each
@dataclass
class ROUPlaneData:
    data: Dict[str, Set[int]] = field(default_factory=lambda: defaultdict(set))

    def add_value(self, rou: str, plane: int):
        self.data[rou].add(plane)

    def get_values(self, rou: str):
        return self.data.get(rou, set())

    def total_plane_count(self):
        return sum(len(planes) for planes in self.data.values())

# custom type to hold variables for TPStream, used later to sort & update db
@dataclass
class TPStreamFile:
    filename: str
    stime: int
    index: int

    def __str__(self):
        return f"Name: {self.filename}, Stime: {self.stime}, Index: {self.index}"

def setup_configuration(path_str: str, sessions_file: str, verbose: bool):
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

def get_replay_app(cfg):
    """
    Retrieves the instance of TriggerReplayApplication from configuration files.
    If it does not exist, stops the script.
    In theory, we could make one from scratch, however, there are so many objects to configure
    that doing that externally is preferred.
    """
    replay_apps = cfg.get_dals("TriggerReplayApplication")
    if replay_apps:
        replay_app = replay_apps[0]
        logging.debug("Loaded replay application")
        return replay_app
    else:
        logging.error("No 'TriggerReplayApplication' DAL objects found.")
        sys.exit(1)

def get_tpstream_files(filename: str, verbose: bool) -> List[str]:
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

def check_files(files):
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

def extract_rous_and_planes(files: List[str], channel_map, planes_to_filter: Set[int], verbose: bool) -> (List[TPStreamFile], ROUPlaneData):
    """
    This function goes over the provided TPStream files.
    It extracts the readout units used to generate the data in the files.
    It also extracts unfiltered planes for each readout unit.
    """
    logging.info("Extracting ROUs and planes from files")
    all_tpstream_files = []
    rou_plane_data = ROUPlaneData()

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
        first_record = all_record_ids[0]
        # check has source IDs
        source_ids = loaded_file.get_source_ids_for_fragment_type(first_record, "Trigger_Primitive")
        if len(source_ids) == 0:
            logging.error("File %s does not have valid SourceIDs!", tpstream_file)
            sys.exit(1)
        logging.debug("SIDs: %s", source_ids)

        for i, sid in enumerate(source_ids):
            frag = loaded_file.get_frag(first_record, sid)
            # check frag has data
            if frag.get_data_size() < 1:
                logging.error("File %s has an empty fragment!", tpstream_file)
                sys.exit(1)
            tp = trgdataformats.TriggerPrimitive(frag.get_data(0))

            if i == 0:
                all_tpstream_files.append(TPStreamFile(tpstream_file, tp.time_start, 0))
                logging.debug("First time start: %s", tp.time_start)

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
        del loaded_file, first_record, source_ids

    # No need for an if verbose check here, as logging level is already set
    logging.info("Extracted ROUs and planes: %s", rou_plane_data.data)

    return all_tpstream_files, rou_plane_data

def update_tpstream_indices(tpstream_files: List[TPStreamFile]) -> List[TPStreamFile]:
    """
    Sorts the files by the time of the very first TP.
    The data will be sorted in TPMM, but this speeds it up.
    """
    logging.info("Sorting TPStream files by time and assigning indices")
    sorted_files = sorted(tpstream_files, key=lambda x: x.stime)
    for i, tp_file in enumerate(sorted_files):
        tp_file.index = i + 1
    cleanup()
    logging.debug("Sorted TPSTreamFile objects: %s", sorted_files)
    return sorted_files

def update_tpstream_dal_objects(a_tp_stream, cfg, sorted_tpstream_files):
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

def update_sid_dal_objects(a_sid, cfg, total_unique_planes):
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
    base_string = "replay-tp-srcid-100000"
    start_number = int(re.search(r'(\d+)$', base_string).group(1))
    for i in range(1, total_unique_planes + 1):
        temp_sid = copy.deepcopy(a_sid)
        temp_sid.id = re.sub(r'\d+$', f"{start_number + i:06d}", base_string)
        temp_sid.sid = i
        temp_sid.subsystem = "Trigger"
        all_sids.append(temp_sid)
        logging.debug("Created SID config: %s", temp_sid)
    return all_sids

def update_configuration(cfg, replay_app, tpmm_conf, sorted_tpstream_files, total_unique_planes, path_str, verbose: bool):
    """
    Takes all changes and updates the local database files.
    [total_planes in TPMM
     tp_streams in TPMM
     tp_source_ids in replay]
    """
    logging.info("Updating configuration with new TPStream data")
    tpmm_conf.total_planes = total_unique_planes

    a_tp_stream = tpmm_conf.tp_streams[0] if tpmm_conf.tp_streams else None
    tpmm_conf.tp_streams = update_tpstream_dal_objects(a_tp_stream, cfg, sorted_tpstream_files)
    logging.info("Total of %i TPStream configs created", len(tpmm_conf.tp_streams))

    a_sid = replay_app.tp_source_ids[0] if replay_app.tp_source_ids else None
    replay_app.tp_source_ids = update_sid_dal_objects(a_sid, cfg, total_unique_planes)
    logging.info("Total of %i SID configs created", len(replay_app.tp_source_ids))

    cleanup()
    logging.debug("Committing updated configuration to database")

    db_modules = conffwk.Configuration(f"oksconflibs:{path_str}/moduleconfs.data.xml")
    db_trigger = conffwk.Configuration(f"oksconflibs:{path_str}/trigger-segment.data.xml")
    for tpstream in tpmm_conf.tp_streams:
        db_modules.update_dal(tpstream)
    for sid in replay_app.tp_source_ids:
        db_trigger.update_dal(sid)
    db_trigger.update_dal(replay_app)
    db_modules.update_dal(tpmm_conf)
    db_modules.commit()
    db_trigger.commit()
    logging.info("Local database updated!")
    cleanup()

def main():
    parser = argparse.ArgumentParser(description="To be used with Trigger Replay Application. Process TPStream data and update configuration.")
    parser.add_argument("--files", type=str, required=True, help="Text file with (full) paths to HDF5 TPStream file location. One per line.")
    parser.add_argument("--config", type=str, default="config/daqsystemtest/example-configs.data.xml", help="Path to OKS configuration file.")
    parser.add_argument("--path", type=str, default="replay-run", help="Path for local output for configuration files.")
    parser.add_argument("--verbose", action="store_true", help="Enable verbose logging.")
    args = parser.parse_args()

    # Set up logging based on the verbose flag
    setup_logging(args.verbose)

    # Set memory limit
    set_mem_limit(args.verbose) 

    logging.info("Starting TPStream processing script")
    cfg = setup_configuration(args.path, args.config, args.verbose)
    replay_app = get_replay_app(cfg)
    logging.debug("Replay application configuration: %s", replay_app)
    tpmm_conf = replay_app.tpmm_conf
    logging.debug("TPMM configuration: %s", tpmm_conf)
    channel_map = detchannelmaps.make_map(tpmm_conf.channel_map)
    planes_to_filter = {plane.plane for plane in tpmm_conf.filter_out_plane}
    logging.debug("Planes to filter: %s", planes_to_filter)

    files = get_tpstream_files(args.files, args.verbose)
    check_files(files)
    all_tpstream_files, rou_plane_data = extract_rous_and_planes(files, channel_map, planes_to_filter, args.verbose)
    sorted_tpstream_files = update_tpstream_indices(all_tpstream_files)

    total_unique_planes = rou_plane_data.total_plane_count()
    logging.info("Total plane count: %d", total_unique_planes)
    update_configuration(cfg, replay_app, tpmm_conf, sorted_tpstream_files, total_unique_planes, args.path, args.verbose)

if __name__ == "__main__":
    main()

