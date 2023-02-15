local moo = import "moo.jsonnet";
local ns = "dunedaq.trigger.tpchannelfilter";
local s = moo.oschema.schema(ns);

local types = {
  bool: s.boolean("Boolean"),
  string : s.string("String", moo.re.ident, doc="A string field"),
//  list: s.sequence("List", doc="List of values"), 
 
  conf : s.record("Conf", [
    s.field("keep_collection", self.bool,
      doc="Whether to keep collection-channel TPs"),
    s.field("keep_induction", self.bool,
      doc="Whether to keep induction-channel TPs"),
    s.field("channel_map_name", self.string,
      doc="Name of channel map"),
    s.field("noisy_channels", self.bool,
      doc="Known noisy offline channels, remove their TP contribution from the stream."),    
  ], doc="FakeTPCreatorHeartbeatMaker configuration parameters."),

};

moo.oschema.sort_select(types, ns)
