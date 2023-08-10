local ffi = require "ffi"
local stdarg = require "ffi-stdarg"

local va_start = stdarg.va_start
local va_args, va_copy = stdarg.va_args, stdarg.va_copy
local fmt = string.format
local C, cast = ffi.C, ffi.cast
local tohex = bit.tohex

ffi.cdef [[
    // stdlib.h
    void *malloc(size_t);
    void *calloc(size_t, size_t);
    void free(void *);

    // stdio.h
    typedef struct _IO_FILE FILE;

    // time.h
    typedef long time_t;

    // pthread.h
    typedef unsigned long pthread_t;
    typedef union pthread_attr_t pthread_attr_t;
]]

ffi.cdef(fmt('FILE *stderr __asm__("%s")',
    ffi.os == "OSX" and '__stderrp' or 'stderr'))

require "libupipe"
require "upipe-helper"

UCLOCK_FREQ = 27000000

setmetatable(_G, { __index = C })

function ubase_assert(command)
    if not ubase_check(command) then
        local msg = ubase_err_str(command)
        error(msg ~= nil and ffi.string(msg) or "UBASE_ERR_" .. command, 2)
    end
end

local void_ptr = ffi.typeof("void *")
local intptr_t = ffi.typeof("intptr_t")

local function dump_traceback(msg)
    io.stderr:write(debug.traceback(msg, 2), "\n")
end

local props = { }

local function props_key(ptr)
    return tohex(cast(intptr_t, cast(void_ptr, ptr)))
end

local function props_init(ptr)
    props[props_key(ptr)] = {}
end

local function props_dict(ptr)
    return props[props_key(ptr)]
end

local function props_clean(ptr)
    local k = props_key(ptr)
    if props[k]._clean then
        xpcall(props[k]._clean, dump_traceback, ptr)
    end
    return k
end

local function props_destroy(ptr)
    props[props_clean(ptr)] = nil
end

local init = {
    upipe_mgr = function (mgr, cb)
        mgr.upipe_alloc = cb.alloc or
            function (mgr, probe, signature, args)
                local pipe = upipe_alloc(mgr, probe)
                if cb.init then cb.init(pipe) end
                pipe.props._clean = cb.clean
                return C.upipe_use(pipe)
            end
        mgr.upipe_input = cb.input
        mgr.upipe_control = cb.control
    end,

    uclock = function (clock, now, mktime)
        clock.uclock_now = now
        clock.uclock_mktime = mktime
    end
}

local function alloc(ty)
    local st = ffi.typeof("struct " .. ty)
    local ct = ffi.typeof("struct { struct urefcount refcount; $ data; }", st)
    return function (...)
        local cd = ffi.cast(ffi.typeof("$ *", ct), C.malloc(ffi.sizeof(ct)))
        local args = { ... }
        for i, arg in ipairs(args) do
            if ffi.istype("struct uprobe", arg) then
                args[i] = C.uprobe_use(arg)
            end
        end
        local cb
        cb = ffi.cast("urefcount_cb", function (refcount)
            local data = ffi.cast(ffi.typeof("$ *", st),
                ffi.cast("char *", refcount) + ffi.offsetof(ct, "data"))
            props_destroy(data)
            if ty ~= "upipe_mgr" and ty ~= "uclock" then
                C[ty .. "_clean"](data)
            end
            if ty == "uprobe" then
                data.uprobe_throw:free()
            end
            C.free(ffi.cast("void *", refcount))
            cb:free()
        end)
        props_init(cd.data);
        local f = init[ty] or C[ty .. "_init"]
        f(cd.data, unpack(args))
        cd.refcount:init(cb)
        cd.data.refcount = cd.refcount
        return ffi.gc(cd.data, C[ty .. "_release"])
    end
end

local uprobe_alloc = alloc "uprobe"
upipe_alloc = alloc "upipe"
uclock_alloc = alloc "uclock"

