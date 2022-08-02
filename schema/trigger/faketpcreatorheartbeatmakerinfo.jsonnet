// This is the application info schema used by the fake TP creator hearbeat maker module.
// It describes the information object structure passed by the application 
// for operational monitoring

local moo = import "moo.jsonnet";
local s = moo.oschema.schema("dunedaq.trigger.faketpcreatorheartbeatmakerinfo");

local info = {
    uint8  : s.number("uint8", "u8",
                     doc="An unsigned of 8 bytes"),

   info: s.record("Info", [
       s.field("tpset_received_count", self.uint8, 0, doc="Number of TPSets received."), 
       s.field("tpset_sent_count",     self.uint8, 0, doc="Number of TPSets added to queue."), 
       s.field("heartbeats_sent",      self.uint8, 0, doc="Number of TPSets corresponding to fake heartbeats added to queue."), 
   ], doc="Fake TP creator heartbeart maker information.")
};

moo.oschema.sort_select(info) 
