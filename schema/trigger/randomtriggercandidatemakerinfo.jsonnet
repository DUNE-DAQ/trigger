// This is the application info schema used by the random trigger candidate maker module.
// It describes the information object structure passed by the application 
// for operational monitoring

local moo = import "moo.jsonnet";
local s = moo.oschema.schema("dunedaq.trigger.randomtriggercandidatemakerinfo");

local info = {
    uint8  : s.number("uint8", "u8",
                     doc="An unsigned of 8 bytes"),

   info: s.record("Info", [
       s.field("tc_sent_count", self.uint8, 0, doc="Number of trigger candidates added to queue."), 
   ], doc="Random trigger candidate maker information.")
};

moo.oschema.sort_select(info) 
