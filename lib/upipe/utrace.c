/*
 * Copyright (C) 2020-2026 EasyTools S.A.S.
 *
 * Authors: Clément Vasseur
 *
 * SPDX-License-Identifier: MIT
 */

#define _GNU_SOURCE

#include "config.h"
#include "upipe/udict.h"
#include "upipe/uprobe.h"
#include "upipe/upipe.h"
#include "upipe/upump.h"
#include "upipe/ulog.h"
#include "upipe/uprobe_prefix.h"

#ifdef HAVE_UTRACE

#include <assert.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <link.h>
#include <limits.h>

#define UTRACE_MAGIC "UTRACE01"

enum utrace_id {
    UTRACE_DUMP_GRAPH,

    /* uprobe */
    UTRACE_UPROBE_INIT,
    UTRACE_UPROBE_CLEAN,
    UTRACE_UPROBE_THROW_ENTER,
    UTRACE_UPROBE_THROW_LEAVE,

    /* upipe */
    UTRACE_UPIPE_ALLOC_ENTER,
    UTRACE_UPIPE_ALLOC_LEAVE,
    UTRACE_UPIPE_INIT,
    UTRACE_UPIPE_CLEAN,
    UTRACE_UPIPE_CONTROL_ENTER,
    UTRACE_UPIPE_CONTROL_LEAVE,
    UTRACE_UPIPE_THROW_ENTER,
    UTRACE_UPIPE_THROW_LEAVE,
    UTRACE_UPIPE_INPUT_ENTER,
    UTRACE_UPIPE_INPUT_LEAVE,

    /* urequest */
    UTRACE_UREQUEST_INIT,
    UTRACE_UREQUEST_CLEAN,
    UTRACE_UREQUEST_FREE,
    UTRACE_UREQUEST_PROVIDE_ENTER,
    UTRACE_UREQUEST_PROVIDE_LEAVE,

    /* upump */
    UTRACE_UPUMP_ALLOC_ENTER,
    UTRACE_UPUMP_ALLOC_LEAVE,
    UTRACE_UPUMP_CONTROL_ENTER,
    UTRACE_UPUMP_CONTROL_LEAVE,

    /* ulog */
    UTRACE_ULOG_INIT,
    UTRACE_ULOG_ADD_PREFIX,
};

static bool utrace_initialized;
static FILE *utrace_f;

static void utrace_init(void);

static void utrace_write(const void *buf, size_t len)
{
    utrace_init();
    if (utrace_f != NULL && len > 0)
        assert(fwrite_unlocked(buf, len, 1, utrace_f) == 1);
}

static void utrace_write_uint(uint64_t val)
{
    uint8_t buf[10];

    /* ULEB128 encoding */
    unsigned i;
    for (i = 0; val >= 0x80; val >>= 7)
        buf[i++] = val | 0x80;
    buf[i++] = val;

    utrace_write(buf, i);
}

static void utrace_write_int(int64_t val)
{
    /* zigzag encoding */
    utrace_write_uint(((uint64_t) val << 1) ^ (val >> 63));
}

static void utrace_write_double(double val)
{
    union {
        double d;
        uint64_t u64;
    } u = { .d = val };

    utrace_write_uint(u.u64);
}

static void utrace_write_sig(uint32_t sig)
{
    uint8_t buf[4];

    for (unsigned i = 0; i < 4; i++)
        buf[i] = sig >> i * 8;

    utrace_write(buf, sizeof buf);
}

static void utrace_write_ptr(void *val)
{
    utrace_write_uint((uintptr_t) val);
}

static void utrace_write_id(enum utrace_id val)
{
    if (utrace_f != NULL)
        flockfile(utrace_f);
    utrace_write_uint(val);
}

static void utrace_write_str(const char *val)
{
    if (val == NULL) {
        utrace_write_int(-1);
        return;
    }
    int len = strlen(val);
    utrace_write_int(len);
    utrace_write(val, len);
}

