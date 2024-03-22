local moo = import "moo.jsonnet";
local ns = "dunedaq.trigger.ctbtriggercandidatemaker";
local s = moo.oschema.schema(ns);
local nc = moo.oschema.numeric_constraints;

local types = {
        count_t : s.number("count_t", "i8", nc(minimum=1), doc="Counter"),
        time_t : s.number("time_t", "i8", doc="Time"),
	conf: s.record("Conf", [
                s.field("prescale", self.count_t, default=1, doc="Option to prescale TTCM TCs"),
 	        s.field("time_before", self.time_t, 1000, doc="Readout time before time stamp"),
                s.field("time_after",  self.time_t, 1000, doc="Readout time after time stamp")
	], doc="CTB configuration block"),

};

moo.oschema.sort_select(types, ns)