local mgr_alias = {
    upump = {
        ev_default = { "ev", "default" }
    },
    umem = {
        pool_simple = { "pool", "simple" }
    }
}

local mgr_mt = {
    __index = function (ctx, key)
        local sym_alloc = fmt("%s_%s_mgr_alloc", ctx.name, key)
        local sym_release = fmt("%s_mgr_release", ctx.name)
        local alias = (mgr_alias[ctx.name] or { })[key]
        if alias then
            sym_alloc = fmt("%s_%s_mgr_alloc_%s", ctx.name, alias[1], alias[2])
        end
        return function (...)
            return ffi.gc(C[sym_alloc](...), C[sym_release])
        end
    end
}

for _, name in ipairs { 'upump', 'udict', 'uref', 'umem', 'ubuf' } do
    _G[name] = setmetatable({ name = name }, mgr_mt)
end

local probe_args = require "uprobe-event-args"

local function ubase_err(ret)
    return type(ret) == "string" and C["UBASE_ERR_" .. ret:upper()] or ret or C.UBASE_ERR_NONE
end

uprobe = setmetatable({ }, {
    __call = function (_, uprobe_throw)
        if type(uprobe_throw) ~= 'function' then
            local events = { }
            for k, v in pairs(uprobe_throw) do
                local k = k:upper()
                if k == "PROVIDE_REQUEST" and type(v) ~= 'function' then
                    local events = { }
                    for k, v in pairs(v) do
                        events[C["UREQUEST_" .. k:upper()]] = { func = v }
                    end
                    v = function (probe, pipe, request, args)
                        local e = events[request.type]
                        if e then
                            return ubase_err(e.func(probe, pipe, request))
                        end
                        return probe:throw_next(pipe, C.UPROBE_PROVIDE_REQUEST, args)
                    end
                end
                events[C["UPROBE_" .. k]] = { func = v, args = probe_args[k] }
            end
            uprobe_throw = function (probe, pipe, event, va_list)
                local e = events[event]
                if not e then return probe:throw_next(pipe, event, va_list) end

                local ret = C.UBASE_ERR_UNHANDLED
                va_copy(va_list, function (args)
                    if event >= C.UPROBE_LOCAL then
                        local signature = va_args(args, "uint32_t")
                        if signature ~= pipe.mgr.signature then
                            return
                        end
                    end
                    local function get_probe_args(args_list)
                        if not args_list then return args end
                        local vals = {va_args(args, unpack(args_list))}
                        table.insert(vals, va_list)
                        return unpack(vals)
                    end
                    ret = ubase_err(e.func(probe, pipe, get_probe_args(e.args)))
                end)
                return ret
            end
        end
        return uprobe_alloc(uprobe_throw, ffi.cast("struct uprobe *", nil))
    end,
    __index = function (_, key)
        local sym = fmt("uprobe_%s_alloc", key)
        return function (...)
            local args = { ... }
            for i, arg in ipairs(args) do
                if ffi.istype("struct uprobe", arg) then
                    args[i] = C.uprobe_use(arg)
                end
            end
            if key == 'selflow' then
                table.insert(args, 1, args[3])
                table.remove(args, 4)
            end
            return ffi.gc(C[sym](nil, unpack(args)), C.uprobe_release)
        end
    end
})

uclock = setmetatable({ }, {
    __index = function (_, key)
        return function (...)
            return ffi.gc(C[fmt("uclock_%s_alloc", key)](...), C.uclock_release)
        end
    end
})

local upipe_getters = require "upipe-getters"
local uref_getters = require "uref-getters"

local function create_getter(f, t)
    return function (self, arg)
        if arg ~= nil then return f(self, arg) end
        local arg_p = ffi.new(t .. "[1]")
        local ret = f(self, arg_p)
        if not C.ubase_check(ret) then
            return nil, ret
        end
        return arg_p[0]
    end
end

local function getter(getters, f, key)
    if not getters[key] then return f end
    return create_getter(f, getters[key])
end

