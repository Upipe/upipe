local ffi = require "ffi"
local C = ffi.C

return {
    -- global commands
    [C.UPIPE_GET_URI]            = { "const char **" },
    [C.UPIPE_SET_URI]            = { "const char *" },
    [C.UPIPE_GET_OPTION]         = { "const char *", "const char **" },
    [C.UPIPE_SET_OPTION]         = { "const char *", "const char *" },

    -- input commands
    [C.UPIPE_REGISTER_REQUEST]   = { "struct urequest *" },
    [C.UPIPE_UNREGISTER_REQUEST] = { "struct urequest *" },
    [C.UPIPE_SET_FLOW_DEF]       = { "struct uref *" },
    [C.UPIPE_GET_MAX_LENGTH]     = { "unsigned int *" },
    [C.UPIPE_SET_MAX_LENGTH]     = { "unsigned int" },

    -- output commands
    [C.UPIPE_GET_OUTPUT]         = { "struct upipe **" },
    [C.UPIPE_SET_OUTPUT]         = { "struct upipe *" },
    [C.UPIPE_GET_FLOW_DEF]       = { "struct uref **" },
    [C.UPIPE_GET_OUTPUT_SIZE]    = { "unsigned int *" },
    [C.UPIPE_SET_OUTPUT_SIZE]    = { "unsigned int" },

    -- split elements commands
    [C.UPIPE_SPLIT_ITERATE]      = { "struct uref **" },

    -- sub/super pipes commands
    [C.UPIPE_GET_SUB_MGR]        = { "struct upipe_mgr **" },
    [C.UPIPE_ITERATE_SUB]        = { "struct upipe **" },
    [C.UPIPE_SUB_GET_SUPER]      = { "struct upipe **" },

    -- source commands
    [C.UPIPE_SRC_GET_SIZE]       = { "uint64_t *" },
    [C.UPIPE_SRC_GET_POSITION]   = { "uint64_t *" },
    [C.UPIPE_SRC_SET_POSITION]   = { "uint64_t" },
    [C.UPIPE_SRC_GET_RANGE]      = { "uint64_t", "uint64_t" },
    [C.UPIPE_SRC_SET_RANGE]      = { "uint64_t", "uint64_t" },
}
