#!/usr/bin/env luajit

local upipe = require "upipe"

require "upump-ev"
require "upipe-modules"
require "upipe-av"
require "upipe-ts"
require "upipe-framers"
require "upipe-filters"
require "upipe-swscale"

local UPROBE_LOG_LEVEL = UPROBE_LOG_INFO

local src_path = assert(arg[1])
local dst_path = assert(arg[2])

-- default probe
local probe, upump_mgr = upipe.default_probe(UPROBE_LOG_LEVEL)

local function pfx(tag)
    return uprobe.pfx(UPROBE_LOG_LEVEL, tag)
end

-- null pipe
local null = upipe.null():new(pfx("null") .. probe)

-- file source
local source = upipe.fsrc():new(pfx("src") .. probe)
if not ubase_check(source:set_uri(src_path)) then
    os.exit(1)
end

local split_pipe

-- other probes
local uref_probe = uprobe {
    probe_uref = function (probe, pipe, ref, pump_p, drop)
        if source then
            -- release the source to exit
            source:release()
            source = nil

            -- send demux output to /dev/null
            split_pipe.output = null
            split_pipe = nil

        else
            -- second (or after) frame, do not output them
            drop[0] = true
        end
    end
}

local avcdec_probe = uprobe {
    need_output = function (probe, pipe, flow_def)
        local hsize = flow_def:pic_flow_get_hsize()
        local sar = flow_def:pic_flow_get_sar()
        if not hsize or not sar then
            pipe:err_va("incompatible flow def")
            return "unhandled"
        end

        local wanted_hsize = (hsize * sar.num / sar.den / 2) * 2

        local ffmt_mgr = upipe.ffmt()
        ffmt_mgr.sws_mgr = upipe.sws()
        ffmt_mgr.deint_mgr = upipe.filter_blend()

        local ffmt_flow = flow_def:sibling_alloc_control()
        ffmt_flow:flow_set_def("pic.")
        ffmt_flow:pic_flow_set_hsize(wanted_hsize)
        ffmt_flow:pic_set_progressive()
        local ffmt = ffmt_mgr:new_flow(pfx("ffmt") .. probe, ffmt_flow)
        ffmt_flow:free()

        local avcenc_flow = flow_def:sibling_alloc_control()
        avcenc_flow:flow_set_def("block.mjpeg.pic.")
        local jpegenc = upipe.avcenc():new_flow(pfx("jpeg") .. probe, avcenc_flow)
        jpegenc:set_option("qmax", "2")
        avcenc_flow:free()

        local urefprobe = upipe.probe_uref():new(
            pfx("uref probe") ..
            uref_probe ..
            probe)

        local fsink = upipe.fsink():new(pfx("sink") .. probe)
        fsink:fsink_set_path(dst_path, "UPIPE_FSINK_OVERWRITE")

        pipe.output = ffmt .. jpegenc .. urefprobe .. fsink
    end
}

local split_probe = uprobe {
    need_output = function (probe, pipe, flow_def)
        split_pipe = pipe
        pipe.output = upipe.avcdec():new(
            pfx("avcdec") ..
            avcdec_probe ..
            probe)
    end
}

upipe_av_init(true, (pfx("av") .. probe):use())

-- ts demux
local ts_demux_mgr = upipe.ts_demux()
ts_demux_mgr.autof_mgr = upipe.autof()

source.output = ts_demux_mgr:new(
    pfx("ts demux") ..
    uprobe.selflow(UPROBE_SELFLOW_VOID, "auto",
        uprobe.selflow(UPROBE_SELFLOW_PIC, "auto",
            split_probe ..
            probe) ..
        probe) ..
    probe)

-- main loop
upump_mgr:run(nil)
