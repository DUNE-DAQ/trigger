local moo = import "moo.jsonnet";
local ns = "dunedaq.trigger.moduleleveltrigger";
local s = moo.oschema.schema(ns);
local nc = moo.oschema.numeric_constraints;

local types = {
  group_id: s.number("group_id", "i4"),
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
  number_of_groups: s.number(  "Ngroups",       "i4", nc(minimum=0, maximum=150), doc="Number of groups of detector links to readout, for ROI config"),
  probability:      s.number(  "Prob",          "f4", nc(minimum=0.0, maximum=1.0), doc="Probability to read out a group of links, for ROI config"),
  group_selection:  s.enum(    "GroupSelection", ["kRandom", "kSequential"]),

  sourceid : s.record("SourceID", [
      s.field("element", self.element_id, doc="" ),
      s.field("subsystem", self.subsystem, doc="" )],
      doc="SourceID"),

  tc_readout: s.record( "tc_readout", [
    s.field("candidate_type", self.tc_type,      default=0,     doc="The TC type, 0=Unknown"),
    s.field("time_before",    self.readout_time, default=1000, doc="Time to readout before TC time [ticks]"),
    s.field("time_after",     self.readout_time, default=1001, doc="Time to readout after TC time [ticks]"),
  ]),

  tc_readout_map: s.sequence( "tc_readout_map", self.tc_readout),

  linkvec : s.sequence("link_vec", self.sourceid),
  grouplink: s.record("group_link", [
    s.field("group", self.group_id, doc="ID / group"),
    s.field("links", self.linkvec, doc="List of associated elements"),
  ]),
  grouplinks: s.sequence("group_links", self.grouplink),

  roi_group_conf: s.record("roi_group_conf", [
    s.field("number_of_link_groups", self.number_of_groups, default=1,         doc="Number of groups of links to readout"),
    s.field("probability",           self.probability,      default=0.1,       doc="Probability to select this configuration [0 to 1]"),
    s.field("time_window",           self.readout_time,     default=1000,      doc="Time window to read out pre/post decision, [clock ticks]"),
    s.field("groups_selection_mode", self.group_selection,  default="kRandom", doc="Whether to read out random groups or in sequence"),
  ]),

  roi_conf_map: s.sequence("roi_conf_map", self.roi_group_conf),
 
  conf : s.record("ConfParams", [
      s.field("mandatory_links", self.linkvec, doc="List of link identifiers that will be included in trigger decision"),
      s.field("groups_links", self.grouplinks, doc="List of link identifiers that may be included in trigger decision"),
      s.field("merge_overlapping_tcs", self.flag, default=true, doc="Flag to allow(true)/disable(false) merging of overlapping TCs when forming TD"),
      s.field("td_out_of_timeout", self.flag, default=true, doc="Option to send TD if TC comes out of timeout window (late, overlapping already sent TD"),
      s.field("buffer_timeout", self.time_t, 100, doc="Buffering timeout [ms] for new TCs"),
      s.field("td_readout_limit", self.time_t, 1000, doc="Time limit [ms] for the length of TD readout window"),
      s.field("ignore_tc", self.tc_types, [], doc="List of TC types to be ignored"),
      s.field("use_bitwords", self.flag, default=false, doc="Option to use bitwords (ie trigger types, coincidences) when forming trigger decisions"),
      s.field("trigger_bitwords", self.bitwords, [], doc="Optional dictionary of bitwords to use when forming trigger decisions"),
      s.field("use_roi_readout", self.flag, default=false, doc="Option to use ROI readout in MLT: only readout selection of fragment producers"),
      s.field("roi_conf", self.roi_conf_map, default=[self.roi_group_conf], doc="The configuration (table) for ROI readout"),
      s.field("use_readout_map", self.flag, default=false, doc="Option to use defalt readout windows (tc.time_start and tc.time_end) or a custom readout map from daqconf"),
      s.field("td_readout_map", self.tc_readout_map, default=[self.tc_readout], doc="A map holding readout pre/post depending on TC type"),
      s.field("use_latency_offset", self.flag, default=false, doc="Should an offset be applied to latency measurements (opmon)"),
  ], doc="ModuleLevelTrigger configuration parameters"),
  
};

moo.oschema.sort_select(types, ns)
