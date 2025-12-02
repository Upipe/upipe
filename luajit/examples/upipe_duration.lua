#!/usr/bin/env luajit

local upipe = require "upipe"

require "upump-ev"
require "upipe-modules"
require "upipe-ts"
require "upipe-framers"

local UPROBE_LOG_LEVEL = 'UPROBE_LOG_INFO'

if #arg ~= 1 then
    io.stderr:write("Usage: ", arg[0], " <filename>\n")
    os.exit(1)
end

local file = arg[1]

-- managers
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
local probe, upump_mgr = upipe.default_probe(UPROBE_LOG_LEVEL)

local function pfx(tag)
    return uprobe.pfx(UPROBE_LOG_LEVEL, tag)
end

-- pipes
local src = upipe.fsrc():new(pfx("src") .. probe)
src.uri = file

local sink = count_mgr:new(pfx("count") .. probe)

local ts_demux_mgr = upipe.ts_demux()
ts_demux_mgr.autof_mgr = upipe.autof()

src.output = ts_demux_mgr:new(
    pfx("ts demux") ..
    uprobe.selflow('UPROBE_SELFLOW_VOID', "auto",
        uprobe.selflow('UPROBE_SELFLOW_PIC', "auto",
            uprobe {
                need_output = function (probe, pipe, flow_def)
                    pipe.output = sink
                end } ..
            probe) ..
        probe) ..
    probe)

-- main loop
upump_mgr:run(nil)

print(string.format("%.2f", tonumber(sink.props.duration) / UCLOCK_FREQ))
