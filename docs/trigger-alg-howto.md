# How to write a trigger algorithm

The DUNE DAQ data selection software separates physics algorithms
(which make trigger activity and trigger candidate objects) from
concerns about how the data objects are packaged and transported
through the data selection system.

Physics algorithms are implemented in the [`triggeralgs`](https://github.com/DUNE-DAQ/triggeralgs) package. To create a new physics algorithm, create a class that derives from [`triggeralgs::TriggerActivityMaker`](https://github.com/DUNE-DAQ/triggeralgs/blob/develop/include/triggeralgs/TriggerActivityMaker.hpp) or [`triggeralgs::TriggerCandidateMaker`](https://github.com/DUNE-DAQ/triggeralgs/blob/develop/include/triggeralgs/TriggerCandidateMaker.hpp) as appropriate. I'll use `TriggerActivityMaker` for definiteness from here on: if you're writing a `TriggerCandidateMaker`, make the appropriate substitutions.

In your derived class, you must implement one pure virtual function, namely:

```cpp
void operator()(const TriggerPrimitive& input_tp, std::vector<TriggerActivity>& output_ta);
```

This function will be called by the data selection framework once for each input `TriggerPrimitive`. Your implementation should do whatever processing it wants, and if the addition of the latest `TriggerPrimitive` causes a new output `TriggerActivity` to be complete, add the `TriggerActivity` to the output vector `output_ta`.

The data selection framework will make calls to `operator()` with the `TriggerPrimitive`s strictly ordered by start time. That is, once `operator()` has been called with a `TriggerPrimitive` whose start time is `T`, it will not later be called with a `TriggerPrimitive` whose start time is less than `T`. (If two `TriggerPrimitives` have exactly the same time, the relative order of the calls to `operator()` is unspecified.)

You will of course need to store the state of your algorithm between calls to `operator()`: use member variables in your class for this.

It's recommended (but not strictly required) to also implement the `flush()` function from `TriggerActivityMaker`, whose signature is:

```cpp
virtual void flush(timestamp_t until, std::vector<TriggerActivity>& output_ta)
```

The reason this function exists is to handle the case where there is a large gap between trigger primitives (or, more likely, between trigger activities). During this gap, `operator()` is not called, and so your algorithm cannot send its output, even if such a long time has passed that you know that any trigger activities currently in progress can be completed and sent out. In this case, the data selection framework calls your implementation of `flush(until, output_ta)` to inform you that no more trigger primitives have occurred between the last one for which `operator()` was called and timestamp `until`. If this causes your algorithm to complete any trigger activities, you can add them to the `output_ta` vector.

Finally, you must register the `TriggerActivityMaker` in the Trigger Activity Factory. This allows it to be accessed in this repository and used when running on `nanorc`. In the case of `TAMakerPrescaleAlgorithm`, the macro call is:

```cpp
REGISTER_TRIGGER_ACTIVITY_MAKER(TRACE_NAME, TAMakerPrescaleAlgorithm)
```

where `TRACE_NAME` is the string `"TAMakerPrescaleAlgorithm"`.

## Configuration

Your algorithm may take configuration parameters at run time (eg, a minimum number of hits or ADC to form a trigger activity, or a verbosity level). Your algorithm receives these configuration parameters via the `configure()` function, whose signature is:

```cpp
void configure(const nlohmann::json &config);
```

The `config` argument is in `nlohmann::json` format, documentation for which can be found at https://json.nlohmann.me/ . Here is a simple example implementation, from the `TAMakerPrescaleAlgorithm` algorithm:

```cpp
void
TAMakerPrescaleAlgorithm::configure(const nlohmann::json &config)
{
  if (config.is_object() && config.contains("prescale"))
  {
    m_prescale = config["prescale"]; 
  }
  TLOG_DEBUG(TRACE_NAME) << "Using activity prescale " << m_prescale;
}
```

For instructions about actually constructing a configuration to pass to your algorithm, see the "Using your algorithm in the dunedaq framework" section below.

## Aggregation

Inputs from a number of sources may be aggregated to send to a physics algorithm. The standard aggregation is that a `TriggerActivityMaker` receives trigger primitives aggregated over one APA, including both collection and induction channels, and a `TriggerCandidateMaker` receives trigger activities aggregated over the whole detector. We can consider alterations and additions to this scheme in future.

## Using your algorithm in the dunedaq framework

Once your code is registered to the appropriate factory (TA/TC), you can run a DAQ job that reads data from a file and passes it to your algorithm. Instructions on how to do that (including choosing configuration parameters for your algorithm) can be found here:

https://github.com/DUNE-DAQ/trigger/blob/develop/python/trigger/faketp_chain/README.md
