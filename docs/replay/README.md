# Trigger Replay Application

This is the new Trigger Replay Application (v5+). For the previous version follow [here](https://github.com/DUNE-DAQ/trigger/tree/production/v4/python/trigger/replay_tps). This version is NOT backward compatible. 

## Table of Contents
- [What is Replay](#what-is-replay)
- [How does it work?](#how-does-it-work)
- [How to Replay?](#how-to-replay)
- [Implementation](#implementation)
- [Appmodel schemas](#appmodel-schemas)
- [Appmodel source code](#appmodel-source-code)
- [Set-up](#set-up)
- [Operational Monitoring](#operational-monitoring)
- [Other Notes](#other-notes)
- [TODO (future)](#todo-future)

## What is Replay
The replay application is an 'emulation tool'. It is meant for developing the trigger, associated infrastructure, integration, and algorithm testing. 

### How does it work?
The application is replacing the readout and instead uses TPs from offline files (HDF5 TPStream). The implementation is 'emulating' the readout closely, meaning the data is replayed per-plane and per-readout unit. TP Handlers are also part of the same application, exactly as in the current readout application.

Process:
- accepts TPStream HDF5 files (taken with v5+)
- uses `TriggerPrimitiveMaker` module to assess the data and extract selected TPs 
- creates TP Handlers (the number depends on configuration), with a configured algorithm
- creates required queues, network connections
- spawns individual threads for each plane
- streams TPs (in vectors), each plane using its own thread
- also contains buffers, which respond to readout requests
- the timing is controlled by a global clock
- as a separate standalone application, it can be used in combination with other DAQ applications

## How to Replay
Replay works via a `TriggerReplayApplication`, a smart DAQ application that can be used inside the trigger segment of your OKS session.
To use it, simply add this application to the trigger segment in your session. There are example sessions available, both local and with ehn1 integration. 
Remember, replay is emulation readout and it simply outputs TAs, so for a full stream, a trigger application creating TCs and an MLT application are required. 
Finally, configure the `TriggerPrimitiveMaker` module that is part of this application. It accepts list of input HDF5 TPStream files. Additionally, one can choose to filter-out planes.

## Implementation
### Appmodel schemas
`TriggerReplayApplication` schema:
```xml
 <class name="TriggerReplayApplication">
  <superclass name="ResourceSetAND"/>
  <superclass name="SmartDaqApplication"/>
  <attribute name="application_name" type="string" init-value="daq_application" is-not-null="yes"/>
  <relationship name="tp_source_ids" class-type="SourceIDConf" low-cc="zero" high-cc="many" is-composite="no" is-exclusive="no" is-dependent="no"/>
  <relationship name="tpmm_conf" class-type="TriggerPrimitiveMakerModuleConf" low-cc="one" high-cc="one" is-composite="no" is-exclusive="no" is-dependent="no"/>
  <relationship name="tp_handler" class-type="DataHandlerConf" low-cc="one" high-cc="one" is-composite="no" is-exclusive="no" is-dependent="no"/>
  <method name="generate_modules" description="Generate daq module dal objects for TriggerReplayApplication on the fly">
   <method-implementation language="c++" prototype="std::vector&lt;const dunedaq::confmodel::DaqModule*&gt; generate_modules(conffwk::Configuration*, const std::string&amp;, const confmodel::Session*) const override" body=""/>
  </method>
  <method name="get_ro_unit" description="">
    <method-implementation language="c++" prototype="static int get_ro_unit(const std::string&amp; path)" body=""/>
  </method>
 </class>
```
- it's own application
- inherits from `SmartDaqApplication`
- Configuration options:
  - *TP Source IDs*: these are already set up to cover 3 planes for 4 different Readout Units (so can be left untouched for NP0X)
  - configuration for `TriggerPrimitiveMaker` module (below)
  - configuration for *TP Handler (TA Maker)*
- declaration of `generate_modules` function (modules & connections build instructions)
- declaration of helper `get_ro_unit` function, which is used by multiple applications to extract Readout Unit [ROU] from file's path
<br>

`TriggerPrimitiveMaker` module schema: 
```xml
 <class name="TriggerPrimitiveMakerModule">
  <superclass name="DaqModule"/>
  <relationship name="configuration" class-type="TriggerPrimitiveMakerModuleConf" low-cc="one" high-cc="one" is-composite="no" is-exclusive="no" is-dependent="no"/>
 </class>

 <class name="TriggerPrimitiveMakerModuleConf">
  <attribute name="template_for" type="class" init-value="TriggerPrimitiveMakerModule"/>
  <attribute name="number_of_loops" type="u32" init-value="1" is-not-null="yes"/>
  <attribute name="maximum_wait_time_us" type="u32" init-value="1000" is-not-null="yes"/>
  <attribute name="channel_map" type="string" init-value="PD2HDChannelMap" is-not-null="yes"/>
  <relationship name="filter_out_plane" class-type="PlaneNumberConf" low-cc="zero" high-cc="many" is-composite="no" is-exclusive="no" is-dependent="no"/>
  <relationship name="tp_streams" class-type="TPStreamConf" low-cc="one" high-cc="many" is-composite="no" is-exclusive="no" is-dependent="no"/>
 </class>
```
- Configuration options:

| Option                 | Description                                                                                      |
|------------------------|--------------------------------------------------------------------------------------------------|
| **number_of_loops**    | Allows replaying the TPs multiple times with shifted timestamps.                                 |
| **maximum_wait_time_us** | Max buffer time between sending consecutive TP vectors.                                            |
| **channel_map**        | Specifies the detector channel map, used to extract Readout Unit (ROU).                         |
| **filter_out_plane**   | Option to filter out (ignore) data from a specific plane (Induction 1 / Induction 2 / Collection). |
| **tp_streams**        | List of TPStream HDF5 files to be used as input (multiple files supported).                      |
<br>
 
Plane filtering schema:
```xml
 <class name="PlaneNumberConf">
  <attribute name="plane" type="u32" init-value="0" is-not-null="yes"/>
 </class>
```
The 3 planes are already part of the configuration, so one can simply select 0-2 to use:
![conf_plane](https://github.com/user-attachments/assets/d49f55af-c578-4cfe-ab4d-d9de2193f476)
<br>

TPStream configuration:
```xml
 <class name="TPStreamConf">
  <attribute name="filename" type="string" init-value="1" is-not-null="yes"/>
 </class>
 ```
 Simple full path to your HDF5 TPStream files. Many can be used at once:
![conf_files](https://github.com/user-attachments/assets/665f8e12-0cec-4e63-91ac-69c85b8fc008)
<br>
 
### Appmodel source code
The design of the source code is a result of the aim to be 'user friendly'. This means the user can provide a vector of files, without restrictions on ROUs. 
Therefore, a lot of heavy lifting happens when the application modules and links are being generated. 
The basis for the procedure is: 
- loop over the files, checking validity (exists; is a TPStream HDF5 file; has data)
- extract the ROU for the file
- output a map of vectors of (valid) files, grouped by ROUs, ordered by time within the vectors

Afterward, the generation is based on: 

> **number of unique ROUs** x **number of active planes**

An active plane is a plane that is **not** filtered out.  
Therefore, in an example scenario where 4 files are provided, covering 4 unique ROUs (for example 4 different APAs), and no planes are configured to be filtered out, this magic number will be 4 (ROUs) x 3 (planes) = 12.
This means there would be 12 `TPHandlers`, 12 queues from `TriggerPrimitiveMaker`, 12 data request network connections, 12 outcoming TA publishing network connections.
Importantly, there is always just 1 `TriggerPrimitiveMaker`, however, it will make use of 12 threads, each feeding its own `TPHandler` (pretending to be a plane from readout). 
<br>

It should be mentioned that the application is fully integrated with the rest of the system, such as registering the SourceIDs in MLT and in DFO. 
<br>

### Set-up
#### TriggerReplayApplication
- Example Trigger Replay application for 1 ROU and 1 active plane:
![replay dot](https://github.com/user-attachments/assets/7c00a8a9-1ffd-4c3f-89cb-598a82c1b444)
- Another example using 2 ROUs and 2 active planes:
![replay3 dot](https://github.com/user-attachments/assets/cdb442e5-0361-43f5-9ca6-d6b9edd91600)

One can see the module generation being driven by the unique ROU and active planes. 
Some additional notes:
- There is always 1 `TriggerPrimitiveMaker` module. It has an internal logic that spawns threads.
- The `TPHandlers` make use of the common `TriggerDataHandlerModules`, meaning they also contain latency buffers, have unique SourceIDs, and respond to data requests.
- The `TPHandlers` create TAs and stream these to output network connections. 
<br>

#### Connecting to the DAQ system
![session dot](https://github.com/user-attachments/assets/a4e56fe8-c0ac-4b42-b837-d14ef4be25ac)
- the TriggerReplayApplication is part of the `trg-segment`
- it has an input from `DFApplication`: readout requests
- it publishes TAs to a `TriggerApplication`; this creates TCs and passes onwards to `MLT`
- generally, the flow is similar to having a readout application replaced

### TriggerPrimitiveMaker module
The `TriggerPrimitiveMaker` module is the base of replay.
Functionality:
- loads in configuration; including HDF5 files, planes, channel map...
- runs checks on files: file exists, is valid HDF5, is TPStream type, has valid fragments, contains TPs
- loops over files, extracts the ROU, and sorts the files by unique ROU (additionally, if there are multiple files for an ROU, the files are time ordered)
- creates unique *streams*, each stream representing a unique plane data
- plane data is extracted from the file, filtering is applied
- TP data is handled in TP vectors (this is mostly because it was already available for `TriggerDataHandlerModule`, was previously using `TPSets` but there is no implementation for this data type)
- each stream spawns a unique thread, running independently (timing controlled by clock)
- running threads check the slice time (slice, in this case, represented as a vector of TPs, covering one fragment of TPs), compare to the clock, and send over the queues to `TPHandlers` as appropriate. There is an additional wait time applied so as to not overwhelm the system.
- if multiple loops are configured, the TP times are shifted to allow for repetition with new/future times
- publishes opmon data
- has basic logging / counters


Additionally, multiple new issues have been declared to handle errors, for example:
- `ReplayConfigurationProblem`: Missing or incorrect configuration
- `ReplayNoValidFiles`: No provided file passes checks
- `ReplayNoValidTPs`: No valid TPs have been extracted
For full list please see: [Issues.hpp](./../../include/trigger/Issues.hpp)


#### Logging
Verbose logging is available in the `TriggerPrimitiveMaker` module:
- Configuration:
```
### REPLAY CONFIGURATION ###
Will use channel map: PD2HDChannelMap
Plane filtering: 1
Planes to filter:
0
```
- File overview:
```
Files to use:
ROU: APA_P01SU
<file>.hdf5
ROU: APA_P02NL
<different_file>.hdf5
```
- Plane data summary:
```
Will use 44 fragments for ROU: APA_P01SU, plane: 1.
Data loading summary (plane stage):
------------------------------
File: <file>.hdf5
ROU: APA_P01SU
Plane: 1
Read TPs: 32738550
TP vectors: 44
```
- File data summary:
```
Data loading summary (file stage):
------------------------------
ROU: APA_P01SU
Planes: 2
Total read TPs: 47295032
TP vectors: 88
```
- Thread summary after running:
```
Thread summary:
------------------------------
Sent TPs: 14556482
TP vectors: 44
Time taken: 42292 ms
Rate: 1.04041 TP vectors/s
Failed to push TP vectors: 0
```
- Global (aggregated) summary:
```
### SUMMARY ###
------------------------------
Generated TP vectors: 178
Generated TPs: 94783270
Time taken: 135302 ms
Rate: 1.31558 TP vectors/s
Failed to push TP vectors: 0
```
This can be compared with opmon from `TPHandlerModule` for sanity checking. 

## OKS Sessions
Two example replay sessions are available as part of example-configs in `daqsystemtest` repository. These are identical in terms of setup, with the only difference being opmon and error reporting. 
- *local-replay-config*: local opmon & reporting
- *ehn1-replay-config*: common cern opmon and reporting

- Replay session:

![replay_ses](https://github.com/user-attachments/assets/8456a4bb-8f51-40b3-99dc-e4d8f2654c7d)

- Trigger segment:

![conf_trg](https://github.com/user-attachments/assets/f913afc9-32f7-40b9-980c-47d7cf8de583)

- `TriggerPrimitiveMaker` module configuration:

![conf_tpmm](https://github.com/user-attachments/assets/def5662f-2df7-4f88-9d43-182c36109744)

As mentioned, the different plane options are already configured, and can simply be selected as needed.

Otherwise, the sessions are kept minimal. Most applications that are not needed are disabled (but can be used of course). `TriggerReplayApplication` is added to the `trg-segment`. You can see an overview [here](#connecting-to-the-daq-system).

## Operational Monitoring
A graph showing opmon data from `TriggerPrimitiveMaker` module is available on Grafana: 
> Grafana -> Trigger Flow -> Plugins -> TP Maker

![new_graf](https://github.com/user-attachments/assets/c3490188-32f1-4693-9e25-cf3d0c5b58d8)

This shows TP data being read in, and TP vector data being created and sent. 
Additionally, the handler modules used are typical in the sense that they already provide the expected opmon data (TP receiving rates, TA making rates...). All other objects also have corresponding monitoring (queues, network connections, buffers...). 

## Other Notes
### Issues / Perks
- *Dynamicity*: The choice to be user-friendly carries a price. Readout units are generally easily extracted from an HDF5 file using already available functions in `hdf5libs` repository. However, this repository is not available in `appmodel` (as `appmodel` is a dependency of `hdf5libs`). This can be solved in a few ways. For now, the `TriggerReplayApplication` extracts the ROU from the TPStream file name. This therefore requires consistency in the naming to be retained, or building a custom extractor function that would replicate portions of the `hdf5libs` code.  
- *Plane filtering*: The code pretends that for APA1 ("APA_P02SU") the collection plane is induiction plane 2, and vice-versa. This is by choice, as for NP04 running plane 2 was used as effective collection plane for APA1.
- *Configuration management*: Current implementation uses 1 queue description and 1 `TPHandler` configuration, that is then used for all the instances of queues and handler objects (with unique names of course). This means that for a file with 3 active planes, the handler for each plane would be using the same algorithm (same for readout).
- *Init stage*: A lot is happening inside the `TriggerPrimitiveMaker` module at the init stage: parsing configuration, multiple checks on HDF5 files, extracting ROUs, actually extracting TP data, plane filtering... Depending on the number of files this can take a lot of time. If needed, portions of this can be moved to different run stages.
- *Expectations ?*: There are many places in the current dune-daq code where expectations are baked in (but not necessarily documented), for example, an expectation for queues that are at times not obvious (ie `TPRequestHandler` is expected to link to `FragmentAggregatorModule`, but this module is not required outside readout).
- *Memory limits*: Some memory optimization is implemented, however, TPStream files are often very big. Because most processing happens within one module (TPMm), memory usage can be an issue for many files at once. You have been warned.

### TODO (future)
- [ ] should the initial TP times be shifted (as if they were streamed now) ?
- [ ] support for multiple concurrent (different) makers
- [ ] when TP format changes (relative `tp.time_peak`) looping logic needs adjusting

For more details please see [this report](https://docs.dunescience.org/cgi-bin/private/ShowDocument?docid=32918). 
