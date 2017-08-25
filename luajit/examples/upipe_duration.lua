#!/usr/bin/env luajit

local ffi = require "ffi"
local upipe = require "upipe"

require "upump-ev"
require "upipe-modules"
require "upipe-ts"
require "upipe-framers"

ffi.cdef [[ FILE *stderr; ]]

local UPROBE_LOG_LEVEL = UPROBE_LOG_NOTICE
local UMEM_POOL = 512
local UDICT_POOL_DEPTH = 500
local UREF_POOL_DEPTH = 500
local UBUF_POOL_DEPTH = 3000
local UBUF_SHARED_POOL_DEPTH = 50
local UPUMP_POOL = 10
local UPUMP_BLOCKER_POOL = 10

if #arg ~= 1 then
    io.stderr:write("Usage: ", arg[0], " <filename>\n")
    os.exit(1)
end

local file = arg[1]

-- managers
local upump_mgr = upump.ev_default(UPUMP_POOL, UPUMP_BLOCKER_POOL)
local umem_mgr = umem.pool_simple(UMEM_POOL)
local udict_mgr = udict.inline(UDICT_POOL_DEPTH, umem_mgr, -1, -1)
local uref_mgr = uref.std(UREF_POOL_DEPTH, udict_mgr, 0)

local count_mgr = upipe {
    init = function (pipe)
        pipe.props.duration = 0
    end,

    input = function (pipe, ref, pump)
        local duration = ref:clock_get_duration()
        if duration then
            pipe.props.duration = pipe.props.duration + duration
        end
        ref:free()
    end,

    control = {
        set_flow_def = function (pipe, flow_def)
            flow_def:dump(pipe.uprobe)
        end
    }
}

-- probes
local probe =
    uprobe.ubuf_mem(umem_mgr, UBUF_POOL_DEPTH, UBUF_SHARED_POOL_DEPTH) ..
    uprobe.upump_mgr(upump_mgr) ..
    uprobe.uref_mgr(uref_mgr) ..
    uprobe.stdio(ffi.C.stderr, UPROBE_LOG_LEVEL)

local function pfx(tag)
    return uprobe.pfx(UPROBE_LOG_LEVEL, tag)
end

-- pipes
local src = upipe.fsrc():new(pfx("src") .. probe)
if not ubase_check(src:set_uri(file)) then
    io.stderr:write("invalid file\n")
    os.exit(1)
end

local sink = count_mgr:new(pfx("count") .. probe)

local ts_demux_mgr = upipe.ts_demux()
ts_demux_mgr.autof_mgr = upipe.autof()

src.output = ts_demux_mgr:new(
    pfx("ts demux") ..
    uprobe.selflow(UPROBE_SELFLOW_VOID, "auto",
        uprobe.selflow(UPROBE_SELFLOW_PIC, "auto",
            uprobe {
                new_flow_def = function (probe, pipe, flow_def)
                    pipe.output = sink
                end } ..
            probe) ..
        probe) ..
    probe)

-- main loop
upump_mgr:run(nil)

print(string.format("%.2f", tonumber(sink.props.duration) / UCLOCK_FREQ))
