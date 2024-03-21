local moo = import "moo.jsonnet";
local ns = "dunedaq.trigger.ctbtriggercandidatemaker";
local s = moo.oschema.schema(ns);
local nc = moo.oschema.numeric_constraints;

local types = {
	hsi_tt_pt : s.boolean("hsi_tt_pt"),
        count_t : s.number("count_t", "i8", nc(minimum=1), doc="Counter"),
	conf: s.record("Conf", [
		s.field("hsi_trigger_type_passthrough", self.hsi_tt_pt, doc="Option to override the trigger type values"),
                s.field("prescale", self.count_t, default=1, doc="Option to prescale TTCM TCs")
	], doc="Configuration of the different readout time maps"),

};

moo.oschema.sort_select(types, ns)
