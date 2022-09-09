local moo = import "moo.jsonnet";
local ns = "dunedaq.trigger.triggercandidatemaker";
local s = moo.oschema.schema(ns);

local types = {
  name: s.string("Name", ".*",
    doc="Name of a plugin etc"),

  any: s.any("Data", doc="Any"),

  conf: s.record("Conf", [
    s.field("candidate_maker", self.name,
      doc="Name of the candidate maker implementation to be used via plugin"),
    s.field("candidate_maker_config", self.any,
      doc="Configuration for the candidate maker implementation"),
    ], doc="TriggerCandidateMaker configuration"),

};

moo.oschema.sort_select(types, ns)
