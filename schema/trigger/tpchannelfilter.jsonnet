local moo = import "moo.jsonnet";
local ns = "dunedaq.trigger.tpchannelfilter";
local s = moo.oschema.schema(ns);

local types = {
  bool: s.boolean("Boolean"),
  string : s.string("String", moo.re.ident,
    doc="A string field"),
  ticks: s.number("ticks", dtype="i8"),
  
  conf : s.record("Conf", [
    s.field("keep_collection", self.bool,
      doc="Whether to keep collection-channel TPs"),
    s.field("keep_induction", self.bool,
      doc="Whether to keep induction-channel TPs"),
    s.field("channel_map_name", self.string,
      doc="Name of channel map"),    
    s.field("max_time_over_threshold", self.ticks,
      doc="Maximum allowed time over threshold per TP in number of ticks"),
  ], doc="FakeTPCreatorHeartbeatMaker configuration parameters."),

};

moo.oschema.sort_select(types, ns)