static void utrace_write_udict(struct udict *udict)
{
    const char *iname = NULL;
    enum udict_type itype = UDICT_TYPE_END;

    while (udict && ubase_check(udict_iterate(udict, &iname, &itype)) &&
           itype != UDICT_TYPE_END) {
        const char *name;
        enum udict_type type;
        if (!ubase_check(udict_name(udict, itype, &name, &type))) {
            name = iname;
            type = itype;
        }

        utrace_write_uint(type);
        utrace_write_str(name);

        switch (type) {
            case UDICT_TYPE_OPAQUE: {
                struct udict_opaque val;
                ubase_assert(udict_get_opaque(udict, &val, itype, iname));
                utrace_write_uint(val.size);
                utrace_write(val.v, val.size);
                break;
            }
            case UDICT_TYPE_STRING: {
                const char *val = "";
                ubase_assert(udict_get_string(udict, &val, itype, iname));
                utrace_write_str(val);
                break;
            }
            case UDICT_TYPE_VOID:
                break;
            case UDICT_TYPE_BOOL: {
                bool val = false;
                ubase_assert(udict_get_bool(udict, &val, itype, iname));
                utrace_write_uint(val);
                break;
            }
            case UDICT_TYPE_RATIONAL: {
                struct urational val = {0};
                ubase_assert(udict_get_rational(udict, &val, itype, iname));
                utrace_write_int(val.num);
                utrace_write_uint(val.den);
                break;
            }
            case UDICT_TYPE_SMALL_UNSIGNED: {
                uint8_t val = 0;
                ubase_assert(udict_get_small_unsigned(udict, &val, itype, iname));
                utrace_write_uint(val);
                break;
            }
            case UDICT_TYPE_SMALL_INT: {
                int8_t val = 0;
                ubase_assert(udict_get_small_int(udict, &val, itype, iname));
                utrace_write_uint(val);
                break;
            }
            case UDICT_TYPE_UNSIGNED: {
                uint64_t val = 0;
                ubase_assert(udict_get_unsigned(udict, &val, itype, iname));
                utrace_write_uint(val);
                break;
            }
            case UDICT_TYPE_INT: {
                int64_t val = 0;
                ubase_assert(udict_get_int(udict, &val, itype, iname));
                utrace_write_uint(val);
                break;
            }
            case UDICT_TYPE_FLOAT: {
                double val = 0;
                ubase_assert(udict_get_float(udict, &val, itype, iname));
                utrace_write_double(val);
                break;
            }
            default:
                fprintf(stderr, "utrace: invalid udict type %u\n", type);
                exit(1);
        }
    }
    utrace_write_uint(itype);
}

static void utrace_write_uref(struct uref *uref)
{
    utrace_write_ptr(uref);
    if (uref != NULL)
        utrace_write_udict(uref->udict);
}

static void utrace_end(void)
{
    if (utrace_f != NULL)
        funlockfile(utrace_f);
}

static int handle_phdr(struct dl_phdr_info *info, size_t size, void *data)
{
    const char *name = info->dlpi_name;
    char path[PATH_MAX];

    if (name == NULL || name[0] == '\0') {
        int ret = readlink("/proc/self/exe", path, sizeof path);
        assert(ret != -1 && ret < sizeof path);
        path[ret] = '\0';
        name = path;
    }

    utrace_write_uint(info->dlpi_addr);
    utrace_write_str(name);
    return 0;
}

static void utrace_init(void)
{
    if (!utrace_initialized) {
        utrace_initialized = true;
        const char *fd_str = getenv("UTRACE_FD");
        if (fd_str == NULL)
            return;
        int fd = atoi(fd_str);
        utrace_f = fdopen(fd, "w");
        assert(utrace_f);
        fprintf(stderr, "upipe: tracing enabled on fd %d\n", fd);

        utrace_write(UTRACE_MAGIC, 8);
        dl_iterate_phdr(handle_phdr, NULL);
        utrace_write_ptr(NULL);
    }
}

