/*
 * Copyright (C) 2020 EasyTools
 *
 * Authors: Arnaud de Turckheim
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#undef NDEBUG

#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_uri.h>
#include <upipe/uref_dump.h>
#include <upipe/uprobe_stdio.h>

#include <stdio.h>
#include <string.h>
#include <assert.h>

#define UDICT_POOL_DEPTH 1
#define UREF_POOL_DEPTH 1

static int catch(struct uprobe *uprobe,
                 struct upipe *upipe,
                 int event, va_list args)
{
    if (event != UPROBE_LOG)
        return uprobe_throw_next(uprobe, upipe, event, args);

    struct ulog *ulog = va_arg(args, struct ulog *);
    if (strstr(ulog->format, "%p"))
        return UBASE_ERR_NONE;

    return uprobe_throw(uprobe->next, upipe, UPROBE_LOG, ulog);
}

int main(int argc, char **argv)
{
    size_t size = 64;
    uint8_t ptr[size];
    struct udict_opaque opaque = { ptr, size };
    struct urational rational = { 25, 2 };
    struct value {
        const char *key;
        enum udict_type type;
        union {
            struct udict_opaque t_opaque;
            const char *t_string;
            void *t_void;
            bool t_bool;
            uint8_t t_small_unsigned;
            int8_t t_small_int;
            uint64_t t_unsigned;
            int64_t t_int;
            double t_float;
            struct urational t_rational;
        } value;
    } values[] = {
#define ENTRY(Key, Value, Type, Typename)                                   \
        {                                                                   \
            .key = Key,                                                     \
            .type = UDICT_TYPE_##Type,                                      \
            .value = {                                                      \
                .t_##Typename = Value,                                      \
            },                                                              \
        }
#define OPAQUE(Key, Value)          ENTRY(Key, Value, OPAQUE, opaque)
#define STRING(Key, Value)          ENTRY(Key, Value, STRING, string)
#define VOID(Key, Value)            ENTRY(Key, Value, VOID, void)
#define BOOL(Key, Value)            ENTRY(Key, Value, BOOL, bool)
#define SMALL_UNSIGNED(Key, Value)  ENTRY(Key, Value, SMALL_UNSIGNED, small_unsigned)
#define SMALL_INT(Key, Value)       ENTRY(Key, Value, SMALL_INT, small_int)
#define UNSIGNED(Key, Value)        ENTRY(Key, Value, UNSIGNED, unsigned)
#define INT(Key, Value)             ENTRY(Key, Value, INT, int)
#define FLOAT(Key, Value)           ENTRY(Key, Value, FLOAT, float)
#define RATIONAL(Key, Value)        ENTRY(Key, Value, RATIONAL, rational)
        OPAQUE("opaque", opaque),
        STRING("string", "this is a test string"),
        VOID("void", NULL),
        SMALL_UNSIGNED("small_unsigned", 42),
        SMALL_INT("small_int", -42),
        UNSIGNED("unsigned", UINT64_MAX),
        INT("int", INT64_MIN + 1),
        FLOAT("float", -42.42),
        RATIONAL("rational", rational),
        OPAQUE("opaque2", opaque),
        STRING("string2", "this is a test string"),
        VOID("void2", NULL),
        SMALL_UNSIGNED("small_unsigned2", 42),
        SMALL_INT("small_int2", -42),
        UNSIGNED("unsigned2", UINT64_MAX),
        INT("int2", INT64_MIN + 1),
        FLOAT("float2", -42.42),
        RATIONAL("rational2", rational),
#undef OPAQUE
#undef STRING
#undef VOID
#undef BOOL
#undef SMALL_UNSIGNED
#undef SMALL_INT
#undef UNSIGNED
#undef INT
#undef FLOAT
#undef RATIONAL
#undef ENTRY
    };

    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    assert(mgr != NULL);

    struct uprobe *uprobe = uprobe_stdio_alloc(NULL, stdout, UPROBE_LOG_VERBOSE);
    assert(uprobe);
    uprobe = uprobe_alloc(catch, uprobe);

    struct uref *uref = uref_alloc_control(mgr);
    for (unsigned i = 0; i < UBASE_ARRAY_SIZE(values); i++) {
        const struct value *v = &values[i];
        switch (v->type) {
            case UDICT_TYPE_OPAQUE:
                uref_attr_set_opaque(uref, v->value.t_opaque, v->type, v->key);
                break;
            case UDICT_TYPE_STRING:
                uref_attr_set_string(uref, v->value.t_string, v->type, v->key);
                break;
            case UDICT_TYPE_VOID:
                uref_attr_set_void(uref, v->value.t_void, v->type, v->key);
                break;
            case UDICT_TYPE_SMALL_UNSIGNED:
                uref_attr_set_small_unsigned(uref, v->value.t_small_unsigned,
                                             v->type, v->key);
                break;
            case UDICT_TYPE_SMALL_INT:
                uref_attr_set_small_int(uref, v->value.t_small_int,
                                        v->type, v->key);
                break;
            case UDICT_TYPE_UNSIGNED:
                uref_attr_set_unsigned(uref, v->value.t_unsigned,
                                       v->type, v->key);
                break;
            case UDICT_TYPE_INT:
                uref_attr_set_int(uref, v->value.t_int, v->type, v->key);
                break;
            case UDICT_TYPE_FLOAT:
                uref_attr_set_float(uref, v->value.t_float, v->type, v->key);
                break;
            case UDICT_TYPE_RATIONAL:
                uref_attr_set_rational(uref, v->value.t_rational,
                                       v->type, v->key);
                break;
            default:
                abort();
        }
    }

    udict_dump(uref->udict, uprobe);
    udict_dump_verbose(uref->udict, uprobe);
    udict_dump_dbg(uref->udict, uprobe);
    udict_dump_info(uref->udict, uprobe);
    udict_dump_notice(uref->udict, uprobe);
    udict_dump_warn(uref->udict, uprobe);
    udict_dump_err(uref->udict, uprobe);

    uref_dump(uref, uprobe);
    uref_dump_verbose(uref, uprobe);
    uref_dump_dbg(uref, uprobe);
    uref_dump_info(uref, uprobe);
    uref_dump_notice(uref, uprobe);
    uref_dump_warn(uref, uprobe);
    uref_dump_err(uref, uprobe);

    uref_dump_clock(uref, uprobe);
    uref_dump_clock_verbose(uref, uprobe);
    uref_dump_clock_dbg(uref, uprobe);
    uref_dump_clock_info(uref, uprobe);
    uref_dump_clock_notice(uref, uprobe);
    uref_dump_clock_warn(uref, uprobe);
    uref_dump_clock_err(uref, uprobe);

    uint64_t sys = 0;
    uint64_t prog = 0;
    uint64_t orig = 0;
    for (unsigned i = 0; i < 100; i++) {
        if (sys) {
            uref_clock_set_date_sys(uref, sys, UREF_DATE_PTS);
            uref_clock_set_dts_pts_delay(uref, UCLOCK_FREQ);
        }
        if (prog) {
            uref_clock_set_date_prog(uref, prog, UREF_DATE_DTS);
            uref_clock_set_cr_dts_delay(uref, UCLOCK_FREQ);
        }
        if (orig)
            uref_clock_set_date_orig(uref, orig, UREF_DATE_CR);

        orig = prog;
        prog = sys;
        sys += UCLOCK_HOUR + 2 * UCLOCK_MINUTE + 3 * UCLOCK_SECOND +
            4 * UCLOCK_MILLISECOND + 1111;

        uref_dump_clock(uref, uprobe);
        uref_dump_clock_verbose(uref, uprobe);
        uref_dump_clock_dbg(uref, uprobe);
        uref_dump_clock_info(uref, uprobe);
        uref_dump_clock_notice(uref, uprobe);
        uref_dump_clock_warn(uref, uprobe);
        uref_dump_clock_err(uref, uprobe);
    }

    uref_free(uref);

    uprobe_release(uprobe);
    uref_mgr_release(mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    return 0;
}
