local moo = import "moo.jsonnet";
local ns = "dunedaq.trigger.triggerbitwords";
local s = moo.oschema.schema(ns);

local types = {
  tc_type : s.number("tc_type", "i4", doc="TC type"),
  tc_name : s.string("tc_name", doc="TC name" ),
  
  t0: s.record("t0", [
    s.field("tc_type",    self.tc_type,    default=0,          		doc="The TC type"),
    s.field("tc_name",    self.tc_name,    default="kUnknown", 		        doc="Name for the TC type"),
  ]),

  t1: s.record("t1", [
    s.field("tc_type",    self.tc_type,    default=1,          		doc="The TC type"),
    s.field("tc_name",    self.tc_name,    default="kTiming", 		doc="Name for the TC type"),
  ]),

  t2: s.record("t2", [
    s.field("tc_type",    self.tc_type,    default=2,          		doc="The TC type"),
    s.field("tc_name",    self.tc_name,    default="kTPCLowE", 		doc="Name for the TC type"),
  ]),

  t3: s.record("t3", [
    s.field("tc_type",    self.tc_type,    default=3,          		doc="The TC type"),
    s.field("tc_name",    self.tc_name,    default="kSupernova", 	doc="Name for the TC type"),
  ]),

  t4: s.record("t4", [
    s.field("tc_type",    self.tc_type,    default=4,          		doc="The TC type"),
    s.field("tc_name",    self.tc_name,    default="kRandom", 		doc="Name for the TC type"),
  ]),

  t5: s.record("t5", [
    s.field("tc_type",    self.tc_type,    default=5,          		doc="The TC type"),
    s.field("tc_name",    self.tc_name,    default="kPrescale", 	doc="Name for the TC type"),
  ]),

  t6: s.record("t6", [
    s.field("tc_type",    self.tc_type,    default=6,          		doc="The TC type"),
    s.field("tc_name",    self.tc_name,    default="kADCSimpleWindow", 	doc="Name for the TC type"),
  ]),

  t7: s.record("t7", [
    s.field("tc_type",    self.tc_type,    default=7,          		doc="The TC type"),
    s.field("tc_name",    self.tc_name,    default="kHorizontalMuon", 	doc="Name for the TC type"),
  ]),

  t8: s.record("t8", [
    s.field("tc_type",    self.tc_type,    default=8,          		doc="The TC type"),
    s.field("tc_name",    self.tc_name,    default="kMichelElectron", 	doc="Name for the TC type"),
  ]),

  t9: s.record("t9", [
    s.field("tc_type",    self.tc_type,    default=9,          		doc="The TC type"),
    s.field("tc_name",    self.tc_name,    default="kPlaneCoincidence", doc="Name for the TC type"),
  ]),

  bitwords_map: s.record("bitwords_map", [
    s.field("b0", self.t0, default=self.t0, doc="b0"),
    s.field("b1", self.t1, default=self.t1, doc="b1"),
    s.field("b2", self.t2, default=self.t2, doc="b2"),
    s.field("b3", self.t3, default=self.t3, doc="b3"),
    s.field("b4", self.t4, default=self.t4, doc="b4"),
    s.field("b5", self.t5, default=self.t5, doc="b5"),
    s.field("b6", self.t6, default=self.t6, doc="b6"),
    s.field("b7", self.t7, default=self.t7, doc="b7"),
    s.field("b8", self.t8, default=self.t8, doc="b8"),
    s.field("b9", self.t9, default=self.t9, doc="b9"),
]),

  conf : s.record("ConfParams", [
      s.field("td_bitwords_map", self.bitwords_map, [], doc="12345"),
  ], doc="Trigger bitwords configuration parameters"),
  
};

moo.oschema.sort_select(types, ns)
