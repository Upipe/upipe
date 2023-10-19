local ffi = require "ffi"
local stdarg = ffi.load("ffi-stdarg")

ffi.cdef [[
    intptr_t ffi_va_arg(va_list *ap, const char *type);
    void ffi_va_copy(va_list *args, void (*cb)(va_list *args));
]]

local void_cb = ffi.typeof("void (*)(void *)")
local va_list_p = ffi.typeof("va_list[1]")
local cb = void_cb(function () end)

local is_number = {
    int = true,
    ["unsigned int"] = true,
    ["signed int"] = true,
    uint32_t = true,
}

local function va_start(va_list)
    if ffi.arch == "x64" or ffi.arch == "arm64" then
        return va_list
    end
    return va_list_p(va_list)
end

return {
    va_start = va_start,

    va_args = function (args, ...)
        local ret = { }
        local n = select("#", ...)
        for i = 1, n do
            local ty = select(i, ...)
            local val = stdarg.ffi_va_arg(args, ty)
            if is_number[ty] then
                ret[i] = tonumber(val)
            elseif ty == "const char *" then
                ret[i] = val ~= 0 and ffi.string(ffi.cast("const char *", val)) or nil
            else
                ret[i] = ffi.cast(select(i, ...), val)
            end
        end
        return unpack(ret)
    end,

    va_copy = function (va_list, func)
        cb:set(func)
        stdarg.ffi_va_copy(va_start(va_list), cb)
    end
}
