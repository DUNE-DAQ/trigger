// This is the application info schema used by the module level trigger module.
// It describes the information object structure passed by the application 
// for operational monitoring

local moo = import "moo.jsonnet";
local s = moo.oschema.schema("dunedaq.trigger.moduleleveltriggerinfo");

local info = {
   cl : s.string("class_s", moo.re.ident,
                  doc="A string field"), 
    uint8  : s.number("uint8", "u8",
                     doc="An unsigned of 8 bytes"),

   info: s.record("Info", [
       s.field("class_name", self.cl, "moduleleveltriggerinfo", doc="Info class name"),
       s.field("trigger_decisions", self.uint8, 0, doc="Number of trigger decisions in the book"), 
       s.field("populated_trigger_ids", self.uint8, 0, doc="Number of trigger IDs with at least one fragment"), 
       s.field("total_fragments", self.uint8, 0, doc="Total number of fragments in the book"),
       s.field("old_fragments", self.uint8, 0, doc="Number of fragments that are late with respect to present time. How late is configurable"),
       s.field("old_trigger_ids", self.uint8, 0, doc="Number of populated trigger IDs that are late with respect to present time. How late is configurable")       
   ], doc="Fragment receiver information")
};

moo.oschema.sort_select(info) 
