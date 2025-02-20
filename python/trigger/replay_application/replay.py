import argparse
import copy
import re
import logging
from collections import defaultdict
from dataclasses import dataclass, field
from typing import Dict, Set, List
from pathlib import Path

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

@dataclass
class ROUPlaneData:
    data: Dict[str, Set[int]] = field(default_factory=lambda: defaultdict(set))

    def add_value(self, rou: str, plane: int):
        self.data[rou].add(plane)

    def get_values(self, rou: str):
        return self.data.get(rou, set())

    def total_plane_count(self):
        return sum(len(planes) for planes in self.data.values())

@dataclass
class TPStreamFile:
    filename: str
    stime: int
    index: int

    def __str__(self):
        return f"Name: {self.filename}, Stime: {self.stime}, Index: {self.index}"

def setup_configuration(path_str: str, sessions_file: str, verbose: bool):
    logging.info("Setting up configuration files")
    logging.debug("Local path for configurations: %s", path_str)
    path = Path(path_str).resolve()  # Convert string to Path object
    path.mkdir(parents=True, exist_ok=True)
    external_logger = logging.getLogger('daqconf.consolidate')
    if not verbose: external_logger.setLevel(logging.WARNING)
    copy_configuration(path, [sessions_file])

    logging.debug("Copying configuration represented by databases: %s", [sessions_file])

    return conffwk.Configuration(f"oksconflibs:{path}/example-configs.data.xml")

def get_tpstream_files(filename: str, verbose: bool) -> List[str]:
    logging.info("Reading TPStream file list from %s", filename)
    with open(filename, "r") as file:
        tpstream_files = [line.strip() for line in file.readlines()]

    logging.info("Total files to process: %i", len(tpstream_files))
    logging.debug("TPStream files loaded: %s", tpstream_files)

    return tpstream_files

def extract_rous_and_planes(files: List[str], channel_map, planes_to_filter: Set[int], verbose: bool) -> (List[TPStreamFile], ROUPlaneData):
    logging.info("Extracting ROUs and planes from files")
    all_tpstream_files = []
    rou_plane_data = ROUPlaneData()

    for tpstream_file in files:
        logging.info("Processing file: %s", tpstream_file)
        loaded_file = HDF5RawDataFile(tpstream_file)
        first_record = loaded_file.get_all_record_ids()[0]
        source_ids = loaded_file.get_source_ids_for_fragment_type(first_record, "Trigger_Primitive")
        logging.debug("SIDs: %s", source_ids)

        for i, sid in enumerate(source_ids):
            frag = loaded_file.get_frag(first_record, sid)
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

    # No need for an if verbose check here, as logging level is already set
    logging.info("Extracted ROUs and planes: %s", rou_plane_data.data)

    return all_tpstream_files, rou_plane_data

def update_tpstream_indices(tpstream_files: List[TPStreamFile]) -> List[TPStreamFile]:
    logging.info("Sorting TPStream files by time and assigning indices")
    sorted_files = sorted(tpstream_files, key=lambda x: x.stime)
    for i, tp_file in enumerate(sorted_files):
        tp_file.index = i + 1
    logging.debug("Sorted TPSTreamFile objects: %s", sorted_files)
    return sorted_files

def update_configuration(replay_app, tpmm_conf, sorted_tpstream_files, total_unique_planes, path_str, verbose: bool):
    logging.info("Updating configuration with new TPStream data")
    tpmm_conf.total_planes = total_unique_planes

    a_tp_stream = tpmm_conf.tp_streams[0] if tpmm_conf.tp_streams else None
    if not a_tp_stream:
        logging.warning("No template TPStream object found")
        # TODO get template and create from scratch
    tp_streams = []
    for a_file in sorted_tpstream_files:
        temp_tp_stream = copy.deepcopy(a_tp_stream)
        temp_tp_stream.id = f"def-tp-stream-{a_file.index}"
        temp_tp_stream.filename = a_file.filename
        temp_tp_stream.index = a_file.index
        tp_streams.append(temp_tp_stream)
        logging.debug("Created TPStream: %s", temp_tp_stream)
    tpmm_conf.tp_streams = tp_streams
    logging.info("Total of %i TPStream configs created", len(tp_streams))

    a_sid = replay_app.tp_source_ids[0] if replay_app.tp_source_ids else None
    if not a_sid:
        logging.warning("No template Source ID object found")
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
    replay_app.tp_source_ids = all_sids
    logging.info("Total of %i SID configs created", len(all_sids))

    # No need for an if verbose check here, as logging level is already set
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

def main():
    parser = argparse.ArgumentParser(description="Process TPStream data and update configuration.")
    parser.add_argument("--files", type=str, required=True, help="Path to TPStream files list.")
    parser.add_argument("--config", type=str, default="config/daqsystemtest/example-configs.data.xml", help="Path to configuration file.")
    parser.add_argument("--path", type=str, default="replay-run", help="Path for configuration output.")
    parser.add_argument("--verbose", action="store_true", help="Enable verbose logging.")
    args = parser.parse_args()

    # Set up logging based on the verbose flag
    setup_logging(args.verbose)

    logging.info("Starting TPStream processing script")
    cfg = setup_configuration(args.path, args.config, args.verbose)
    replay_app = cfg.get_dals("TriggerReplayApplication")[0] # need a check here
    logging.debug("Replay application configuration: %s", replay_app)
    tpmm_conf = replay_app.tpmm_conf
    logging.debug("TPMM configuration: %s", tpmm_conf)
    channel_map = detchannelmaps.make_map(tpmm_conf.channel_map)
    planes_to_filter = {plane.plane for plane in tpmm_conf.filter_out_plane}
    logging.debug("Planes to filter: %s", planes_to_filter)

    files = get_tpstream_files(args.files, args.verbose)
    all_tpstream_files, rou_plane_data = extract_rous_and_planes(files, channel_map, planes_to_filter, args.verbose)
    sorted_tpstream_files = update_tpstream_indices(all_tpstream_files)

    total_unique_planes = rou_plane_data.total_plane_count()
    logging.info("Total plane count: %d", total_unique_planes)
    update_configuration(replay_app, tpmm_conf, sorted_tpstream_files, total_unique_planes, args.path, args.verbose)

if __name__ == "__main__":
    main()

