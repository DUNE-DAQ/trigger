// This is the application info schema used by the module level trigger module.
// It describes the information object structure passed by the application 
// for operational monitoring

local moo = import "moo.jsonnet";
local s = moo.oschema.schema("dunedaq.trigger.triggerzipperinfo");

local info = {
    uint8  : s.number("uint8", "u8",
                     doc="An unsigned of 8 bytes"),

   info: s.record("Info", [
       s.field("n_received",                  self.uint8, 0, doc="Number of inputs received."), 
       s.field("n_sent",                      self.uint8, 0, doc="Number of results added to queue."), 
       s.field("n_tardy",                     self.uint8, 0, doc="Number of Tarfy added to queue."), 
   ], doc="Trigger Generic Maker information")
};

moo.oschema.sort_select(info) 