#define w_str()  utrace_write_str(va_arg(ap, const char *))
#define w_ptr(T) utrace_write_ptr(va_arg(ap, T *))
#define w_uref() utrace_write_uref(va_arg(ap, struct uref *))
#define w_int()  utrace_write_int(va_arg(ap, int))
#define w_uint() utrace_write_uint(va_arg(ap, unsigned int))
#define w_u64()  utrace_write_uint(va_arg(ap, uint64_t))
#define w_sig()  utrace_write_sig(va_arg(ap, uint32_t))

#define s_str()  va_arg(ap, const char **)
#define r_str()  utrace_write_str(*(va_arg(ap, const char **)))
#define r_ptr(T) utrace_write_ptr(*(va_arg(ap, T **)))
#define r_uref() utrace_write_uref(*(va_arg(ap, struct uref **)))
#define r_int()  utrace_write_int(*(va_arg(ap, int *)))
#define r_uint() utrace_write_uint(*(va_arg(ap, unsigned int *)))
#define r_u64()  utrace_write_uint(*(va_arg(ap, uint64_t *)))

void utrace_uprobe_init(struct uprobe *uprobe)
{
    utrace_write_id(UTRACE_UPROBE_INIT);
    utrace_write_ptr(uprobe);
    utrace_write_ptr(uprobe->uprobe_throw);
    utrace_write_ptr(uprobe->next);
    utrace_write_str(uprobe_pfx_get_name(uprobe));
    utrace_end();
}

void utrace_uprobe_clean(struct uprobe *uprobe)
{
    utrace_write_id(UTRACE_UPROBE_CLEAN);
    utrace_write_ptr(uprobe);
    utrace_end();
}

void utrace_uprobe_throw_enter(struct uprobe *uprobe,
                               struct upipe *upipe,
                               int event, va_list args)
{
    va_list ap;

    utrace_write_id(UTRACE_UPROBE_THROW_ENTER);
    utrace_write_ptr(uprobe);
    utrace_write_ptr(upipe);
    utrace_write_int(event);
    va_copy(ap, args);
    switch (event) {
        case UPROBE_LOG: w_ptr(struct ulog); break;
    }
    va_end(ap);
    utrace_end();
}

void utrace_uprobe_throw_leave(int err)
{
    utrace_write_id(UTRACE_UPROBE_THROW_LEAVE);
    utrace_write_int(err);
    utrace_end();
}

void utrace_upipe_alloc_enter(struct upipe_mgr *mgr,
                              struct uprobe *uprobe,
                              uint32_t signature)
{
    utrace_write_id(UTRACE_UPIPE_ALLOC_ENTER);
    utrace_write_ptr(mgr);
    utrace_write_sig(mgr->signature);
    utrace_write_ptr(mgr->upipe_alloc);
    utrace_write_ptr(uprobe);
    utrace_write_sig(signature);
    utrace_end();
}

void utrace_upipe_alloc_leave(struct upipe *upipe)
{
    utrace_write_id(UTRACE_UPIPE_ALLOC_LEAVE);
    utrace_write_ptr(upipe);
    utrace_end();
}

void utrace_upipe_init(struct upipe *upipe)
{
    utrace_write_id(UTRACE_UPIPE_INIT);
    utrace_write_ptr(upipe);
    utrace_write_ptr(upipe->mgr);
    utrace_write_ptr(upipe->uprobe);
    utrace_end();
}

void utrace_upipe_clean(struct upipe *upipe)
{
    utrace_write_id(UTRACE_UPIPE_CLEAN);
    utrace_write_ptr(upipe);
    utrace_end();
}