local function fourcc(n1, n2, n3, n4)
    n1 = type(n1) == 'string' and n1:byte(1) or n1
    n2 = type(n2) == 'string' and n2:byte(1) or n2
    n3 = type(n3) == 'string' and n3:byte(1) or n3
    n4 = type(n4) == 'string' and n4:byte(1) or n4
    return n4*2^24 + n3*2^16 + n2*2^8 + n1
end

local sigs = { }
function upipe_sig(name, n1, n2, n3, n4)
    sigs[fourcc(n1, n2, n3, n4)] = name
end

local sig = {
    void = fourcc('v','o','i','d'),
    flow = fourcc('f','l','o','w'),
    rtp_fec = fourcc('r','f','c',' '),
}

ffi.metatype("struct upipe_mgr", {
    __index = function (mgr, key)
        if key == 'props' then
            return props_dict(mgr)
        end
        if key == 'new' then key = 'new_void' end
        local mgr_sig = key:match("^new_(.*)")
        if mgr_sig then
            return function (mgr, probe, ...)
                local pipe = C.upipe_alloc(mgr, C.uprobe_use(probe), sig[mgr_sig] or 0, ...)
                assert(pipe ~= nil, "upipe_alloc failed")
                return ffi.gc(pipe, C.upipe_release)
            end
        end
        return C[fmt("upipe_%s_mgr_%s", sigs[mgr.signature], key)]
    end,
    __newindex = function (mgr, key, val)
        local sym = fmt("upipe_%s_mgr_set_%s", sigs[mgr.signature], key)
        C[sym](mgr, val)
    end
})

ffi.metatype("struct upump_mgr", {
    __index = function (mgr, key)
        local alloc_type = key:match("^new_(.*)")
        if alloc_type then
            return function (...)
                local alloc_func = "upump_alloc_" .. alloc_type
                local pump = C[alloc_func](...)
                assert(pump ~= nil, alloc_func .. " failed")
                return pump
            end
        end
        return C["upump_mgr_" .. key]
    end
})

local function iterator(pipe, f, t)
    local item_p = ffi.new(t .. "[1]")
    return function ()
        assert(C.ubase_check(f(pipe, item_p)))
        return item_p[0] ~= nil and item_p[0] or nil
    end
end

local upipe_methods = {
    new = function (pipe, probe)
        local pipe = C.upipe_void_alloc_sub(pipe, C.uprobe_use(probe))
        assert(pipe ~= nil, "upipe_void_alloc_sub failed")
        return ffi.gc(pipe, C.upipe_release)
    end,
    new_flow = function (pipe, probe, flow)
        local pipe = C.upipe_flow_alloc_sub(pipe, C.uprobe_use(probe), flow)
        assert(pipe ~= nil, "upipe_flow_alloc_sub failed")
        return ffi.gc(pipe, C.upipe_release)
    end,
    release = function (pipe)
        C.upipe_release(ffi.gc(pipe, nil))
    end,
    iterate_sub = function (pipe, p)
        local f = C.upipe_iterate_sub
        return p and f(pipe, p) or iterator(pipe, f, "struct upipe *")
    end,
    split_iterate = function (pipe, p)
        local f = C.upipe_split_iterate
        return p and f(pipe, p) or iterator(pipe, f, "struct uref *")
    end,
    iterate = function (pipe, f, t)
        if type(f) == 'string' then f = pipe[f] end
        return iterator(pipe, f, t)
    end,
}

ffi.metatype("struct upipe", {
    __index = function (pipe, key)
        if key == 'props' then
            return props_dict(pipe)
        elseif key == 'helper' then
            return props_dict(pipe).helper
        end
        local m = upipe_methods[key]
        if m then return m end
        local f
        local props = props_dict(pipe)
        if props and props._control and props._control[key] then
            f = props._control[key]
        else
            f = C["upipe_" .. key]
        end
        return getter(upipe_getters, f, key)
    end,
    __newindex = function (pipe, key, val)
        local sym = "upipe_set_" .. key
        assert(C.ubase_check(C[sym](pipe, val)), sym)
    end,
    __concat = function (pipe, next_pipe)
        local last = pipe
        local output = ffi.new("struct upipe *[1]")
        while C.ubase_check(last:get_output(output)) and output[0] ~= nil do
            last = output[0]
        end
        last.output = next_pipe
        return pipe
    end,
})

