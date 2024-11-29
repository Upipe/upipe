#!/usr/bin/env luajit

local upipe = require "upipe"

require "upump-ev"
require "upipe-modules"
require "upipe-openssl"

local UPROBE_LOG_LEVEL = UPROBE_LOG_INFO

if #arg ~= 1 then
    io.stderr:write("Usage: ", arg[0], " <url>\n")
    os.exit(1)
end

local url = arg[1]

-- probes
local probe, upump_mgr = upipe.default_probe(UPROBE_LOG_LEVEL)
local probe_https = uprobe.https_openssl()

local function pfx(tag)
    return uprobe.pfx(UPROBE_LOG_LEVEL, tag)
end

-- pipes
local sink = upipe.fsink():new(pfx("sink") .. probe)
ubase_assert(sink:fsink_set_path("/dev/stdout", "UPIPE_FSINK_OVERWRITE"))

local src = upipe.http_src():new(pfx("http") .. probe_https .. probe)
src.uri = url
src.output = sink

-- main loop
upump_mgr:run(nil)