void utrace_upipe_control_enter(struct upipe *upipe,
                                int command, va_list args)
{
    va_list ap;

    utrace_write_id(UTRACE_UPIPE_CONTROL_ENTER);
    utrace_write_ptr(upipe);
    utrace_write_int(command);
    va_copy(ap, args);
    switch (command) {
        case UPIPE_ATTACH_UREF_MGR: break;
        case UPIPE_ATTACH_UPUMP_MGR: break;
        case UPIPE_ATTACH_UCLOCK: break;
        case UPIPE_GET_URI: break;
        case UPIPE_SET_URI: w_str(); break;
        case UPIPE_GET_OPTION: w_str(); break;
        case UPIPE_SET_OPTION: w_str(); w_str(); break;
        case UPIPE_REGISTER_REQUEST: w_ptr(struct urequest); break;
        case UPIPE_UNREGISTER_REQUEST: w_ptr(struct urequest); break;
        case UPIPE_SET_FLOW_DEF: w_uref(); break;
        case UPIPE_GET_MAX_LENGTH: break;
        case UPIPE_SET_MAX_LENGTH: w_uint(); break;
        case UPIPE_FLUSH: break;
        case UPIPE_END_PREROLL: break;
        case UPIPE_GET_OUTPUT: break;
        case UPIPE_SET_OUTPUT: w_ptr(struct upipe); break;
        case UPIPE_ATTACH_UBUF_MGR: break;
        case UPIPE_GET_FLOW_DEF: break;
        case UPIPE_GET_OUTPUT_SIZE: break;
        case UPIPE_SET_OUTPUT_SIZE: w_uint(); break;
        case UPIPE_SPLIT_ITERATE: break;
        case UPIPE_GET_SUB_MGR: break;
        case UPIPE_ITERATE_SUB: break;
        case UPIPE_SUB_GET_SUPER: break;
        case UPIPE_BIN_FREEZE: break;
        case UPIPE_BIN_THAW: break;
        case UPIPE_BIN_GET_FIRST_INNER: break;
        case UPIPE_BIN_GET_LAST_INNER: break;
        case UPIPE_SRC_GET_SIZE: break;
        case UPIPE_SRC_GET_POSITION: break;
        case UPIPE_SRC_SET_POSITION: w_u64(); break;
        case UPIPE_SRC_SET_RANGE: w_u64(); w_u64(); break;
        case UPIPE_SRC_GET_RANGE: break;

        default:
            assert(command >= UPIPE_CONTROL_LOCAL);
            w_sig();
    }
    va_end(ap);
    utrace_end();
}

void utrace_upipe_control_leave(int err, int command, va_list args)
{

    utrace_write_id(UTRACE_UPIPE_CONTROL_LEAVE);
    utrace_write_int(err);
    utrace_write_int(command);
    if (err == UBASE_ERR_NONE) {
        va_list ap;
        va_copy(ap, args);
        switch (command) {
            case UPIPE_GET_URI: r_str(); break;
            case UPIPE_GET_OPTION: s_str(); r_str(); break;
            case UPIPE_GET_MAX_LENGTH: r_uint(); break;
            case UPIPE_GET_OUTPUT: r_ptr(struct upipe); break;
            case UPIPE_GET_FLOW_DEF: r_uref(); break;
            case UPIPE_GET_OUTPUT_SIZE: r_uint(); break;
            case UPIPE_SPLIT_ITERATE: r_uref(); break;
            case UPIPE_GET_SUB_MGR: r_ptr(struct upipe_mgr); break;
            case UPIPE_ITERATE_SUB: r_ptr(struct upipe); break;
            case UPIPE_SUB_GET_SUPER: r_ptr(struct upipe); break;
            case UPIPE_BIN_GET_FIRST_INNER: r_ptr(struct upipe); break;
            case UPIPE_BIN_GET_LAST_INNER: r_ptr(struct upipe); break;
            case UPIPE_SRC_GET_SIZE: r_u64(); break;
            case UPIPE_SRC_GET_POSITION: r_u64(); break;
            case UPIPE_SRC_GET_RANGE: r_u64(); r_u64(); break;
        }
        va_end(ap);
    }
    utrace_end();
}

