local moo = import "moo.jsonnet";
local ns = "dunedaq.trigger.triggerzipper";
local s = moo.oschema.schema(ns);

local types = {
    ito: s.number("InputTimeout", "u4",
                  doc="Maximum time in milliseconds to wait to recv input"),
    oto: s.number("OutputTimeout", "u4",
                  doc="Maximum time in milliseconds to wait to send output"),
    lat: s.number("MaxLatency", "u4",
                  doc="Maximum latency to inflict buffered elements"),
    card: s.number("Cardinality", "u4",
                   doc="Expected number of identified streams"),

    // fixme: this should be factored, not copy-pasted
    region_id : s.number("region_id", "u2"),
    element_id : s.number("element_id", "u4"),

    conf : s.record("ConfParams", [


        s.field("links", self.linkvec,
                doc="List of link identifiers that may be included into trigger decision"),

    s.field("initial_token_count", self.token_count, 0,
      doc="Number of trigger tokens to start the run with"),


  ], doc="ModuleLevelTrigger configuration parameters"),

  
};

moo.oschema.sort_select(types, ns)
