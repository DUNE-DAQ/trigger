# Trigger Replay Application

This is the new Trigger Replay Application (v5+). For the previous version follow [here](https://github.com/DUNE-DAQ/trigger/tree/production/v4/python/trigger/replay_tps).

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

## Implementation
### TriggerReplayApplication
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
  - `number_of_loops`: possibility to 'shift' the times of TPs and replay them multiple times in consecution
  - `maximum_wait_time_us`: a little time buffer to wait between sending consecutive TP vectors
  - `channel_map`: detector channel map, used to extract ROU
  - `filter_out_plane`: possibility to filter out (ignore) data from a particular plane (induction 1 / induction 2 / collection)
  - `tp_streams`: a vector of TPStream HDF5 files to be used (your TP data input)
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
 

