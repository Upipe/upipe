#!/usr/bin/env luajit

--------------------------------------------------------------------------------
--  utrace.lua - upipe trace debugging tool
--------------------------------------------------------------------------------
--  Copyright (C) 2020-2026 EasyTools S.A.S.
--  SPDX-License-Identifier: MIT
--  Authors: Clément Vasseur
--------------------------------------------------------------------------------

local ffi = require "ffi"
local hex = bit.tohex
local fmt = string.format
local concat = table.concat
local insert = table.insert

require("table.clear")

local lsh, rsh, bxor, band = bit.lshift, bit.rshift, bit.bxor, bit.band

ffi.cdef [[
    // stdlib.h
    unsigned long int strtoul(const char *nptr, char **endptr, int base);

    // stdio.h
    typedef struct _IO_FILE FILE;
    FILE *fopen(const char *pathname, const char *mode);
    FILE *fdopen(int fd, const char *mode);
    void *rewind(FILE *stream);
    int fgetc_unlocked(FILE *stream);
    size_t fread_unlocked(void *ptr, size_t size, size_t n, FILE *stream);
    size_t fwrite_unlocked(const void *ptr, size_t size, size_t n, FILE *stream);
    int feof(FILE *stream);
    int fclose(FILE *stream);

    // unistd.h
    int pipe(int pipefd[2]);
    int close(int fd);
    int isatty(int fd);

    // sys/types.h
    typedef int pid_t;

    // sys/wait.h
    pid_t waitpid(pid_t pid, int *wstatus, int options);

    // spawn.h
    typedef struct posix_spawnattr_t posix_spawnattr_t;
    struct __spawn_action;

    typedef struct {
            int allocated;
            int used;
            struct __spawn_action *actions;
            int pad[16];
    } posix_spawn_file_actions_t;

    int posix_spawnp(pid_t *restrict pid, const char *restrict file,
                     const posix_spawn_file_actions_t *file_actions,
                     const posix_spawnattr_t *restrict attrp,
                     const char *const argv[restrict],
                     const char *const envp[restrict]);

    int posix_spawn_file_actions_init(posix_spawn_file_actions_t *file_actions);
    int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *file_actions,
                                          int fildes);
    int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *file_actions,
                                         int fildes, int newfildes);
    int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *file_actions);

    // elf.h
    typedef uint32_t Elf64_Word;
    typedef uint64_t Elf64_Addr;
    typedef struct Elf64_Shdr Elf64_Shdr;

    // libelf.h
    typedef struct Elf Elf;

    // gelf.h
    typedef Elf64_Word GElf_Word;
    typedef Elf64_Addr GElf_Addr;
    typedef Elf64_Shdr GElf_Shdr;

    // libdw.h
    typedef GElf_Addr Dwarf_Addr;

    // libdwfl.h
    typedef struct Dwfl Dwfl;
    typedef struct Dwfl_Module Dwfl_Module;
    typedef struct {
        int (*find_elf)(Dwfl_Module *mod, void **userdata,
                        const char *modname, Dwarf_Addr base,
                        char **file_name, Elf **elfp);

        int (*find_debuginfo)(Dwfl_Module *mod, void **userdata,
                              const char *modname, Dwarf_Addr base,
                              const char *file_name,
                              const char *debuglink_file, GElf_Word debuglink_crc,
                              char **debuginfo_file_name);

        int (*section_address)(Dwfl_Module *mod, void **userdata,
                               const char *modname, Dwarf_Addr base,
                               const char *secname,
                               GElf_Word shndx, const GElf_Shdr *shdr,
                               Dwarf_Addr *addr);

        char **debuginfo_path;
    } Dwfl_Callbacks;

    Dwfl *dwfl_begin(const Dwfl_Callbacks *callbacks);
    void dwfl_end(Dwfl *);
    int dwfl_errno(void);
    const char *dwfl_errmsg(int err);
    void dwfl_report_begin(Dwfl *dwfl);
    Dwfl_Module *dwfl_report_elf(Dwfl *dwfl, const char *name,
                                 const char *file_name, int fd,
                                 GElf_Addr base, bool add_p_vaddr);
    int dwfl_report_end(Dwfl *dwfl,
                        int (*removed)(Dwfl_Module *, void *,
                                       const char *, Dwarf_Addr,
                                       void *arg),
                        void *arg);
    Dwfl_Module *dwfl_addrmodule(Dwfl *dwfl, Dwarf_Addr address);
    const char *dwfl_module_addrname(Dwfl_Module *mod, GElf_Addr address);
    int dwfl_build_id_find_elf(Dwfl_Module *, void **,
                               const char *, Dwarf_Addr,
                               char **, Elf **);
    int dwfl_standard_find_debuginfo(Dwfl_Module *, void **,
                                     const char *, Dwarf_Addr,
                                     const char *, const char *,
                                     GElf_Word, char **);
    int dwfl_offline_section_address(Dwfl_Module *, void **,
                                     const char *, Dwarf_Addr,
                                     const char *, GElf_Word,
                                     const GElf_Shdr *,
                                     Dwarf_Addr *addr);
    const char *dwfl_module_info(Dwfl_Module *mod, void ***userdata,
                                 Dwarf_Addr *start, Dwarf_Addr *end,
                                 Dwarf_Addr *dwbias, Dwarf_Addr *symbias,
                                 const char **mainfile,
                                 const char **debugfile);
]]

local UCLOCK_FREQ = 27000000

local dw = ffi.load("libdw.so.1")

local function fatal(...)
    io.stderr:write("utrace: ", fmt(...), "\n")
    os.exit(1)
end

local function uassert(cond, ...)
    if not cond then fatal(...) end
end