--[[
-- A function to iterate over all the planes of a uref.  For each plane it
-- returns the plane string (chroma for pic, channel for sound).
--]]
local function foreach_plane(func)
    return function(ref)
        local plane = ffi.new("const uint8_t *[1]")
        return function()
            if C.ubase_check(func(ref, plane)) and plane[0] ~= nil then
                return ffi.string(plane[0])
            else
                return nil
            end
        end
    end
end

ffi.metatype("struct uref", {
    __index = function (_, key)
        if key == "pic_foreach_plane" then
            return foreach_plane(C.uref_pic_iterate_plane)
        elseif key == "sound_foreach_plane" then
            return foreach_plane(C.uref_sound_iterate_plane)
        end
        local f = C["uref_" .. key]
        return getter(uref_getters, f, key)
    end
})

ffi.metatype("struct ubuf", {
    __index = function (_, key)
        return C["ubuf_" .. key]
    end
})

ffi.metatype("struct upump", {
    __index = function (pump, key)
        return C["upump_" .. key]
    end
})

ffi.metatype("struct urefcount", {
    __index = function (_, key)
        return C["urefcount_" .. key]
    end
})

ffi.metatype("struct uprobe", {
    __index = function (_, key)
        return C["uprobe_" .. key]
    end,
    __concat = function (probe, next_probe)
        local last = probe
        while last.next ~= nil do last = last.next end
        last.next = C.uprobe_use(next_probe)
        return probe
    end
})

ffi.metatype("struct uclock", {
    __index = function (clock, key)
        if key == 'props' then
            return props_dict(clock)
        end
        return C["uclock_" .. key]
    end
})

ffi.metatype("struct urequest", {
    __index = function (_, key)
        return C["urequest_" .. key]
    end
})

ffi.metatype("struct umem_mgr", {
    __index = function (_, key)
        return C["umem_mgr_" .. key]
    end
})

ffi.metatype("struct uref_mgr", {
    __index = function (_, key)
        local alloc_type = key:match("^new_(.*)")
        if alloc_type then
            return function (...)
                local alloc_func = fmt("uref_%s_alloc", alloc_type)
                local ref = C[alloc_func](...)
                assert(ref ~= nil, alloc_func .. " failed")
                return ref
            end
        end
        return C["uref_mgr_" .. key]
    end
})

local udict_getters = {}
local function udict_getter(udict_type, ctype, number)
    local cfunc = C[string.format("udict_get_%s", udict_type)]
    local lfunc = function(udict, type, name, arg)
        if arg ~= nil then
            return cfunc(udict, arg, type, name)
        end

        local value = ffi.new(ctype .. "[1]")
        local ret = cfunc(udict, value, type, name)
        if not C.ubase_check(ret) then
            return nil, ret
        end

        if number then
            return tonumber(value[0])
        else
            return ffi.new(ctype, value[0])
        end
    end

    udict_getters[C["UDICT_TYPE_"..udict_type:upper()]] = lfunc
end

udict_getter("opaque",         "struct udict_opaque")
udict_getter("string",         "const char *")
udict_getter("bool",           "bool")
udict_getter("rational",       "struct urational")
udict_getter("small_unsigned", "uint8_t",  true)
udict_getter("small_int",      "int8_t",   true)
udict_getter("unsigned",       "uint64_t")
udict_getter("int",            "int64_t")
udict_getter("float",          "double",   true)
-- void
udict_getters[C.UDICT_TYPE_VOID] = function(udict, type, name, arg)
    if arg ~= nil then
        return C.udict_get_void(udict, arg, type, name)
    end

    local value = ffi.new("void *[1]")
    local ret = C.udict_get_void(udict, value, type, name)
    if not C.ubase_check(ret) then
        return nil, ret
    end
    return nil