void utrace_upipe_throw_enter(struct upipe *upipe, int event, va_list args)
{
    va_list ap;

    utrace_write_id(UTRACE_UPIPE_THROW_ENTER);
    utrace_write_ptr(upipe);
    utrace_write_ptr(upipe->uprobe);
    utrace_write_int(event);
    va_copy(ap, args);
    switch (event) {
        case UPROBE_LOG: w_ptr(struct ulog); break;
        case UPROBE_FATAL: w_int(); break;
        case UPROBE_ERROR: w_int(); break;
        case UPROBE_READY: break;
        case UPROBE_DEAD: break;
        case UPROBE_STALLED: break;
        case UPROBE_SOURCE_END: break;
        case UPROBE_SINK_END: w_str(); break;
        case UPROBE_NEED_OUTPUT: w_uref(); break;
        case UPROBE_PROVIDE_REQUEST: w_ptr(struct urequest); break;
        case UPROBE_NEED_UPUMP_MGR: break;
        case UPROBE_FREEZE_UPUMP_MGR: break;
        case UPROBE_THAW_UPUMP_MGR: break;
        case UPROBE_NEED_SOURCE_MGR: break;
        case UPROBE_NEW_FLOW_DEF: w_uref(); break;
        case UPROBE_NEW_RAP: w_uref(); break;
        case UPROBE_SPLIT_UPDATE: break;
        case UPROBE_SYNC_ACQUIRED: break;
        case UPROBE_SYNC_LOST: break;
        case UPROBE_CLOCK_REF: w_uref(); w_u64(); w_int(); break;
        case UPROBE_CLOCK_TS: w_uref(); break;
        case UPROBE_CLOCK_UTC: w_uref(); w_u64(); break;
        case UPROBE_PREROLL_END: break;

        default:
            assert(event >= UPROBE_LOCAL);
            w_sig();
    }
    va_end(ap);
    utrace_end();
}

void utrace_upipe_throw_leave(int err, int event, va_list args)
{
    utrace_write_id(UTRACE_UPIPE_THROW_LEAVE);
    utrace_write_int(err);
    utrace_write_int(event);
    if (err == UBASE_ERR_NONE) {
        va_list ap;
        va_copy(ap, args);
        switch (event) {
            case UPROBE_NEED_UPUMP_MGR: r_ptr(struct upump_mgr); break;
            case UPROBE_NEED_SOURCE_MGR: r_ptr(struct upipe_mgr); break;
        }
        va_end(ap);
    }
    utrace_end();
}

void utrace_upipe_input_enter(struct upipe *upipe, struct uref *uref)
{
    utrace_write_id(UTRACE_UPIPE_INPUT_ENTER);
    utrace_write_ptr(upipe);
    utrace_end();
}

void utrace_upipe_input_leave(void)
{
    utrace_write_id(UTRACE_UPIPE_INPUT_LEAVE);
    utrace_end();
}

void utrace_urequest_init(struct urequest *urequest)
{
    utrace_write_id(UTRACE_UREQUEST_INIT);
    utrace_write_ptr(urequest);
    utrace_write_int(urequest->type);
    utrace_write_uref(urequest->uref);
    utrace_end();
}

void utrace_urequest_clean(struct urequest *urequest)
{
    utrace_write_id(UTRACE_UREQUEST_CLEAN);
    utrace_write_ptr(urequest);
    utrace_end();
}

void utrace_urequest_free(struct urequest *urequest)
{
    utrace_write_id(UTRACE_UREQUEST_FREE);
    utrace_write_ptr(urequest);
    utrace_end();
}

void utrace_urequest_provide_enter(struct urequest *urequest, va_list args)
{
    va_list ap;

    utrace_write_id(UTRACE_UREQUEST_PROVIDE_ENTER);
    utrace_write_ptr(urequest);
    va_copy(ap, args);
    switch (urequest->type) {
        case UREQUEST_UREF_MGR: w_ptr(struct uref_mgr); break;
        case UREQUEST_FLOW_FORMAT: w_uref(); break;
        case UREQUEST_UBUF_MGR: w_ptr(struct ubuf_mgr); w_uref(); break;
        case UREQUEST_UCLOCK: w_ptr(struct uclock); break;
        case UREQUEST_SINK_LATENCY: w_u64(); break;

        default:
            assert(urequest->type >= UREQUEST_LOCAL);
            w_sig();
    }
    va_end(ap);
    utrace_end();
}

