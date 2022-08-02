local moo = import "moo.jsonnet";
local ns = "dunedaq.trigger.triggerzipper";
local s = moo.oschema.schema(ns);

local hier = {
    // ito: s.number("InputTimeout", "u4",
    //               doc="Maximum time in milliseconds to wait to recv input"),
    // oto: s.number("OutputTimeout", "u4",
    //               doc="Maximum time in milliseconds to wait to send output"),
    card: s.number("Count", dtype='u8'),
    delay: s.number("Delay", dtype='u8'),

    // fixme: this should be factored, not copy-pasted
    region_id : s.number("RegionId", "u2"),
    element_id : s.number("ElementId", "u4"),

    conf : s.record("ConfParams", [
        s.field("cardinality", hier.card,
                doc="Expected number of streams"),
        s.field("max_latency_ms", hier.delay,
                doc="Max bound on latency, zero for unbound but lossless"),
        s.field("region_id", hier.region_id,
                doc="The GeoID region of output"),
        s.field("element_id", hier.element_id,
                doc="The GeoID element of output"),
    ], doc="TriggerZipper configuration"),

  
};

moo.oschema.sort_select(hier, ns)