end

--[[
-- A function to iterate over all the attributes stored in a udict.
-- For each attribute it returns:
-- * the name (or shorthand name) of the attribute (type: const char *)
-- * the value of the attribute (type: depends on the base type)
-- * the real type value (type: enum udict_type)
--
-- The values for the name and type will be invalid on the next iteration so
-- make a copy if they need to be kept.
--]]
local function udict_foreach_attribute(dict)
    local name_real = ffi.new("const char *[1]")
    local type_real = ffi.new("enum udict_type [1]", C.UDICT_TYPE_END)
    local name_shorthand = ffi.new("const char *[1]")
    local type_base = ffi.new("enum udict_type [1]")

    return function()
        if C.ubase_check(C.udict_iterate(dict, name_real, type_real)) and type_real[0] ~= C.UDICT_TYPE_END then
            local name = name_real[0]
            local type = tonumber(type_real[0])
            local value, err

            if type >= C.UDICT_TYPE_SHORTHAND then
                ubase_assert(C.udict_name(dict, type, name_shorthand, type_base))
                name = name_shorthand[0]
                value, err = udict_getters[tonumber(type_base[0])](dict, type, nil)
            else
                value, err = udict_getters[type](dict, type, name)
            end
            -- TODO: check for error

            return ffi.string(name), value, type
        else
            return nil
        end
    end
end

ffi.metatype("struct udict", {
    __index = function (_, key)
        if key == "foreach_attribute" then
            return udict_foreach_attribute
        end
    end
})

local function container_of(ptr, ct, member)
    if type(ct) == 'string' then ct = ffi.typeof(ct) end
    return ffi.cast(ffi.typeof("$ *", ct), ffi.cast("char *", ptr) - ffi.offsetof(ct, member))
end

local ctrl_args = require "upipe-command-args"

local function control_args(cmd, args)
    if not ctrl_args[cmd] then return args end
    return va_args(args, unpack(ctrl_args[cmd]))
end