local function spawn(process, args, env, actions)
    -- prepare file actions
    local file_actions
    if actions then
        file_actions = ffi.new("posix_spawn_file_actions_t")
        assert(ffi.C.posix_spawn_file_actions_init(file_actions) == 0)
        for _, action in ipairs(actions) do
            local f = "posix_spawn_file_actions_add" .. action[1]
            assert(ffi.C[f](file_actions, unpack(action, 2)) == 0)
        end
    end

    -- prepare args
    local argv = ffi.new("const char *[?]", #args + 1, args)
    argv[#args] = nil

    -- prepare environment
    local envp
    if env then
        local environ = {}
        for k, v in pairs(env) do
            insert(environ, fmt("%s=%s", k, v))
        end
        envp = ffi.new("const char *[?]", #environ + 1, environ)
        envp[#environ] = nil
    end

    -- spawn
    local pid = ffi.new("pid_t[1]")
    assert(ffi.C.posix_spawnp(pid, process, file_actions, nil, argv, envp) == 0)
    if file_actions then
        assert(ffi.C.posix_spawn_file_actions_destroy(file_actions))
    end
    return pid[0]
end

local function spawn_target(arg, env)
    -- create pipe for reading utrace log from target process
    local pipefd = ffi.new("int[2]")
    assert(ffi.C.pipe(pipefd) == 0)

    -- spawn target process
    env.UTRACE_FD = pipefd[1]
    local pid = spawn(arg[1], arg, env, {{ 'close', pipefd[0] }})
    io.stderr:write(fmt("utrace: spawned process %d\n", pid))
    ffi.C.close(pipefd[1])

    -- return FILE handle
    local f = ffi.C.fdopen(pipefd[0], "r")
    assert(f ~= nil)
    return f, pid, pipefd[0]
end

local function shift(t)
    return table.remove(t or arg, 1)
end

local function enum(e)
    local t = {}
    for i, v in ipairs(e) do
        t[v] = i - 1
        t[i - 1] = v
    end
    return t
end

local utrace_magic = "UTRACE01"

local ubase_err = enum {
    'none', 'unknown', 'alloc', 'nospc', 'upump', 'unhandled', 'invalid',
    'external', 'busy'
}

local uprobe_event = enum {
    'log', 'fatal', 'error', 'ready', 'dead', 'stalled', 'source_end',
    'sink_end', 'need_output', 'provide_request', 'need_upump_mgr',
    'freeze_upump_mgr', 'thaw_upump_mgr', 'need_source_mgr', 'new_flow_def',
    'new_rap', 'split_update', 'sync_acquired', 'sync_lost', 'clock_ref',
    'clock_ts', 'clock_utc', 'preroll_end'
}

local uprobe_log_level = enum {
    'verbose', 'debug', 'info', 'notice', 'warning', 'error'
}

local upipe_command = enum {
    'attach_uref_mgr', 'attach_upump_mgr', 'attach_uclock', 'get_uri',
    'set_uri', 'get_option', 'set_option', 'register_request',
    'unregister_request', 'set_flow_def', 'get_max_length', 'set_max_length',
    'flush', 'end_preroll', 'get_output', 'set_output', 'attach_ubuf_mgr',
    'get_flow_def', 'get_output_size', 'set_output_size', 'split_iterate',
    'get_sub_mgr', 'iterate_sub', 'sub_get_super', 'bin_freeze', 'bin_thaw',
    'bin_get_first_inner', 'bin_get_last_inner', 'src_get_size',
    'src_get_position', 'src_set_position', 'src_set_range', 'src_get_range'
}

local urequest_type = enum {
    'uref_mgr', 'flow_format', 'ubuf_mgr', 'uclock', 'sink_latency'
}

local upump_event = enum {
    'idler', 'timer', 'fd_read', 'fd_write', 'signal'
}

local upump_command = enum {
    'start', 'stop', 'free', 'get_status', 'set_status',
    'alloc_blocker', 'free_blocker', 'restart'
}

local udict_type = enum {
    'end', 'opaque', 'string', 'void', 'bool', 'small_unsigned', 'small_int',
    'unsigned', 'int', 'rational', 'float'
}

local function usage()
    io.stderr:write("Usage: ", arg[0], " COMMAND [ARGS]\n",
        "\n",
        "  utrace record [<options>] [--] <command> [<options>]\n",
        "    -e, --env <key=value>  set environment variable for command\n",
        "    -o, --output <file>    output file name [utrace.data]\n",
        "    -r, --raw              disable record data compression\n",
        "\n",
        "  utrace report [<options>]\n",
        "    -i, --input <file>     input file name [utrace.data]\n",
        "    -f, --filter <event>   event filter\n",
        "    -a, --attr <mode>      dump udict attributes (full|short|line)\n",
        "    -l, --leaks            enable leak check at exit\n",
        "\n",
        "  utrace log [<options>] [<level>]\n",
        "    -i, --input <file>     input file name [utrace.data]\n",
        "    -p, --prefix <prefix>  show logs with given prefix\n",
        "\n",
        "  utrace graph [<options>] [<name>]...\n",
        "    -i, --input <file>     input file name [utrace.data]\n",
        "\n")
    os.exit(1)
end

local command = shift()

local ctx_filter
local call_filter = {}
local dump_udict_full
local dump_udict_short
local dump_udict_line
local show_prefix
local show_leaks = false
local filename = "utrace.data"
local show_log = 6
local dump_graphs

local calls = enum {
    -- uprobe
    'uprobe_init', 'uprobe_clean',
    'uprobe_throw', 'uprobe_throw_enter', 'uprobe_throw_leave',

    -- upipe
    'upipe_alloc', 'upipe_alloc_enter', 'upipe_alloc_leave',
    'upipe_init', 'upipe_clean',
    'upipe_control', 'upipe_control_enter', 'upipe_control_leave',
    'upipe_throw', 'upipe_throw_enter', 'upipe_throw_leave',

    -- urequest
    'urequest_init', 'urequest_clean', 'urequest_free',
    'urequest_provide', 'urequest_provide_enter', 'urequest_provide_leave',

    -- upump
    'upump_alloc', 'upump_alloc_enter', 'upump_alloc_leave',
    'upump_control', 'upump_control_enter', 'upump_control_leave',

    -- ulog
    'ulog_init', 'ulog_add_prefix',
}

if command == "record" then
    local env = {}
    local raw = false

    while arg[1] and arg[1]:sub(1, 1) == "-" do
        local opt = shift()
        if opt == "--env" or opt == "-e" then
            local e = shift()
            e:gsub("(.+)=(.*)", function (k, v) env[k] = v end)
        elseif opt == "--output" or opt == "-o" then filename = shift()
        elseif opt == "--raw" or opt == "-r" then raw = true
        elseif opt == "--" then break
        else usage()
        end
    end

    arg[-1], arg[0] = nil, nil
    local f, pid, fd = spawn_target(arg, env)

    if raw then
        local size = 4096 * 10
        local buf = ffi.new("char[?]", size)
        local o = assert(io.open(filename, "w"))
        while true do
            local ret = ffi.C.fread_unlocked(buf, 1, size, f)
            if ret > 0 then o:write(ffi.string(buf, ret)) end
            if ret < size then
                if ffi.C.feof(f) then break end
                fatal("read error from target process")
            end
        end
        ffi.C.fclose(f)
        o:close()
    else
        local cmd = { "zstd", "-19", "-f", "-q", "-o", filename }
        local pid2 = spawn(cmd[1], cmd, nil, {{ 'dup2', fd, 0 }, { 'close', fd }})
        ffi.C.waitpid(pid2, nil, 0)
    end

    ffi.C.waitpid(pid, nil, 0)
    return

elseif command == "report" then
    while arg[1] and arg[1]:sub(1, 1) == "-" do
        local opt = shift()
        if opt == "--filter" or opt == "-f" then
            local filter = shift()
            local kind, id = filter:match("^([%w%-]+)%-(%d+)$")
            if kind and id then
                ctx_filter = filter
            else
                local found
                for _, c in pairs(calls) do
                    if type(c) == 'string' and c:match("^" .. filter) then
                        call_filter[c] = true
                        found = true
                    end
                end
                uassert(found, "unknown event '%s'", filter)
            end
        elseif opt == "--attr" or opt == "-a" then
            local mode = shift()
            if mode == "full" then dump_udict_full = true
            elseif mode == "short" then dump_udict_short = true
            elseif mode == "line" then dump_udict_line = true
            else usage()
            end
        elseif opt == "--leaks" or opt == "-l" then show_leaks = true
        elseif opt == "--input" or opt == "-i" then filename = shift()
        else usage()
        end
    end

    if not next(call_filter) then
        call_filter = calls
    end

elseif command == "log" then
    while arg[1] and arg[1]:sub(1, 1) == "-" do
        local opt = shift()
        if opt == "--prefix" or opt == "-p" then show_prefix = shift()
        elseif opt == "--input" or opt == "-i" then filename = shift()
        else usage()
        end
    end

    local level = arg[1] or 'info'
    show_log = uprobe_log_level[level]
    uassert(show_log, "unknown log level '%s'", level)

elseif command == "graph" then
    while arg[1] and arg[1]:sub(1, 1) == "-" do
        local opt = shift()
        if opt == "--input" or opt == "-i" then filename = shift()
        else usage()
        end
    end

    dump_graphs = enum(arg)

else
    usage()
end

local f = ffi.C.fopen(filename, "r")
if f == nil then fatal("%s: %m", filename) end
local buf_8 = ffi.new("char[8]")
assert(ffi.C.fread_unlocked(buf_8, 8, 1, f) == 1)

if ffi.string(buf_8, 8) == utrace_magic then
    ffi.C.rewind(f)
else
    ffi.C.fclose(f)
    local cmd = { "zstd", "-d", "-c", "-q", "--", filename }
    local pipefd = ffi.new("int[2]")
    assert(ffi.C.pipe(pipefd) == 0)
    local pid = spawn(cmd[1], cmd, nil, {
        { 'dup2', pipefd[1], 1 },
        { 'close', pipefd[0] },
        { 'close', pipefd[1] },
    })
    ffi.C.close(pipefd[1])
    f = ffi.C.fdopen(pipefd[0], "r")
end
assert(f ~= nil)

local dwfl
local symbol_cache = {}

local function symbolize(addr)
    local hex_addr = hex(addr)
    if not dw then return hex_addr end
    local sym = symbol_cache[hex_addr]
    if sym then return sym end

    local mod = dw.dwfl_addrmodule(dwfl, addr)
    if mod == nil then
        io.stderr:write(fmt("utrace: failed to find module for addr %s\n", hex_addr))
        return hex_addr
    end

    local symname = dw.dwfl_module_addrname(mod, addr)
    if symname == nil then return hex_addr end
    sym = ffi.string(symname)
    symbol_cache[hex_addr] = sym
    return sym
end

local function read_u8()
    return ffi.C.fgetc_unlocked(f)
end

local function read_uint()
    -- ULEB128 decoding
    local res = ffi.new("uint64_t")
    local shift = 0
    repeat
        local v = read_u8()
        res = res + lsh(ffi.cast("uint64_t", band(v, 0x7f)), shift)
        shift = shift + 7
    until v < 0x80
    return res < 2^32 and tonumber(res) or res
end

local function read_int()
    local v = read_uint()
    -- ZigZag decode: (v >> 1) ^ -(v & 1)
    local res = ffi.cast("int64_t", bxor(rsh(v, 1), -band(v, 1)))
    return (res >= 0 and res < 2^32) and tonumber(res) or res
end

local function read_double()
    local u = ffi.new("union { double d; uint64_t u64; }")
    u.u64 = read_uint()
    return u.d
end

local function read_ptr()
    return ffi.cast("uint64_t", read_uint())
end

local function read_id()
    return read_u8()
end

local function read_buf(len)
    local buf = ffi.new("uint8_t[?]", len)
    if len > 0 then
        assert(ffi.C.fread_unlocked(buf, len, 1, f) == 1)
    end
    return buf
end

local function read_lstr(len)
    return ffi.string(read_buf(len), len)
end

local function read_str()
    local len = read_int()
    if len >= 0 then
        return read_lstr(len)
    end
end

local function read_sig()
    return (read_lstr(4):gsub("^%z%z%z%z$", ""):gsub("%c", function (v)
        return fmt("<%02x>", string.byte(v))
    end))
end

local function read_uref()
    local ptr = read_ptr()
    local ref = { ptr = ptr }
    if ptr ~= 0 then
        ref.dict = {}
        while true do
            local type = udict_type[read_uint()]
            if type == 'end' then break end
            local name, val = read_str()
            if type == 'opaque' then val = read_buf(read_uint())
            elseif type == 'string' then val = read_str()
            elseif type == 'void' then
            elseif type == 'bool' then val = read_uint() ~= 0
            elseif type == 'rational' then
                val = { num = read_int(), den = read_uint() }
            elseif type == 'small_unsigned' then val = read_uint()
            elseif type == 'small_int' then val = read_int()
            elseif type == 'unsigned' then val = read_uint()
            elseif type == 'int' then val = read_int()
            elseif type == 'float' then val = read_double()
            end
            insert(ref, { name = name, type = type, val = val })
            ref.dict[name] = val or true
        end
        ref.flow_def = ref.dict["f.def"]
    end
    return ref
end

local id_d = {}
local id_n = {
    pipe=0, probe=0, request=0, pump=0, log=0,
    ['pipe-mgr']=0, ['ref-mgr']=0, ['buf-mgr']=0, ['pump-mgr']=0
}

local function id_new(cat, addr)
    assert(addr ~= 0)
    id_n[cat] = id_n[cat] + 1
    local id = cat .. "-" .. id_n[cat]
    id_d[hex(addr)] = id
    return id
end

local function id_get(cat, addr)
    if addr == 0 then return nil end
    local id = id_d[hex(addr)]
    assert(id, "unknown address " .. hex(addr))
    assert(id:match("^" .. cat))
    return id
end

local function id_del(addr)
    id_d[hex(addr)] = nil
end

local function probe_new(addr) return id_new('probe', addr) end
local function probe_get(addr) return id_get('probe', addr) end
local function pipe_new(addr) return id_new('pipe', addr) end
local function pipe_get(addr) return id_get('pipe', addr) end
local function pipe_mgr_new(addr) return id_new('pipe-mgr', addr) end
local function pipe_mgr_get(addr) return id_get('pipe-mgr', addr) end
local function request_new(addr) return id_new('request', addr) end
local function request_get(addr) return id_get('request', addr) end
local function pump_new(addr) return id_new('pump', addr) end
local function pump_get(addr) return id_get('pump', addr) end
local function pump_mgr_new(addr) return id_new('pump-mgr', addr) end
local function pump_mgr_get(addr) return id_get('pump-mgr', addr) end
local function log_new(addr) return id_new('log', addr) end
local function log_get(addr) return id_get('log', addr) end

local context = {}
local ctx_mt = {
    __tostring = function (self)
        return self.kind .. "-" .. self.id
    end
}
local function context_get(addr) return addr ~= 0 and assert(context[hex(addr)]) or nil end
local function context_set(addr, ctx) context[hex(addr)] = setmetatable(ctx, ctx_mt) end
local function context_del(addr) context[hex(addr)] = nil; id_del(addr) end

local ctx_stack = {}
local function ctx_push(ctx) insert(ctx_stack, ctx) end
local function ctx_pop() return assert(table.remove(ctx_stack)) end
local function ctx_top() return ctx_stack[#ctx_stack] end

local function mgr_get(kind, addr)
    local ctx = context[hex(addr)]
    if ctx then
        assert(ctx.kind == kind)
        return ctx
    end
    id_n[kind] = id_n[kind] + 1
    ctx = { kind = kind, addr = addr, id = id_n[kind] }
    context_set(addr, ctx)
    return ctx
end

local logs = {}
local logs_stack = {}
local log_direct = false
local log_ignore = enum { 'ulog_init', 'ulog_add_prefix' }
local log_throw_ignore = enum {
    'log', 'clock_ts', 'clock_ref', 'new_rap', 'clock_utc'
}

local color_enabled = ffi.C.isatty(1) == 1
local function SGR(c) return color_enabled and "\x1b[" .. c .. "m" or "" end

local c = { italic_off = SGR(23) }
for i, attr in ipairs{'reset','bold','dim','italic','underline'} do
    c[attr] = SGR(i - 1)
end
for i, color in ipairs{'black','red','green','yellow','blue','magenta','cyan','white'} do
    c[color] = SGR(29 + i)
    c[color:upper()] = SGR("1;" .. (29 + i))
    c[color:gsub("^%l", string.upper)] = SGR(89 + i)
end

local function it(str)
    return c.italic .. str .. c.italic_off
end

local color_mapping = {
    upipe = 'cyan', uprobe = 'blue', upump = 'magenta',
    urequest = 'green', ulog = 'yellow'
}

local highlight_mapping = {
    upipe_alloc = 4,
    upipe_control = 3,
    upipe_throw = 4,
    uprobe_init = 3,
    uprobe_throw = 4,
    upump_alloc = 8,
    upump_control = 3,
    urequest_init = 3,
}

local function hex_string(buf)
    local res = {}
    for i = 0, ffi.sizeof(buf) - 1 do
        res[i] = fmt("%02x", buf[i])
    end
    return concat(res, " ")
end

local function pushf(t, format, ...)
    t[#t + 1] = fmt(format, ...)
end

local pix_fmts = {}
local hsub = { [420] = 2, [422] = 2, [444] = 1 }
local vsub = { [420] = 2, [422] = 1, [444] = 1 }

-- generate yuv planar pixel formats
for _, alpha in ipairs { "", "a" } do
    for _, depth in ipairs { 8, 10, 12, 16 } do
        for _, subsampling in ipairs { 420, 422, 444 } do
            for _, endian in ipairs { "l", "b" } do
                local planes = {}
                for i, plane in ipairs { "y", "u", "v", "a" } do
                    if plane == "a" and alpha == "" then break end
                    planes[i] = fmt("%s%u%s:%u:%u:%u", plane, depth,
                        depth == 8 and "" or endian,
                        (i == 2 or i == 3) and hsub[subsampling] or 1,
                        (i == 2 or i == 3) and vsub[subsampling] or 1,
                        depth == 8 and 1 or 2)
                end
                pix_fmts["1-" .. concat(planes, "-")] =
                    "yuv" .. alpha .. subsampling .. "p" ..
                    (depth == 8 and "" or depth) ..
                    (depth == 8 and "" or endian .. "e")
                if depth == 8 then break end
            end
        end
    end
end

-- generate yuv semi-planar pixel formats
for _, depth in ipairs { 8, 10 } do
    local bpp8 = { [420] = 12, [422] = 16, [444] = 24 }
    for _, subsampling in ipairs { 420, 422, 444 } do
        for _, endian in ipairs { "l", "b" } do
            local planes = {}
            for i, plane in ipairs { "y", "uv" } do
                planes[i] = fmt("%s%s:%u:%u:%u",
                    plane:gsub(".", "%0" .. depth),
                    depth == 8 and "" or endian,
                    plane == "uv" and hsub[subsampling] or 1,
                    plane == "uv" and vsub[subsampling] or 1,
                    (plane == "uv" and 2 or 1) * (depth == 8 and 1 or 2))
            end
            pix_fmts["1-" .. concat(planes, "-")] =
                depth == 8 and "nv" .. bpp8[subsampling] or
                "p" .. (subsampling % 10) .. depth .. endian .. "e"
        end
    end
end

local function hr(num)
    return tostring(num)
        :reverse()
        :gsub("%d%d%d", "%0,")
        :gsub(",$", "")
        :reverse()
end

local function hr_k(num)
    return (hr(num):gsub(",000$", "k"))
end

local function hr_c(ts)
    local res = hr(ts)
    if ts < UCLOCK_FREQ then
        res = res .. fmt(" (%gms)", 1000. * ts / UCLOCK_FREQ)
    else
        res = res .. fmt(" (%gs)", tonumber(ts) / UCLOCK_FREQ)
    end
    return res
end

local h26x_encaps = {
    "NALUs", "Annex B", "Length", "Length 1", "Length 2", "Length 4"
}

local mpga_audio_object_type = {
    "AAC main", "AAC LC", "AAC SSR", "AAC LTP", "SBR", "AAC Scalable", "TwinVQ",
    "CELP", "HVXC", 0, 0, "TTSI", "Main synthetic", "Wavetable synthesis",
    "General MIDI", "Algorithmic Synthesis and Audio FX", "ER AAC LC", 0,
    "ER AAC LTP", "ER AAC Scalable", "ER TwinVQ", "ER BSAC", "ER AAC LD",
    "ER CELP", "ER HVXC", "ER HILN", "ER Parametric", "SSC", "PS",
    "MPEG Surround", "(escape)", "Layer-1", "Layer-2", "Layer-3", "DST", "ALS",
    "SLS", "SLS non-core", "ER AAC ELD", "SMR Simple", "SMR Main"
}

local mpga_encaps = {
    "raw", "ADTS", "LOAS", "LATM"
}

local avc_profiles = {
    [0x4240] = "Constrained Baseline",
    [0x4200] = "Baseline",
    [0x5800] = "Extended",
    [0x4d00] = "Main",
    [0x4d40] = "Constrained Main",
    [0x6400] = "High",
    [0x6408] = "Progressive High",
    [0x640c] = "Constrained High",
    [0x6e00] = "High 10",
    [0x7a00] = "High 4:2:2",
    [0xf400] = "High 4:4:4 Predictive",
    [0x6e10] = "High 10 Intra",
    [0x7a10] = "High 4:2:2 Intra",
    [0xf410] = "High 4:4:4 Intra",
    [0x4400] = "CAVLC 4:4:4 Intra",
    [0x5300] = "Scalable Baseline",
    [0x5304] = "Scalable Constrained Baseline",
    [0x5600] = "Scalable High",
    [0x5604] = "Scalable Constrained High",
    [0x5620] = "Scalable High Intra",
    [0x8000] = "Stereo High",
    [0x7600] = "Multiview High",
    [0x8a00] = "Multiview Depth High",
}

local function h264_profile(profile, compatibility_flags)
    return avc_profiles[profile * 256 + compatibility_flags]
end

local function udict_summary(ref)
    if not ref.dict then return end

    local attr = {}
    local used = {}
    for k, v in pairs(ref.dict) do
        local prefix, field = k:match("^(%w+)%.(.+)")
        if prefix then
            attr[prefix] = attr[prefix] or {}
            attr[prefix][field] = v
        end
    end

    local line = {}
    local res = {}
    local short = {}

    local function use(...)
        for _, v in ipairs({...}) do
            used[v] = true
        end
    end

    local function sep(title)
        if res[1] then
            local info = concat(res, " ")
            insert(short, { title, info })
            if line[1] then insert(line, "|") end
            insert(line, (info:gsub(" %S+ %((.+)%)", " %1")))
            table.clear(res)
        end
    end

    -- flow
    local f = attr.f
    if f then
        if f.id then pushf(res, "%u", f.id) end
        if f.def then pushf(res, "%s", f.def) end
        if f.comp then pushf(res, "comp") end
        if f.global then pushf(res, "global") end
        if f.langs then
            local langs = {}
            for i = 0, f.langs - 1 do
                insert(langs, f["lang[" .. i .. "]"])
            end
            pushf(res, "%s", concat(langs, "/"))
            for i = 0, f.langs - 1 do
                use("f.lang[" .. i .. "]")
            end
        end
        use("f.id", "f.def", "f.comp", "f.global", "f.langs")
    end
    sep("flow")

    -- clock
    local k = attr.k
    if k then
        if k.latency then pushf(res, "latency %s", hr_c(k.latency)) end
        use("k.latency")
    end
    sep("clock")

    -- block
    local b = attr.b
    if b then
        if b.size then pushf(res, "%sB", hr_k(b.size)) end
        if b.max_octetrate then pushf(res, "max-rate %sB/s", hr_k(b.max_octetrate)) end
        if b.max_bs then pushf(res, "max-bs %sB", hr_k(b.max_bs)) end
        if b.octetrate then pushf(res, "rate %sB/s", hr_k(b.octetrate)) end
        if b.bs then pushf(res, "bs %sB", hr_k(b.bs)) end
        use("b.size", "b.max_octetrate", "b.max_bs", "b.octetrate", "b.bs")
    end
    sep("block")

    -- uri
    local uri = attr.uri
    if uri and uri.path then
        pushf(res, "%s%s", uri.scheme and uri.scheme .. "://" or "", uri.path)
        use("uri.scheme", "uri.path")
    end
    sep("uri")

    -- ts
    local t = attr.t
    if t then
        if t.pid then pushf(res, "PID %u", t.pid) end
        if t.maxdelay then pushf(res, "max-delay %s", hr_c(t.maxdelay)) end
        use("t.pid", "t.maxdelay")
    end
    sep("ts")

    -- h264/h26x
    local h264, h26x = attr.h264, attr.h26x
    if h264 and h264.profile and h264.profilecomp and h264.level then
        pushf(res, "AVC %s Profile, Level %.1f%s",
              h264_profile(h264.profile, h264.profilecomp),
              h264.level / 10,
              h26x and h26x.encaps and
              ", " .. h26x_encaps[h26x.encaps + 1] or "")
        use("h264.profile", "h264.profilecomp", "h264.level", "h26x.encaps")
        sep("h264")
    elseif h26x then
        pushf(res, h26x_encaps[h26x.encaps + 1])
        use("h26x.encaps")
        sep("h26x")
    end

    -- mpga
    local mpga = attr.mpga
    if mpga then
        if mpga.aot then pushf(res, "%s", mpga_audio_object_type[mpga.aot]) end
        if mpga.encaps then pushf(res, "%s", mpga_encaps[mpga.encaps + 1]) end
        use("mpga.aot", "mpga.encaps")
    end
    sep("mpga")

    -- pic
    local p = attr.p
    if p then
        if (p.hsize and p.vsize) or p.fps then
            pushf(res, "%s%s%s",
                p.hsize and fmt("%u×%u", p.hsize, p.vsize) or "",
                p.progressive and "p" or "i",
                p.fps and fmt("%.3f",
                    p.fps.num / p.fps.den):gsub("%.000$", "") or "")
        end
        if p.sar then
            pushf(res, "%u:%u", p.sar.num, p.sar.den)
        end
        if p.planes then
            local pix = p.macropixel
            for i = 0, p.planes - 1 do
                pix = pix .. fmt("-%s:%u:%u:%u",
                    p["chroma[" .. i .. "]"],
                    p["hsub[" .. i .. "]"],
                    p["vsub[" .. i .. "]"],
                    p["macropix[" .. i .. "]"])
            end
            if pix_fmts[pix] then
                pushf(res, "%s", pix_fmts[pix])
            end
            for i = 0, p.planes - 1 do
                use("p.chroma[" .. i .. "]",
                    "p.hsub[" .. i .. "]",
                    "p.vsub[" .. i .. "]",
                    "p.macropix[" .. i .. "]")
            end
        end
        if p.colorprim or p.transfer or p.colmatrix then
            pushf(res, "%s/%s/%s",
                p.colorprim or "unspec",
                p.transfer or "unspec",
                p.colmatrix or "unspec")
        end
        if p.format then pushf(res, "%s", p.format) end
        use("p.hsize", "p.vsize", "p.fps", "p.progressive", "p.sar",
            "p.planes", "p.macropixel", "p.format",
            "p.colorprim", "p.transfer", "p.colmatrix")
    end
    sep("pic")

    -- sound
    local s = attr.s
    if s then
        if s.channels then pushf(res, "%uch", s.channels) end
        if s.rate then pushf(res, "%gkHz", s.rate / 1000) end
        if s.samples then pushf(res, "%s samples", hr(s.samples)) end
        use("s.channels", "s.rate", "s.samples")
    end
    sep("sound")

    return line, short, used
end

local function iter_attr(ref)
    local pos = 1
    return function ()
        local v = ref[pos]
        if not v then return end
        pos = pos + 1
        local val = v.val
        if v.type == 'string'       then val = '"' .. val .. '"'
        elseif v.type == 'opaque'   then val = "<" .. hex_string(val) .. ">"
        elseif v.type == 'rational' then val = fmt("%d/%u", val.num, val.den)
        elseif v.type == 'void'     then val = "yes"
        elseif v.type == 'unsigned' then val = hr(fmt("%u", val))
        elseif v.type == 'int'      then val = hr(fmt("%d", val))
        end
        return v.name, v.type, val
    end
end

local function log_print(log)
    if ctx_filter then
        local found
        for _, token in ipairs(log) do
            if token == ctx_filter then
                found = true
                break
            end
        end
        if not found then return end
    end
    local highlight = highlight_mapping[log[1]]
    local color = color_mapping[log[1]:match("^[^_]+")]
    if color then
        log[1] = c[color] .. log[1] .. c.reset
        if highlight then
            local hl_color = color
            if log[1]:match("_alloc") or log[1]:match("_init") then
                hl_color = hl_color:upper()
            end
            log[highlight] = c[hl_color] .. log[highlight] .. c.reset
        end
    end
    for i, token in ipairs(log) do
        if token:match('^".*"$') then
            log[i] = c.yellow .. token .. c.reset
        end
    end
    local indent = ("  "):rep(log.indent)
    io.stdout:write(indent, concat(log, " "), "\n")

    if log.ref then
        local b_color = c[color:upper()]
        local br_color = c[color:gsub("^%l", string.upper)]
        if dump_udict_full then
            for name, type, val in iter_attr(log.ref) do
                io.stdout:write(indent, "  ", c.dim, b_color, name,
                    c.reset, c.dim, br_color, c.italic, " ", type, SGR(23),
                    b_color, " = ", tostring(val), c.reset, "\n")
            end
        end
        if dump_udict_short or dump_udict_line then
            local line, short, used = udict_summary(log.ref)
            if dump_udict_line then
                if line and line[1] then
                    io.stdout:write(indent, "  ", c.dim, b_color, "▶︎ ",
                        concat(line, " "), c.reset, "\n")
                end
            end
            if dump_udict_short then
                if short then
                    for _, v in ipairs(short) do
                        io.stdout:write(indent, "  ", c.dim, b_color, v[1],
                            c.reset, c.dim, br_color, " ", v[2], c.reset, "\n")
                    end
                end
            end
            for name, type, val in iter_attr(log.ref) do
                if not used[name] then
                    io.stdout:write(indent, "  ", c.dim, b_color, name,
                        c.reset, c.dim, br_color, c.italic, " ", type, SGR(23),
                        b_color, " = ", tostring(val), c.reset, "\n")
                end
            end
        end
    end
end

local function log_flush()
    for _, log in ipairs(logs) do
        if not (log_ignore[log[1]] or
            (log[1] == "upipe_throw" or
             log[1] == "uprobe_throw") and
            log_throw_ignore[log[4]]) then
            log_print(log)
        end
    end
    table.clear(logs)
end

local curr_warn
local curr_uref

local function apply_filter(call)
    if not call_filter[call] then
        curr_warn = nil
        curr_uref = nil
        return
    end
    return true
end

local function insert_props(log)
    if curr_warn then
        insert(log, curr_warn)
        curr_warn = nil
    end
    if curr_uref then
        log.ref = curr_uref
        curr_uref = nil
    end
end

local function log(call, ...)
    if not apply_filter(call) then return end
    local t = { call, indent = #logs_stack }
    for i = 1, select("#", ...) do
        t[i + 1] = tostring(select(i, ...))
    end
    insert_props(t)
    if log_direct then
        return log_print(t)
    end
    insert(logs, t)
    return t
end

local function log_enter(call, ...)
    if log_direct then return log(call .. "_enter", ...) end
    local t = log(call, ...)
    if t then insert(logs_stack, t) end
end

local function log_leave(call, ...)
    if log_direct then return log(call .. "_leave", ...) end
    if not apply_filter(call) then return end
    local log = table.remove(logs_stack)
    insert(log, "→")
    for i = 1, select("#", ...) do
        insert(log, tostring(select(i, ...)))
    end
    insert_props(log)
    if log.indent == 0 then
        log_flush()
    end
end

local function q(str)
    return str and "'" .. str .. "'" or nil
end

local function qq(str)
    return str and '"' .. str .. '"' or nil
end

local function utrace_init()
    uassert(read_lstr(8) == utrace_magic,
        "%s: unrecognized file format", filename)

    local function fatal_dw(msg)
        fatal("symbolizer: %s: %s", msg, dw.dwfl_errmsg(dw.dwfl_errno()))
    end

    if not dw then
        while read_ptr() ~= 0 do read_str() end
        return
    end

    local callbacks = ffi.new("Dwfl_Callbacks", {
        dw.dwfl_build_id_find_elf,
        dw.dwfl_standard_find_debuginfo,
        dw.dwfl_offline_section_address,
        nil
    })
    dwfl = dw.dwfl_begin(callbacks)
    -- dwfl = ffi.gc(dw.dwfl_begin(callbacks), dw.dwfl_end) XXX
    dw.dwfl_report_begin(dwfl)

    while true do
        local addr = read_uint()
        if addr == 0 then break end
        local path = read_str()
        if not dw.dwfl_report_elf(dwfl, path, path, -1, addr, false) then
            fatal_dw("dwfl_report_elf")
        end
    end

    if dw.dwfl_report_end(dwfl, nil, nil) ~= 0 then
        fatal_dw("dwfl_report_end")
    end
end

local function utrace_dump_graph()
    local name = read_str()
    if not dump_graphs or (next(dump_graphs) and not dump_graphs[name]) then
        return
    end

    local dumped = {}
    local f = assert(io.open(name .. ".dot", "w"))

    local function p(stmt, attrs)
        local a = ""
        if attrs then
            local attr_list = {}
            for k, v in pairs(attrs) do
                if k == "label_html" then
                    k = "label"
                    v = "<" .. v .. ">"
                else
                    v = '"' .. v .. '"'
                end
                insert(attr_list, k .. "=" .. v)
            end
            a = fmt(" [%s]", concat(attr_list, " "))
        end
        f:write(fmt("%s%s\n", stmt, a))
    end

    local function pp(ctx, d)
        if not ctx.inner_pipes[1] then d = "" end
        return "pipe_" .. (d or "") .. ctx.id
    end

    local function node(id, attrs) p(id, attrs) end
    local function edge(from, to, attrs) p(from .. "->" .. to, attrs) end
    local function comment(...) f:write("#", fmt(...), "\n") end
    local function attr(k, v) f:write(fmt("%s=%q\n", k, v)) end
    local function attr_html(k, v) f:write(fmt("%s=<%s>\n", k, v)) end

    local function dump_pipe(ctx)
        if dumped[ctx] then return end
        comment("begin pipe-%d", ctx.id)

        for _, sub_ctx in ipairs(ctx.sub_pipes) do
            dump_pipe(sub_ctx)
            edge(pp(ctx, 'i'), pp(sub_ctx, 'i'),
              {style="dashed", arrowtail="tee", dir="back"})
            f:write(fmt("{rank=same; %s %s}\n", pp(ctx, 'i'), pp(sub_ctx, 'i')))
        end

        local label = ctx.mgr.name:gsub("^upipe_", "")
        if ctx.name ~= label and ctx.name then
            label = label .. "<br/>" .. ctx.name
        end
        label = "<b>" .. ctx.id .. "</b> | " .. label

        if ctx.uri then
            label = label .. "<br/>uri: " .. ctx.uri
        end

        if ctx.inner_pipes[1] then
            f:write(fmt("subgraph cluster_%d {\n", ctx.id))
            attr("color", "lightblue")
            attr("fillcolor", "#d7f1ff")
            attr("style", "filled")
            attr_html("label", label)

            local first, last
            for _, inner in ipairs(ctx.inner_pipes) do
                if not first then first = inner end
                if not last then last = inner end
                if inner.output then
                    if inner.output == first then first = inner end
                    if inner.output == ctx.output then last = inner end
                else
                    last = inner
                end
                dump_pipe(inner)
            end

            local flow_def = first.flow_def_input or ""
            flow_def = flow_def:gsub("%.", "\\l")
            node(pp(ctx, 'i'), {shape="point"})
            edge(pp(ctx, 'i'), pp(first, 'i'), {label=flow_def})
            if last.output then
                node(pp(ctx, 'o'), {shape="point"})
                edge(pp(last, 'o'), pp(ctx, 'o'))
            end

            f:write("}\n")
        else
            node(pp(ctx), {label_html=label})
        end

        if ctx.output then
            if not ctx.outer_pipe or ctx.output ~= ctx.outer_pipe.output then
                dump_pipe(ctx.output)
                local flow_def = ctx.output.flow_def_input or ""
                flow_def = flow_def:gsub("%.", "\\l")
                edge(pp(ctx, 'o'), pp(ctx.output, 'i'), {label=flow_def})
            end
        end

        comment("end pipe-%d", ctx.id)
        dumped[ctx] = true
    end

    f:write("digraph {\n")
    p("graph", {bgcolor="#00000000", fontname="Arial", fontsize=10, fontcolor="#0e0e0e"})
    p("edge", {penwidth=1, color="#0e0e0e", fontname="Arial", fontsize=7, fontcolor="#0e0e0e"})
    p("node", {
        shape = "plaintext", width = 0, height = 0, margin = 0.05,
        style = "filled", color = "lightblue", fillcolor = "#b7d1df",
        fontname = "Arial", fontsize = 10, fontcolor = "#0e0e0e"})

    attr("newrank", true)
    attr("rankdir", "LR")

    for _, ctx in pairs(context) do
        if ctx.kind == 'pipe' then
            if not ctx.outer_pipe and not ctx.super_pipe then
                dump_pipe(ctx)
            end
        end
    end

    f:write("}\n")
    f:close()
end

local state_transitions = {
    pipe = {
        alloc = enum { 'init' },
        init = enum { 'ready', 'clean' },
        ready = enum { 'dead', 'error', 'fatal' },
        error = enum { 'dead', 'fatal' },
        fatal = enum { 'dead' },
        dead = enum { 'clean' },
        clean = {}
    },
    request = {
        init = enum { 'clean' },
        clean = enum { 'free' },
    },
}

local function warn(...)
    curr_warn = c.red .. "[" .. fmt(...) .. "]" .. c.reset
end

local function state_set(ctx, state)
    if not state_transitions[ctx.kind][ctx.state][state] then
        warn("invalid state %s → %s", ctx.state, state)
    end
    ctx.state = state
end

local function utrace_uprobe_init()
    local probe = read_ptr()
    local throw = read_ptr()
    local next = read_ptr()
    local pfx_name = read_str()
    local next_ctx = context_get(next)
    local top_ctx = ctx_top()
    local outer_pipe
    if top_ctx then
        if top_ctx.kind == 'pipe' then
            outer_pipe = top_ctx
        elseif top_ctx.kind == 'pipe-control' then
            outer_pipe = top_ctx.pipe
        end
    end
    if pfx_name then
        log("uprobe_init", probe_new(probe), symbolize(throw),
            probe_get(next), qq(pfx_name))
    else
        log("uprobe_init", probe_new(probe), symbolize(throw),
            probe_get(next))
    end
    context_set(probe, {
        kind = 'probe', id = id_n.probe, addr = probe,
        throw = throw, next = next_ctx, outer_pipe = outer_pipe,
        pfx_name = pfx_name
    })
end

local function utrace_uprobe_clean()
    local probe = read_ptr()
    log("uprobe_clean", probe_get(probe))
    context_del(probe)
end

local log_colors = {
    verbose = c.white, debug = c.green, info = c.blue,
    notice = c.blue, warning = c.yellow, error = c.red
}

local function dump_log(o, log)
    if log.level < show_log then return end
    if show_prefix then
        local found
        for _, prefix in ipairs(log.prefixes) do
            if prefix == show_prefix then
                found = true
                break
            end
        end
        if not found then return end
    end
    local level = uprobe_log_level[log.level]
    o:write(log_colors[level], fmt("%7s", level), c.reset, ": ")
    for _, prefix in ipairs(log.prefixes) do
        o:write(c.bold, c.black, "[", c.reset,
                c.cyan, prefix,
                c.bold, c.black, "]", c.reset, " ")
    end
    o:write(log.msg, "\n")
end

local curr_log

local function utrace_uprobe_throw_enter()
    local probe = read_ptr()
    local pipe = read_ptr()
    local event = read_int()
    local ctx = context_get(probe)
    local args = { uprobe_event[event] or event }
    if event == uprobe_event.log then
        local log = read_ptr()
        pushf(args, "%s", log_get(log))
        curr_log = log
    end
    log_enter("uprobe_throw", probe_get(probe), pipe_get(pipe),
        unpack(args))
    ctx_push(ctx)
end

local function utrace_uprobe_throw_leave()
    local ctx = ctx_pop()
    local err = read_int()
    if err == ubase_err.none then
        if curr_log then
            if not ctx.pfx_name then
                dump_log(io.stdout, context_get(curr_log))
            end
            context_del(curr_log)
            curr_log = nil
        end
    end
    log_leave("uprobe_throw", ubase_err[err] or err)
end

local function utrace_upipe_alloc_enter()
    local mgr = read_ptr()
    local mgr_sig = read_sig()
    local alloc = read_ptr()
    local probe = read_ptr()
    local sig = read_sig()
    local probe_ctx = context_get(probe)
    local mgr_ctx = mgr_get('pipe-mgr', mgr)
    local mgr_name = symbolize(alloc):gsub("_alloc$", ""):gsub("^_", "")
    mgr_ctx.sig = mgr_sig
    mgr_ctx.name = mgr_name
    log_enter("upipe_alloc", mgr_ctx, q(mgr_sig), mgr_name, probe_get(probe), q(sig))
    local outer_pipe
    local top_ctx = ctx_top()
    if top_ctx then
        if top_ctx.kind == "pipe" then
            outer_pipe = top_ctx
        elseif top_ctx.kind == "pipe-control" then
            outer_pipe = top_ctx.pipe
        elseif top_ctx.kind == "probe" then
            outer_pipe = top_ctx.outer_pipe
        end
    end
    ctx_push({
        kind = 'pipe', mgr = mgr_ctx, sig = mgr_sig,
        probe = probe_ctx, state = 'alloc', outer_pipe = outer_pipe,
        sub_pipes = {}, inner_pipes = {}
    })
end

local function utrace_upipe_alloc_leave()
    local ctx = ctx_pop()
    local pipe = read_ptr()
    log_leave("upipe_alloc", pipe_get(pipe))
end

local function utrace_upipe_init()
    local pipe = read_ptr()
    local mgr = read_ptr()
    local probe = read_ptr()
    local ctx = ctx_top()
    local mgr_ctx = mgr_get('pipe-mgr', mgr)
    local prefix
    local probe_ctx = context_get(probe)
    while probe_ctx and not prefix do
        prefix = probe_ctx.pfx_name
        probe_ctx = probe_ctx.next
    end
    state_set(ctx, 'init')
    log("upipe_init", pipe_new(pipe), mgr_ctx, probe_get(probe), qq(prefix))
    ctx.addr = pipe
    ctx.id = id_n.pipe
    ctx.super_pipe = ctx.mgr.super
    if ctx.super_pipe then insert(ctx.super_pipe.sub_pipes, ctx) end
    if ctx.outer_pipe then insert(ctx.outer_pipe.inner_pipes, ctx) end
    ctx.name = prefix
    context_set(pipe, ctx)
end

local function utrace_upipe_clean()
    local pipe = read_ptr()
    state_set(context_get(pipe), 'clean')
    log("upipe_clean", pipe_get(pipe))
    context_del(pipe)
end

local function utrace_upipe_control_enter()
    local pipe = read_ptr()
    local command = read_int()
    local ctx = { kind = "pipe-control", pipe = context_get(pipe) }
    local args = { upipe_command[command] or command }
    if command == upipe_command.set_uri then
        local uri = read_str()
        pushf(args, "%q", uri)
        ctx.uri = uri
    elseif command == upipe_command.get_option then
        local key = read_str()
        pushf(args, "%s", key)
    elseif command == upipe_command.set_option then
        local key = read_str()
        local val = read_str()
        pushf(args, "%s", key)
        pushf(args, "%q", val)
        ctx.option = { key=key, val=val }
    elseif command == upipe_command.register_request then
        local request = read_ptr()
        pushf(args, "%s", request_get(request))
    elseif command == upipe_command.unregister_request then
        local request = read_ptr()
        pushf(args, "%s", request_get(request))
    elseif command == upipe_command.set_flow_def then
        local ref = read_uref()
        pushf(args, "%s", ref.flow_def)
        ctx.flow_def = ref.flow_def
        curr_uref = ref
    elseif command == upipe_command.set_max_length then
        local max_length = read_uint()
        pushf(args, "%u", max_length)
        ctx.max_length = max_length
    elseif command == upipe_command.set_output then
        local pipe = read_ptr()
        pushf(args, "%s", pipe_get(pipe))
        ctx.output = context_get(pipe)
    elseif command == upipe_command.set_output_size then
        local size = read_uint()
        pushf(args, "%u", size)
        ctx.output_size = size
    elseif command == upipe_command.src_set_position then
        local position = read_uint()
        pushf(args, "%u", position)
        ctx.position = position
    elseif command == upipe_command.src_set_range then
        local position = read_uint()
        local length = read_uint()
        pushf(args, "%u %u", position, length)
        ctx.range = { position = position, length = length }
    elseif command >= 0x8000 then
        local sig = read_sig()
        pushf(args, "'%s'", sig)
    end
    log_enter("upipe_control", pipe_get(pipe), unpack(args))
    ctx_push(ctx)
end

local function utrace_upipe_control_leave()
    local ctx = ctx_pop()
    local err = read_int()
    local command = read_int()
    local args = { ubase_err[err] or err }
    if err == ubase_err.none then
        if command == upipe_command.get_uri then
            local uri = read_str()
            pushf(args, "%q", uri)
        elseif command == upipe_command.set_uri then
            ctx.pipe.uri = ctx.uri
        elseif command == upipe_command.get_option then
            local val = read_str()
            pushf(args, " %q", val)
        elseif command == upipe_command.set_option then
            if not ctx.pipe.options then ctx.pipe.options = {} end
            ctx.pipe.options[ctx.option.key] = ctx.option.val
        elseif command == upipe_command.set_flow_def then
            ctx.pipe.flow_def_input = ctx.flow_def
        elseif command == upipe_command.get_max_length then
            local max_length = read_uint()
            pushf(args, "%u", max_length)
        elseif command == upipe_command.set_max_length then
            ctx.pipe.max_length = ctx.max_length
        elseif command == upipe_command.get_output then
            local pipe = read_ptr()
            pushf(args, "%s", pipe_get(pipe))
        elseif command == upipe_command.set_output then
            ctx.pipe.output = ctx.output
        elseif command == upipe_command.get_flow_def then
            local ref = read_uref()
            pushf(args, "%s", ref.flow_def)
            curr_uref = ref
        elseif command == upipe_command.get_output_size then
            local size = read_uint()
            pushf(args, "%u", size)
        elseif command == upipe_command.set_output_size then
            ctx.pipe.output_size = ctx.output_size
        elseif command == upipe_command.split_iterate then
            local ref = read_uref()
            pushf(args, "%s", ref.flow_def)
            curr_uref = ref
        elseif command == upipe_command.get_sub_mgr then
            local mgr = read_ptr()
            ctx.pipe.sub_mgr = mgr_get('pipe-mgr', mgr)
            ctx.pipe.sub_mgr.super = ctx.pipe
            pushf(args, "%s", ctx.pipe.sub_mgr)
        elseif command == upipe_command.iterate_sub then
            local pipe = read_ptr()
            pushf(args, "%s", pipe_get(pipe))
        elseif command == upipe_command.sub_get_super then
            local pipe = read_ptr()
            pushf(args, "%s", pipe_get(pipe))
        elseif command == upipe_command.bin_get_first_inner then
            local pipe = read_ptr()
            pushf(args, "%s", pipe_get(pipe))
        elseif command == upipe_command.bin_get_last_inner then
            local pipe = read_ptr()
            pushf(args, "%s", pipe_get(pipe))
        elseif command == upipe_command.src_get_size then
            local size = read_uint()
            pushf(args, "%u", size)
        elseif command == upipe_command.src_get_position then
            local position = read_uint()
            pushf(args, "%u", position)
        elseif command == upipe_command.src_set_position then
            ctx.pipe.position = ctx.position
        elseif command == upipe_command.src_set_range then
            ctx.pipe.range = ctx.range
        elseif command == upipe_command.src_get_range then
            local position = read_uint()
            local length = read_uint()
            pushf(args, "%u %u", position, length)
        end
    end
    log_leave("upipe_control", unpack(args))
end

local function utrace_upipe_throw_enter()
    local pipe = read_ptr()
    local probe = read_ptr()
    local event = read_int()
    local ctx = context_get(pipe)
    local args = { uprobe_event[event] or event }
    local state

    if event == uprobe_event.log then
        local log = read_ptr()
        pushf(args, "%s", log_get(log))
    elseif event == uprobe_event.fatal then
        state = 'fatal'
        local err = read_int()
        pushf(args, "%s", ubase_err[err] or err)
    elseif event == uprobe_event.error then
        state = 'error'
        local err = read_int()
        pushf(args, "%s", ubase_err[err] or err)
    elseif event == uprobe_event.ready then
        state = 'ready'
    elseif event == uprobe_event.dead then
        state = 'dead'
    elseif event == uprobe_event.sink_end then
        local err = read_str()
        pushf(args, "%s", err)
    elseif event == uprobe_event.need_output then
        local ref = read_uref()
        pushf(args, "%s", ref.flow_def)
        curr_uref = ref
    elseif event == uprobe_event.provide_request then
        local request = read_ptr()
        pushf(args, "%s", request_get(request))
    elseif event == uprobe_event.new_flow_def then
        local ref = read_uref()
        pushf(args, "%s", ref.flow_def)
        ctx.flow_def = ref.flow_def
        curr_uref = ref
    elseif event == uprobe_event.new_rap then
        local ref = read_uref()
        pushf(args, "%s", ref.flow_def)
        curr_uref = ref
    elseif event == uprobe_event.sync_acquired then
        ctx.sync = true
    elseif event == uprobe_event.sync_lost then
        ctx.sync = false
    elseif event == uprobe_event.clock_ref then
        local ref = read_uref()
        local clock_ref = read_uint()
        local discontinuity = read_int()
        pushf(args, "%u %d", clock_ref, discontinuity)
        curr_uref = ref
    elseif event == uprobe_event.clock_ts then
        local ref = read_uref()
        curr_uref = ref
    elseif event == uprobe_event.clock_utc then
        local ref = read_uref()
        local clock_utc = read_uint()
        pushf(args, "%u", clock_utc)
        curr_uref = ref
    elseif event >= 0x8000 then
        local sig = read_sig()
        pushf(args, "'%s'", sig)
    end
    if state then
        state_set(ctx, state)
    else
        if not (ctx.state == 'ready' or
                ctx.state == 'error' or
                probe == 0) then
            warn("invalid state %s", it(ctx.state))
        end
    end
    log_enter("upipe_throw", pipe_get(pipe), probe_get(probe), unpack(args))
    ctx_push(ctx)
end

local function utrace_upipe_throw_leave()
    local ctx = ctx_pop()
    local err = read_int()
    local event = read_int()
    local args = { ubase_err[err] or err }
    if err == ubase_err.none then
        if event == uprobe_event.need_upump_mgr then
            local mgr = read_ptr()
            local mgr_ctx = mgr_get('pump-mgr', mgr)
            pushf(args, "%s", mgr_ctx)
        elseif event == uprobe_event.need_source_mgr then
            local mgr = read_ptr()
            local mgr_ctx = mgr_get('pipe-mgr', mgr)
            pushf(args, "%s", mgr_ctx)
        end
    end
    log_leave("upipe_throw", unpack(args))
end

local function utrace_upipe_input_enter()
    local pipe = read_ptr()
    local ctx = context_get(pipe)
    log_enter("upipe_input", pipe_get(pipe))
    ctx_push(ctx)
end

local function utrace_upipe_input_leave()
    local ctx = ctx_pop()
    log_leave("upipe_input")
end

local function utrace_urequest_init()
    local request = read_ptr()
    local type = read_int()
    type = urequest_type[type] or type
    curr_uref = read_uref()
    log("urequest_init", request_new(request), type)
    context_set(request, {
        kind = 'request', addr = request, id = id_n.request,
        type = type, state = 'init'
    })
end

local function utrace_urequest_clean()
    local request = read_ptr()
    state_set(context_get(request), 'clean')
    log("urequest_clean", request_get(request))
end

local function utrace_urequest_free()
    local request = read_ptr()
    state_set(context_get(request), 'free')
    log("urequest_free", request_get(request))
    context_del(request)
end

local function utrace_urequest_provide_enter()
    local request = read_ptr()
    local ctx = context_get(request)
    local args = {}
    if ctx.type == 'uref_mgr' then
        local mgr = read_ptr()
        local mgr_ctx = mgr_get('ref-mgr', mgr)
        pushf(args, "%s", mgr_ctx)
    elseif ctx.type == 'flow_format' then
        local ref = read_uref()
        curr_uref = ref
        pushf(args, "%s", ref.flow_def)
    elseif ctx.type == 'ubuf_mgr' then
        local mgr = read_ptr()
        local mgr_ctx = mgr_get('buf-mgr', mgr)
        local ref = read_uref()
        curr_uref = ref
        pushf(args, "%s", mgr_ctx)
        pushf(args, "%s", ref.flow_def)
    end
    log_enter("urequest_provide", request_get(request), unpack(args))
    ctx_push(ctx)
end

local function utrace_urequest_provide_leave()
    local ctx = ctx_pop()
    local err = read_int()
    log_leave("urequest_provide", ubase_err[err] or err)
end

local function utrace_upump_alloc_enter()
    local mgr = read_ptr()
    local mgr_sig = read_sig()
    local alloc = read_ptr()
    local event = read_int()
    local mgr_ctx = mgr_get('pump-mgr', mgr)
    local mgr_name = symbolize(alloc):gsub("_alloc$", ""):gsub("^_", "")
    mgr_ctx.sig = mgr_sig
    mgr_ctx.name = mgr_name
    local args = { upump_event[event] or event }
    if event == upump_event.timer then
        local time_after = read_uint()
        local time_repeat = read_uint()
        pushf(args, "%.3f", tonumber(time_after) / UCLOCK_FREQ)
        pushf(args, "%.3f", tonumber(time_repeat) / UCLOCK_FREQ)
    elseif event == upump_event.fd_read then
        local fd = read_int()
        pushf(args, "%s", fd)
    elseif event == upump_event.fd_write then
        local fd = read_int()
        pushf(args, "%s", fd)
    elseif event == upump_event.signal then
        local signal = read_int()
        pushf(args, "%s", signal)
    end
    log_enter("upump_alloc", mgr_ctx, q(mgr_sig), mgr_name, unpack(args))
    local outer_pipe
    local top_ctx = ctx_top()
    if top_ctx then
        if top_ctx.kind == "pipe" then
            outer_pipe = top_ctx
        elseif top_ctx.kind == "pipe-control" then
            outer_pipe = top_ctx.pipe
        elseif top_ctx.kind == "probe" then
            outer_pipe = top_ctx.outer_pipe
        end
    end
    ctx_push({
        kind = 'pump', mgr = mgr_ctx, sig = mgr_sig,
        outer_pipe = outer_pipe,
    })
end

local function utrace_upump_alloc_leave()
    local ctx = ctx_pop()
    local pump = read_ptr()
    local cb = read_ptr()
    log_leave("upump_alloc", pump_new(pump), symbolize(cb))
    ctx.addr = pump
    ctx.id = id_n.pump
    context_set(pump, ctx)
end

local function utrace_upump_control_enter()
    local pump = read_ptr()
    local command = read_int()
    local ctx = { kind = "pump-control", pump = context_get(pump) }
    local args = { upump_command[command] or command }
    if command == upump_command.set_status then
        local status = read_int()
        pushf(args, "%s", status ~= 0)
        ctx.status = status
    elseif command == upipe_command.free_blocker then
        local blocker = read_ptr()
        pushf(args, "%s", hex(blocker))
    elseif command >= 0x8000 then
        local sig = read_sig()
        pushf(args, "'%s'", sig)
    end
    log_enter("upump_control", pump_get(pump), unpack(args))
    ctx_push(ctx)
end

local function utrace_upump_control_leave()
    local ctx = ctx_pop()
    local err = read_int()
    local command = read_int()
    local args = { ubase_err[err] or err }
    if err == ubase_err.none then
        if command == upump_command.free then
            context_del(ctx.pump.addr)
        elseif command == upump_command.get_status then
            local status = read_int()
            pushf(args, "%s", status ~= 0)
        elseif command == upump_command.set_status then
            ctx.pump.status = ctx.status
        elseif command == upump_command.alloc_blocker then
            local blocker = read_ptr()
            pushf(args, "%s", hex(blocker))
        end
    end
    log_leave("upump_control", unpack(args))
end

local function utrace_ulog_init()
    local ulog = read_ptr()
    local level = read_uint()
    local msg = read_str()
    log("ulog_init", log_new(ulog), uprobe_log_level[level], qq(msg))
    context_set(ulog, {
        kind = 'log', id = id_n.log, addr = ulog,
        level = level, msg = msg, prefixes = {}
    })
end

local function utrace_ulog_add_prefix()
    local ulog = read_ptr()
    local prefix = read_str()
    local ctx = context_get(ulog)
    insert(ctx.prefixes, 1, prefix)
    log("ulog_add_prefix", log_get(ulog), qq(prefix))
end

local id_handlers = {
    utrace_dump_graph,

    -- uprobe
    utrace_uprobe_init,
    utrace_uprobe_clean,
    utrace_uprobe_throw_enter,
    utrace_uprobe_throw_leave,

    -- upipe
    utrace_upipe_alloc_enter,
    utrace_upipe_alloc_leave,
    utrace_upipe_init,
    utrace_upipe_clean,
    utrace_upipe_control_enter,
    utrace_upipe_control_leave,
    utrace_upipe_throw_enter,
    utrace_upipe_throw_leave,
    utrace_upipe_input_enter,
    utrace_upipe_input_leave,

    -- urequest
    utrace_urequest_init,
    utrace_urequest_clean,
    utrace_urequest_free,
    utrace_urequest_provide_enter,
    utrace_urequest_provide_leave,

    -- upump
    utrace_upump_alloc_enter,
    utrace_upump_alloc_leave,
    utrace_upump_control_enter,
    utrace_upump_control_leave,

    -- ulog
    utrace_ulog_init,
    utrace_ulog_add_prefix,
}

utrace_init()

while true do
    local id = read_id()
    if id == -1 then break end
    id_handlers[id + 1]()
end

log_flush()
assert(ctx_stack[1] == nil)
ffi.C.fclose(f)

if not show_leaks then return end

local function ranges_list(v)
    local t = {}
    local r_start, r_end
    for i, v in ipairs(v) do
        if i == 1 then
            r_start, r_end = v, v
        elseif v == r_end + 1 then
            r_end = v
        else
            if r_start == r_end then
                insert(t, r_start)
            else
                insert(t, r_start .. "-" .. r_end)
            end
            r_start, r_end = v, v
        end
    end
    if r_start then
        if r_start == r_end then
            insert(t, r_start)
        else
            insert(t, r_start .. "-" .. r_end)
        end
    end
    return concat(t, ",")
end

local function dump_ids()
    local ids = { pipe={}, probe={}, request={}, pump={} }

    for addr, id in pairs(id_d) do
        id:gsub("(.+)-(%d+)", function (cat, n)
            insert(ids[cat], tonumber(n))
        end)
    end

    for cat in pairs(ids) do
        if next(ids[cat]) then
            table.sort(ids[cat])
            print(fmt("%s: %s", cat, ranges_list(ids[cat])))
        end
    end
end

if next(id_d) then
    print("\n=== LEAKS ===")

    for _, ctx in pairs(context) do
        if ctx.kind == 'pipe' then
            if ctx.probe then id_del(ctx.probe.addr) end
            if ctx.output then id_del(ctx.output.addr) end
            if ctx.super_pipe then id_del(ctx.super_pipe.addr) end
        elseif ctx.kind == 'probe' then
            if ctx.next then id_del(ctx.next.addr) end
        end
    end

    dump_ids()
end
