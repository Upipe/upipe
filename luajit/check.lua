local ffi = require "ffi"
local upipe = require "upipe"

local errors = 0

local function prefix(e)
    if e == 0 then
        return "\27[32mPASS\27[0m"
    else
        return "\27[31mFAIL\27[0m"
    end
end

for _, module in ipairs(arg) do
    local ok, msg = pcall(require, module)
    if not ok then
        io.stderr:write("ERROR: " .. msg .. "\n")
    end
    local e = ok and 0 or 1
    io.stdout:write(prefix(e) .. ": " .. module .. ".lua\n")
    errors = errors + e
end

local function check_args(name)
    local e = 0
    local args = require(name .. "-args")
    for k, v in pairs(args) do
        for _, t in ipairs(v) do
            if not pcall(ffi.typeof, t) then
                io.stderr:write("ERROR: invalid type: " .. t .. "\n")
                e = e + 1
            end
        end
    end
    io.stdout:write(prefix(e) .. ": " .. name .. "-args.lua\n")
    errors = errors + e
end

check_args "uprobe-event"
check_args "upipe-command"

if errors > 0 then
    os.exit(1)
end