local function upipe_helper_alloc(cb)
    local ct = ffi.typeof("struct upipe_helper_mgr")
    local h_mgr = ffi.cast(ffi.typeof("$ *", ct), C.calloc(1, ffi.sizeof(ct)))

    if cb.input_output then
        h_mgr.output = cb.input_output
    end

    local _control = {}

    local mgr = h_mgr.mgr
    props_init(mgr)
    mgr.upipe_alloc = cb.alloc or
        function (mgr, probe, signature, args)
            local ct = ffi.typeof("struct upipe_helper")
            local h_pipe = ffi.cast(ffi.typeof("$ *", ct), C.malloc(ffi.sizeof(ct)))
            local pipe = ffi.gc(h_pipe.upipe, C.upipe_release)
            pipe:init(mgr, probe)
            pipe:helper_init_urefcount()
            pipe:helper_init_output()
            pipe:helper_init_output_size(cb.output_size or 0)
            pipe:helper_init_input()
            pipe:helper_init_uclock()
            pipe:helper_init_upump_mgr()
            pipe:helper_init_uref_mgr()
            pipe:helper_init_ubuf_mgr()
            pipe:helper_init_bin_input()
            pipe:helper_init_bin_output()
            pipe:helper_init_sync()
            pipe:helper_init_uref_stream()
            pipe:helper_init_flow_def()
            pipe:helper_init_upump()
            props_init(pipe)
            pipe.props.helper = h_pipe
            pipe:throw_ready()
            if cb.sub_mgr then
                cb.sub_mgr.props._super = pipe
                pipe.props._sub_mgr = cb.sub_mgr
                pipe.props._subpipes = {}
            end
            if cb.sub then
                local super = pipe:sub_get_super()
                table.insert(super.props._subpipes, h_pipe)
            end
            if cb.init then cb.init(pipe, args) end
            pipe.props._control = _control
            pipe.props._clean = cb.clean
            return pipe:use()
        end

    local function wrap_traceback(f)
        return function (...)
            local ret = {xpcall(f, dump_traceback, ...)}
            local success = table.remove(ret, 1)
            if not success then return C.UBASE_ERR_UNKNOWN end
            return unpack(ret)
        end
    end

    if cb.input_output then
        mgr.upipe_input = function (pipe, ref, pump_p)
            if not C.upipe_helper_check_input(pipe) then
                C.upipe_helper_hold_input(pipe, ref)
                C.upipe_helper_block_input(pipe, pump_p)
                return
            end
            local success, ret = xpcall(cb.input_output,
                dump_traceback, pipe, ref, pump_p)
            if success and not ret then
                C.upipe_helper_hold_input(pipe, ref)
                C.upipe_helper_block_input(pipe, pump_p)
                C.upipe_use(pipe)
            end
        end
    else
        mgr.upipe_input = function (pipe, ref, pump_p)
            xpcall(cb.input, dump_traceback, pipe, ref, pump_p)
        end
    end

    if cb.commands then
        for i, comm in ipairs(cb.commands) do
            local v = C.UPIPE_CONTROL_LOCAL + i - 1
            ffi.cdef(fmt("enum { UPIPE_%s = %d };", comm[1]:upper(), v))
            ctrl_args[v] = { unpack(comm, 2) }
        end
    end

    if type(cb.control) == "function" then
        mgr.upipe_control = wrap_traceback(cb.control)
    else
        local control = { }

        if not cb.bin_input and not cb.bin_output then
            control[C.UPIPE_REGISTER_REQUEST] = C.upipe_throw_provide_request
            control[C.UPIPE_UNREGISTER_REQUEST] = C.UBASE_ERR_NONE
        end

        if cb.output then
            control[C.UPIPE_SET_OUTPUT] = C.upipe_helper_set_output
            control[C.UPIPE_GET_OUTPUT] = C.upipe_helper_get_output
            control[C.UPIPE_REGISTER_REQUEST] = C.upipe_helper_alloc_output_proxy
            control[C.UPIPE_UNREGISTER_REQUEST] = C.upipe_helper_free_output_proxy
            control[C.UPIPE_GET_FLOW_DEF] = C.upipe_helper_get_flow_def
        end

        if cb.output_size then
            control[C.UPIPE_SET_OUTPUT_SIZE] = C.upipe_helper_set_output_size
            control[C.UPIPE_GET_OUTPUT_SIZE] = C.upipe_helper_get_output_size
        end

        if cb.upump_mgr then
            control[C.UPIPE_ATTACH_UPUMP_MGR] = C.upipe_helper_attach_upump_mgr
        end

        if cb.sub_mgr then
            _control.get_sub_mgr = function (pipe, mgr_p)
                mgr_p[0] = pipe.props._sub_mgr
                return C.UBASE_ERR_NONE
            end
            _control.iterate_sub = function (pipe, sub_p)
                if sub_p[0] == nil then
                    if pipe.props._subpipes[1] then
                        sub_p[0] = pipe.props._subpipes[1].upipe
                    end
                else
                    for i, sub in ipairs(pipe.props._subpipes) do
                        if sub.upipe == sub_p[0] then
                            local next_sub = pipe.props._subpipes[i + 1]
                            sub_p[0] = next_sub and next_sub.upipe or nil
                            break
                        end
                    end
                end
                return C.UBASE_ERR_NONE
            end
            control[C.UPIPE_GET_SUB_MGR] = _control.get_sub_mgr
            control[C.UPIPE_ITERATE_SUB] = _control.iterate_sub
        end

        if cb.sub then
            _control.sub_get_super = function (pipe, super_p)
                super_p[0] = pipe.mgr.props._super
                return C.UBASE_ERR_NONE
            end
            control[C.UPIPE_SUB_GET_SUPER] = _control.sub_get_super
        end

        if cb.control then
            for k, v in pairs(cb.control) do
                control[C["UPIPE_" .. k:upper()]] = v
                _control[k] = function (...) return ubase_err(v(...)) end
            end
        end

        mgr.upipe_control = wrap_traceback(function (pipe, cmd, args)
            local ret = control[cmd] or C.UBASE_ERR_UNHANDLED
            if type(ret) ~= "number" then
                if type(ret) ~= "string" then
                    ret = ret(pipe, control_args(cmd, va_start(args)))
                end
                ret = ubase_err(ret)
            end
            if ret == C.UBASE_ERR_UNHANDLED and cb.bin_input then
                ret = C.upipe_helper_control_bin_input(pipe, cmd, args)
            end
            if ret == C.UBASE_ERR_UNHANDLED and cb.bin_output then
                ret = C.upipe_helper_control_bin_output(pipe, cmd, args)
            end
            return ret
        end)
    end

    h_mgr.refcount_cb = function (refcount)
        local h_pipe = container_of(refcount, "struct upipe_helper", "urefcount")
        local pipe = h_pipe.upipe
        local k = props_clean(pipe)
        if cb.sub then
            local super = pipe:sub_get_super()
            for i, sub in ipairs(super.props._subpipes) do
                if sub.upipe == pipe then
                    table.remove(super.props._subpipes, i)
                    break
                end
            end
        end
        pipe:throw_dead()
        props[k] = nil
        pipe:helper_clean_upump()
        pipe:helper_clean_flow_def()
        pipe:helper_clean_uref_stream()
        pipe:helper_clean_sync()
        pipe:helper_clean_bin_output()
        pipe:helper_clean_bin_input()
        pipe:helper_clean_ubuf_mgr()
        pipe:helper_clean_uref_mgr()
        pipe:helper_clean_upump_mgr()
        pipe:helper_clean_uclock()
        pipe:helper_clean_input()
        pipe:helper_clean_output_size()
        pipe:helper_clean_output()
        pipe:helper_clean_urefcount()
        pipe:clean()
        C.free(ffi.cast("void *", h_pipe))
    end

    local refcount_cb
    refcount_cb = ffi.cast("urefcount_cb", function (refcount)
        local h_mgr = container_of(refcount, ct, "refcount")
        local mgr = h_mgr.mgr
        props_destroy(mgr)
        mgr.upipe_alloc:free()
        mgr.upipe_control:free()
        if mgr.upipe_input ~= nil then
            mgr.upipe_input:free()
        end
        h_mgr.refcount_cb:free()
        refcount_cb:free()
        C.free(ffi.cast("void *", h_mgr))
    end)

    h_mgr.refcount:init(refcount_cb)
    mgr.refcount = h_mgr.refcount
    return ffi.gc(mgr, C.upipe_mgr_release)
end

local function default_probe(log_level)
    local clock = uclock.std(0)
    local umem_mgr = umem.pool_simple(0)
    local udict_mgr = udict.inline(0, umem_mgr, -1, -1)
    local uref_mgr = uref.std(0, udict_mgr, 0)
    local upump_mgr

    local probe =
        uprobe.uclock(clock) ..
        uprobe.ubuf_mem(umem_mgr, 0, 0) ..
        uprobe.uref_mgr(uref_mgr) ..
        uprobe.stdio(C.stderr, log_level or C.UPROBE_LOG_NOTICE)

    if rawget(_G, "libupump_ev_static_so") or
       rawget(_G, "libupump_ev_static_dylib") then
        upump_mgr = upump.ev_default(0, 0)
        probe = probe .. uprobe.upump_mgr(upump_mgr)
    end

    return probe, upump_mgr
end

return setmetatable({
    name = "upipe",
    sigs = sigs,
    mgr = alloc("upipe_mgr"),
    iterator = iterator,
    default_probe = default_probe
}, {
    __index = mgr_mt.__index,
    __call = function (_, cb)
        return upipe_helper_alloc(cb)
    end
})
