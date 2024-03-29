local moo = import "moo.jsonnet";
local ns = "dunedaq.trigger.customtriggercandidatemaker";
local s = moo.oschema.schema(ns);

local types = {
  ticks: s.number("ticks", dtype="i8"),
  freq: s.number("frequency", dtype="u8"),
  timestamp_estimation: s.enum("timestamp_estimation", ["kTimeSync", "kSystemClock"]),
  tc_type : s.number("tc_type", "i4", doc="TC type"), 
  tc_types : s.sequence("tc_types", self.tc_type, doc="List of TC types"),
  tc_interval : s.number("tc_interval", "i8", doc="interval (in clock ticks) for a TC type"),
  tc_intervals : s.sequence("tc_intervals", self.tc_interval, doc="List of TC intervals"),

  conf : s.record("Conf", [
    s.field("clock_frequency_hz", self.ticks, 62500000,
      doc="Assumed clock frequency in Hz (for current-timestamp estimation)"),

    s.field("timestamp_method", self.timestamp_estimation, "kTimeSync",
      doc="Use TimeSync queues to estimate timestamp (instead of system clock)"),

    s.field("trigger_types", self.tc_types, [],
      doc="List of TC types to be used by CTCM"),

    s.field("trigger_intervals", self.tc_intervals, [],
      doc="List of TC intervals (in clock ticks) for provided TC types"),

  ], doc="CustomTriggerCandidateMaker configuration parameters"),

};

moo.oschema.sort_select(types, ns)
