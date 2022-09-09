local moo = import "moo.jsonnet";
local ns = "dunedaq.trigger.triggerdecisionmaker";
local s = moo.oschema.schema(ns);

local types = {
  name: s.string("Name", ".*",
    doc="Name of a plugin etc"),

  any: s.any("Data", doc="Any"),

  conf: s.record("Conf", [
    s.field("decision_maker", self.name,
      doc="Name of the decision maker implementation to be used via plugin"),
    s.field("decision_maker_config", self.any,
      doc="Configuration for the decusuib maker implementation"),
    ], doc="TriggerDecisionMaker configuration"),

};

moo.oschema.sort_select(types, ns)
