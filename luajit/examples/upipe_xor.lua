#!/usr/bin/env luajit

local ffi = require "ffi"
local upipe = require "upipe"

require "upump-ev"
require "upipe-modules"

ffi.cdef [[ FILE *stderr; ]]

local UPROBE_LOG_LEVEL = UPROBE_LOG_DEBUG
local UMEM_POOL = 512
local UDICT_POOL_DEPTH = 500
local UREF_POOL_DEPTH = 500
local UBUF_POOL_DEPTH = 3000
local UBUF_SHARED_POOL_DEPTH = 50
local UPUMP_POOL = 10
local UPUMP_BLOCKER_POOL = 10

if #arg ~= 2 then
    io.stderr:write("Usage: ", arg[0], " <input> <output>\n")
    os.exit(1)
end

local input, output = arg[1], arg[2]

-- managers
local upump_mgr = upump.ev_default(UPUMP_POOL, UPUMP_BLOCKER_POOL)
local umem_mgr = umem.pool_simple(UMEM_POOL)
local udict_mgr = udict.inline(UDICT_POOL_DEPTH, umem_mgr, -1, -1)
local uref_mgr = uref.std(UREF_POOL_DEPTH, udict_mgr, 0)

local upipe_xor_mgr = upipe {
    -- use upipe_helper_output
    output = true,

    input = function (pipe, ref, pump)
        -- get size
        local size = ffi.new("size_t[1]")
        ubase_assert(ref:block_size(size))
        size = tonumber(size[0])

        local offset = 0
        local size_p = ffi.new("int[1]")
        local buffer_p = ffi.new("uint8_t *[1]")
        while offset < size do
            size_p[0] = size - offset
            ubase_assert(ref:block_write(offset, size_p, buffer_p))

            -- xor buffer content
            local buf = buffer_p[0]
            for i = 0, size_p[0] - 1 do
                buf[i] = bit.bxor(buf[i], 0x42)
            end

            ubase_assert(ref:block_unmap(offset))
            offset = offset + size_p[0]
        end

        -- output uref
        pipe:helper_output(ref, pump)
    end,

    control = {
        set_flow_def = function (pipe, flow_def)
            if not ubase_check(flow_def:flow_match_def("block.")) then
                return "invalid"
            end
            pipe:helper_store_flow_def(flow_def:dup())
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
    return uprobe.pfx(UPROBE_LOG_LEVEL, tag) .. probe
end

-- pipes
local src = upipe.fsrc():new(pfx "src")
local xor = upipe_xor_mgr:new(pfx "xor")
local sink = upipe.fsink():new(pfx "sink")

src.uri = input
ubase_assert(sink:fsink_set_path(output, "UPIPE_FSINK_CREATE"))
src.output = xor .. sink

-- main loop
upump_mgr:run(nil)