void utrace_urequest_provide_leave(int err)
{
    utrace_write_id(UTRACE_UREQUEST_PROVIDE_LEAVE);
    utrace_write_int(err);
    utrace_end();
}

void utrace_upump_alloc_enter(struct upump_mgr *mgr, int event, va_list args)
{
    va_list ap;

    utrace_write_id(UTRACE_UPUMP_ALLOC_ENTER);
    utrace_write_ptr(mgr);
    utrace_write_sig(mgr->signature);
    utrace_write_ptr(mgr->upump_alloc);
    utrace_write_int(event);
    va_copy(ap, args);
    switch (event) {
        case UPUMP_TYPE_IDLER: break;
        case UPUMP_TYPE_TIMER: w_u64(); w_u64(); break;
        case UPUMP_TYPE_FD_READ: w_int(); break;
        case UPUMP_TYPE_FD_WRITE: w_int(); break;
        case UPUMP_TYPE_SIGNAL: w_int(); break;

        default:
            assert(event >= UPUMP_TYPE_LOCAL);
            w_sig();
    }
    va_end(ap);
    utrace_end();
}

void utrace_upump_alloc_leave(struct upump *upump)
{
    utrace_write_id(UTRACE_UPUMP_ALLOC_LEAVE);
    utrace_write_ptr(upump);
    utrace_write_ptr(upump ? upump->cb : NULL);
    utrace_end();
}

void utrace_upump_control_enter(struct upump *upump, int command, va_list args)
{
    va_list ap;

    utrace_write_id(UTRACE_UPUMP_CONTROL_ENTER);
    utrace_write_ptr(upump);
    utrace_write_int(command);
    va_copy(ap, args);
    switch (command) {
        case UPUMP_START: break;
        case UPUMP_STOP: break;
        case UPUMP_FREE: break;
        case UPUMP_GET_STATUS: break;
        case UPUMP_SET_STATUS: w_int(); break;
        case UPUMP_ALLOC_BLOCKER: break;
        case UPUMP_FREE_BLOCKER: w_ptr(struct upump_blocker); break;
        case UPUMP_RESTART: break;

        default:
            assert(command >= UPUMP_CONTROL_LOCAL);
            w_sig();
    }
    va_end(ap);
    utrace_end();
}

void utrace_upump_control_leave(int err, int command, va_list args)
{
    utrace_write_id(UTRACE_UPUMP_CONTROL_LEAVE);
    utrace_write_int(err);
    utrace_write_int(command);
    if (err == UBASE_ERR_NONE) {
        va_list ap;
        va_copy(ap, args);
        switch (command) {
            case UPUMP_GET_STATUS: r_int(); break;
            case UPUMP_ALLOC_BLOCKER: r_ptr(struct upump_blocker); break;
        }
        va_end(ap);
    }
    utrace_end();
}

void utrace_ulog_init(struct ulog *ulog)
{
    char msg[ulog_msg_len(ulog) + 1];
    ulog_msg_print(ulog, msg, sizeof msg);

    utrace_write_id(UTRACE_ULOG_INIT);
    utrace_write_ptr(ulog);
    utrace_write_uint(ulog->level);
    utrace_write_str(msg);
    utrace_end();
}

void utrace_ulog_add_prefix(struct ulog *ulog, struct ulog_pfx *prefix)
{
    utrace_write_id(UTRACE_ULOG_ADD_PREFIX);
    utrace_write_ptr(ulog);
    utrace_write_str(prefix->tag);
    utrace_end();
}

void utrace_dump_graph(const char *name)
{
    utrace_write_id(UTRACE_DUMP_GRAPH);
    utrace_write_str(name);
    utrace_end();
}

#endif
