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

