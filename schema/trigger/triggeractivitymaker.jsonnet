local moo = import "moo.jsonnet";
local ns = "dunedaq.trigger.triggeractivitymaker";
local s = moo.oschema.schema(ns);

local types = {
  name: s.string("Name", ".*", doc="Name of a plugin etc"),
  region: s.number("Region", "u2", doc="16bit region identifier for a GeoID"),
  element: s.number("Element", "u4", doc="32bit element identifier for a GeoID"),
  time: s.number("Time", "u8", doc="A count of timestamp ticks"),
  any: s.any("Data", doc="Any"),

  conf: s.record("Conf", [
    s.field("activity_maker", self.name,
      doc="Name of the activity maker implementation to be used via plugin"),
    s.field("geoid_region", self.region,
      doc="The region used in the GeoID for TASets produced by this maker"),
    s.field("geoid_element", self.element,
      doc="The element used in the GeoID for TASets produced by this maker"),
    s.field("buffer_time", self.time,
      doc="The time to buffer past a window before emitting a TASet for that window in ticks"),
    s.field("activity_maker_config", self.any,
      doc="Configuration for the activity maker implementation"),
    ], doc="TriggerActivityMaker configuration"),

};

moo.oschema.sort_select(types, ns)
