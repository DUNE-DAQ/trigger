local moo = import "moo.jsonnet";
local ns = "dunedaq.trigger.randomtriggercandidatemaker";
local s = moo.oschema.schema(ns);

local types = {
  ticks: s.number("ticks", dtype="i8"),
  freq: s.number("frequency", dtype="u8"),
  timestamp_estimation: s.enum("timestamp_estimation", ["kTimeSync", "kSystemClock"]),
  distribution_type: s.enum("distribution_type", ["kUniform", "kPoisson"]),
  
  conf : s.record("Conf", [
    s.field("trigger_interval_ticks", self.ticks, 64000000,
      doc="Interval between triggers in 16 ns time ticks (default 1.024 s) "),

    s.field("clock_frequency_hz", self.ticks, 62500000,
      doc="Assumed clock frequency in Hz (for current-timestamp estimation)"),

    s.field("timestamp_method", self.timestamp_estimation, "kTimeSync",
      doc="Use TimeSync queues to estimate timestamp (instead of system clock)"),
      
    s.field("time_distribution", self.distribution_type, "kUniform",
      doc="Type of distribution used for random timestamps"),
    
  ], doc="RandomTCMakerModule configuration parameters"),

};

moo.oschema.sort_select(types, ns)
