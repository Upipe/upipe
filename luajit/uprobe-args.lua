return {
    -- upipe
    LOG                   = { "enum uprobe_log_level", "const char *" },
    FATAL                 = { "int" },
    ERROR                 = { "int" },
    SINK_END              = { "const char *" },
    NEED_OUTPUT           = { "struct uref *" },
    PROVIDE_REQUEST       = { "struct urequest *" },
    NEED_UPUMP_MGR        = { "struct upump_mgr **" },
    NEW_FLOW_DEF          = { "struct uref *" },
    NEW_RAP               = { "struct uref *" },
    CLOCK_REF             = { "struct uref *", "uint64_t", "int" },
    CLOCK_TS              = { "struct uref *" },
    CLOCK_UTC             = { "struct uref *", "uint64_t" },

    -- upipe-modules
    PROBE_UREF            = { "struct uref *", "bool *" },
    MULTICAT_PROBE_ROTATE = { "struct uref *", "uint64_t" },
    HTTP_SRC_REDIRECT     = { "const char *" },
}
