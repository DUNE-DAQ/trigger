// This is the application info schema used by the module level trigger module.
// It describes the information object structure passed by the application 
// for operational monitoring

local moo = import "moo.jsonnet";
local s = moo.oschema.schema("dunedaq.trigger.triggergenericmakerinfo");

local info = {
    uint8  : s.number("uint8", "u8",
                     doc="An unsigned of 8 bytes"),

   info: s.record("Info", [
       s.field("received_count",                  self.uint8, 0, doc="Number of inputs received."), 
       s.field("sent_count",                      self.uint8, 0, doc="Number of results added to queue."), 
       s.field("data_vs_system_in_ms",            self.uint8, 0, doc="Difference between data time and system time when entering maker (ms)"),
       s.field("data_vs_system_out_ms",           self.uint8, 0, doc="Difference between data time and system time when exiting maker (ms)")
   ], doc="Trigger Generic Maker information")
};

moo.oschema.sort_select(info) 
