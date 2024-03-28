// This is the application info schema used by the cib trigger candidate maker module.
// It describes the information object structure passed by the application 
// for operational monitoring

local moo = import "moo.jsonnet";
local s = moo.oschema.schema("dunedaq.trigger.cibtriggercandidatemakerinfo");

local info = {
    uint8  : s.number("uint8", "u8",
                     doc="An unsigned of 8 bytes"),

   info: s.record("Info", [
       s.field("tsd_received_count",    self.uint8, 0, doc="Number of HSIEvent messages received."), 
       s.field("tc_sent_count",         self.uint8, 0, doc="Number of trigger candidates added to queue."), 
       s.field("tc_sig_type_err_count", self.uint8, 0, doc="Number of trigger candidates not added to queue due to a signal type error."), 
       s.field("tc_total_count",        self.uint8, 0, doc="Total number of trigger candidates created."), 
   ], doc="CIB trigger candidate maker information.")
};

moo.oschema.sort_select(info) 
