local moo = import "moo.jsonnet";
local ns = "dunedaq.trigger.moduleleveltrigger";
local s = moo.oschema.schema(ns);

local types = {
  element_id : s.number("element_id_t", "u4"),
  subsystem : s.string("subsystem_t"),
  flag : s.boolean("Boolean", doc="Option for flags, true/false"),
  candidate_type_t : s.number("candidate_type_t", "u4", doc="Candidate type"),
  time_t : s.number("time_t", "i8", doc="Time"),
  tc_type : s.number("tc_type", "i4", doc="TC type"),
  tc_types : s.sequence("tc_types", self.tc_type, doc="List of TC types"),
  readout_time:    s.number(   "ROTime",        "i8", doc="A readout time in ticks"),
  bitword:         s.number(   "Bitword", "i4", doc="An integer representing the TC type, to be set in the trigger bitword."),
  bitword_list:    s.sequence( "BitwordList",   self.bitword, doc="A sequence of bitword (bits) forming a bitword."),
  bitwords:        s.sequence( "Bitwords",      self.bitword_list, doc="List of bitwords to use when forming trigger decisions in MLT" ),
  region:          s.number(   "Region", "u4",  doc="Represents the associated readout region"),
  regions:         s.sequence( "Regions",       self.region, doc="List of regions (allows multi-region association of elements)"),

  tc_readout: s.record("tc_readout", [
    s.field("candidate_type", self.tc_type,        default=0,     doc="The TC type"),
    s.field("time_before",    self.time_t,    default=1000, doc="Time to readout before TC time [ticks]"),
    s.field("time_after",     self.time_t,     default=1001, doc="Time to readout after TC time [ticks]"),
  ]),

  sourceid : s.record("SourceID", [
      s.field("element", self.element_id, doc="" ),
      s.field("subsystem", self.subsystem, doc="" ),
      s.field("regions", self.regions, doc="" )],
      doc="SourceID"),

  c0_readout: s.record("c0_readout", [
    s.field("candidate_type", self.tc_type,      default=0,     doc="The TC type, 0=Unknown"),
    s.field("time_before",    self.readout_time, default=1000, doc="Time to readout before TC time [ticks]"),
    s.field("time_after",     self.readout_time, default=1001, doc="Time to readout after TC time [ticks]"),
  ]),
  c1_readout: s.record("c1_readout", [
    s.field("candidate_type", self.tc_type,      default=1,     doc="The TC type, 1=Timing"),
    s.field("time_before",    self.readout_time, default=1000, doc="Time to readout before TC time [ticks]"),
    s.field("time_after",     self.readout_time, default=1001, doc="Time to readout after TC time [ticks]"),
  ]),
  c2_readout: s.record("c2_readout", [
    s.field("candidate_type", self.tc_type,      default=2,     doc="The TC type, 2=TPCLowE"),
    s.field("time_before",    self.readout_time, default=1000, doc="Time to readout before TC time [ticks]"),
    s.field("time_after",     self.readout_time, default=1001, doc="Time to readout after TC time [ticks]"),
  ]),
  c3_readout: s.record("c3_readout", [
    s.field("candidate_type", self.tc_type,      default=3,     doc="The TC type, 3=Supernova"),
    s.field("time_before",    self.readout_time, default=1000, doc="Time to readout before TC time [ticks]"),
    s.field("time_after",     self.readout_time, default=1001, doc="Time to readout after TC time [ticks]"),
  ]),
  c4_readout: s.record("c4_readout", [
    s.field("candidate_type", self.tc_type,      default=4,     doc="The TC type, 4=Random"),
    s.field("time_before",    self.readout_time, default=1000, doc="Time to readout before TC time [ticks]"),
    s.field("time_after",     self.readout_time, default=1001, doc="Time to readout after TC time [ticks]"),
  ]),
  c5_readout: s.record("c5_readout", [
    s.field("candidate_type", self.tc_type,      default=5,     doc="The TC type, 5=Prescale"),
    s.field("time_before",    self.readout_time, default=1000, doc="Time to readout before TC time [ticks]"),
    s.field("time_after",     self.readout_time, default=1001, doc="Time to readout after TC time [ticks]"),
  ]),
  c6_readout: s.record("c6_readout", [
    s.field("candidate_type", self.tc_type,      default=6,     doc="The TC type, 6=ADCSimpleWindow"),
    s.field("time_before",    self.readout_time, default=1000, doc="Time to readout before TC time [ticks]"),
    s.field("time_after",     self.readout_time, default=1001, doc="Time to readout after TC time [ticks]"),
  ]),
  c7_readout: s.record("c7_readout", [
    s.field("candidate_type", self.tc_type,      default=7,     doc="The TC type, 7=HorizontalMuon"),
    s.field("time_before",    self.readout_time, default=1000, doc="Time to readout before TC time [ticks]"),
    s.field("time_after",     self.readout_time, default=1001, doc="Time to readout after TC time [ticks]"),
  ]),
  c8_readout: s.record("c8_readout", [
    s.field("candidate_type", self.tc_type,      default=8,     doc="The TC type, 8=MichelElectron"),
    s.field("time_before",    self.readout_time, default=1000, doc="Time to readout before TC time [ticks]"),
    s.field("time_after",     self.readout_time, default=1001, doc="Time to readout after TC time [ticks]"),
  ]),
  c9_readout: s.record("c9_readout", [
    s.field("candidate_type", self.tc_type,      default=9,     doc="The TC type, 9=LowEnergyEvent"),
    s.field("time_before",    self.readout_time, default=1000, doc="Time to readout before TC time [ticks]"),
    s.field("time_after",     self.readout_time, default=1001, doc="Time to readout after TC time [ticks]"),
  ]),

  tc_readout_map: s.record("tc_readout_map", [
    s.field("c0", self.c0_readout, default=self.c0_readout, doc="TC readout for TC type 0"),
    s.field("c1", self.c1_readout, default=self.c1_readout, doc="TC readout for TC type 1"),
    s.field("c2", self.c2_readout, default=self.c2_readout, doc="TC readout for TC type 2"),
    s.field("c3", self.c3_readout, default=self.c3_readout, doc="TC readout for TC type 3"),
    s.field("c4", self.c4_readout, default=self.c4_readout, doc="TC readout for TC type 4"),
    s.field("c5", self.c5_readout, default=self.c5_readout, doc="TC readout for TC type 5"),
    s.field("c6", self.c6_readout, default=self.c6_readout, doc="TC readout for TC type 6"),
    s.field("c7", self.c7_readout, default=self.c7_readout, doc="TC readout for TC type 7"),
    s.field("c8", self.c8_readout, default=self.c8_readout, doc="TC readout for TC type 8"),
    s.field("c9", self.c9_readout, default=self.c9_readout, doc="TC readout for TC type 9"),
  ]),

  linkvec : s.sequence("link_vec", self.sourceid),
  
  conf : s.record("ConfParams", [
    s.field("links", self.linkvec,
      doc="List of link identifiers that may be included into trigger decision"),
      s.field("hsi_trigger_type_passthrough", self.flag, default=false, doc="Option to override the trigger type inside MLT"),
      s.field("merge_overlapping_tcs", self.flag, default=true, doc="Flag to allow(true)/disable(false) merging of overlapping TCs when forming TD"),
      s.field("td_out_of_timeout", self.flag, default=true, doc="Option to send TD if TC comes out of timeout window (late, overlapping already sent TD"),
      s.field("buffer_timeout", self.time_t, 100, doc="Buffering timeout [ms] for new TCs"),
      s.field("td_readout_limit", self.time_t, 1000, doc="Time limit [ms] for the length of TD readout window"),
      s.field("ignore_tc", self.tc_types, [], doc="List of TC types to be ignored"),
      s.field("use_bitwords", self.flag, default=false, doc="Option to use bitwords (ie trigger types, coincidences) when forming trigger decisions"),
      s.field("trigger_bitwords", self.bitwords, [], doc="Optional dictionary of bitwords to use when forming trigger decisions"),
      s.field("use_readout_map", self.flag, default=false, doc="Option to use defalt readout windows (tc.time_start and tc.time_end) or a custom readout map from daqconf"),
      s.field("td_readout_map", self.tc_readout_map, self.tc_readout_map, doc="A map holding readout pre/post depending on TC type"),
      s.field("use_roi_readout", self.flag, default=false, doc="Option to use ROI readout in MLT: only readout APA(s) associated with the Trigger Decision"),
  ], doc="ModuleLevelTrigger configuration parameters"),
  
};

moo.oschema.sort_select(types, ns)
