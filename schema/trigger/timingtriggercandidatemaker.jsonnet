local moo = import "moo.jsonnet";
local ns = "dunedaq.trigger.timingtriggercandidatemaker";
local s = moo.oschema.schema(ns);
local nc = moo.oschema.numeric_constraints;


local types = {
	time_t : s.number("time_t", "u8", doc="Time"),
	signal_type_t : s.number("signal_type_t", "u8", doc="Signal type"),
	trigger_type_t : s.string("trigger_type_t"),
	hsi_tt_pt : s.boolean("hsi_tt_pt"),
  count_t : s.number("count_t", "u8", nc(minimum=1), doc="Counter"),

  hsi_input: s.record("hsi_input", [
    s.field("signal",   self.signal_type_t,   default=0),
    s.field("tc_type_name", self.trigger_type_t,  default="kTiming"),
    s.field("time_before",  self.time_t,          default=10000, doc="Readout time before time stamp"),
    s.field("time_after",   self.time_t,          default=20000, doc="Readout time after time stamp"),
    ]),

  hsi_inputs: s.sequence("hsi_inputs", self.hsi_input),

  conf: s.record("Conf", [
    s.field("hsi_configs", self.hsi_inputs, [], doc="List of the input HSI configurations"),
    s.field("prescale", self.count_t, default=1, doc="Option to prescale TTCM TCs")
	], doc="Configuration of the different readout time maps"),

};

moo.oschema.sort_select(types, ns)
