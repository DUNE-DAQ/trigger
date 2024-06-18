// This is the application info schema used by the TXBuffer modules.
// It describes the information object structure passed by the application 
// for operational monitoring

local moo = import "moo.jsonnet";
local s = moo.oschema.schema("dunedaq.trigger.txbufferinfo");

local info = {
    uint8  : s.number("uint8", "u8",
                     doc="An unsigned of 8 bytes"),

   info: s.record("Info", [
       s.field("num_payloads",                  self.uint8, 0, doc="Number of payloads received."), 
       s.field("num_payloads_overwritten",      self.uint8, 0, doc="Number of payloads overwritten."), 
       s.field("num_requests",                  self.uint8, 0, doc="Number of fragment requests received."), 
       s.field("num_buffer_elements",           self.uint8, 0, doc="Occupancy of the TXBuffer's latencybuffer."), 
   ], doc="TXBuffer operational monitoring information")
};

moo.oschema.sort_select(info) 
