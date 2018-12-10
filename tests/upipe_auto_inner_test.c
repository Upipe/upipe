/*
 * Copyright (C) 2018 OpenHeadend S.A.R.L.
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

#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_flow.h>

#include <upipe/upipe.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uclock_std.h>

#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_uref_mgr.h>

#include <upipe-modules/upipe_idem.h>
#include <upipe-modules/upipe_auto_inner.h>

#include <assert.h>

#define UPUMP_POOL  1
#define UPUMP_BLOCKER_POOL 1
#define UDICT_POOL_DEPTH 5
#define UREF_POOL_DEPTH 5
#define UBUF_POOL_DEPTH 5
#define UPROBE_LOG_LEVEL UPROBE_LOG_VERBOSE
#define COUNT 5

static uint64_t sink1_count = 0;
static uint64_t sink2_count = 0;

struct sink1 {
    struct upipe upipe;
    struct urefcount urefcount;
};

UPIPE_HELPER_UPIPE(sink1, upipe, 0);
UPIPE_HELPER_UREFCOUNT(sink1, urefcount, sink1_free);
UPIPE_HELPER_VOID(sink1);

struct sink2 {
    struct upipe upipe;
    struct urefcount urefcount;
};

UPIPE_HELPER_UPIPE(sink2, upipe, 0);
UPIPE_HELPER_UREFCOUNT(sink2, urefcount, sink2_free);
UPIPE_HELPER_FLOW(sink2, "void.");

static struct upipe *sink1_alloc(struct upipe_mgr *mgr,
                                 struct uprobe *uprobe,
                                 uint32_t signature, va_list args)
{
    struct upipe *upipe = sink1_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(!upipe))
        return NULL;

    sink1_init_urefcount(upipe);

    upipe_throw_ready(upipe);
    return upipe;
}

static void sink1_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    sink1_clean_urefcount(upipe);
    sink1_free_void(upipe);
}

static void sink1_input(struct upipe *upipe, struct uref *uref,
                        struct upump **upump_p)
{
    sink1_count++;
    uref_free(uref);
}

static int sink1_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    return uref_flow_match_def(flow_def, "type1.");
}

static int sink1_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *urequest = va_arg(args, struct urequest *);
            return upipe_throw_provide_request(upipe, urequest);
        }
        case UPIPE_UNREGISTER_REQUEST:
            return UBASE_ERR_NONE;

        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return sink1_set_flow_def(upipe, flow_def);
        }
    }
    return UBASE_ERR_UNHANDLED;
}

static struct upipe_mgr sink1_mgr = {
    .refcount = NULL,
    .signature = 0,
    .upipe_alloc = sink1_alloc,
    .upipe_input = sink1_input,
    .upipe_control = sink1_control,
};

static struct upipe *sink2_alloc(struct upipe_mgr *mgr,
                                 struct uprobe *uprobe,
                                 uint32_t signature, va_list args)
{
    struct uref *flow_def = NULL;
    struct upipe *upipe = sink2_alloc_flow(mgr, uprobe, signature, args,
                                           &flow_def);
    if (unlikely(!upipe))
        return NULL;

    sink2_init_urefcount(upipe);

    upipe_throw_ready(upipe);
    uref_free(flow_def);
    return upipe;
}

static void sink2_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    sink2_clean_urefcount(upipe);
    sink2_free_flow(upipe);
}

static void sink2_input(struct upipe *upipe, struct uref *uref,
                        struct upump **upump_p)
{
    sink2_count++;
    uref_free(uref);
}

static int sink2_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    return uref_flow_match_def(flow_def, "type2.");
}

static int sink2_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *urequest = va_arg(args, struct urequest *);
            return upipe_throw_provide_request(upipe, urequest);
        }
        case UPIPE_UNREGISTER_REQUEST:
            return UBASE_ERR_NONE;

        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return sink2_set_flow_def(upipe, flow_def);
        }
    }
    return UBASE_ERR_UNHANDLED;
}

static struct upipe_mgr sink2_mgr = {
    .refcount = NULL,
    .signature = 0,
    .upipe_alloc = sink2_alloc,
    .upipe_input = sink2_input,
    .upipe_control = sink2_control,
};

static int catch(struct uprobe *uprobe, struct upipe *upipe,
                 int event, va_list args)
{
    return UBASE_ERR_NONE;
}

int main(int argc, char *argv[])
{
    int ret;

    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr);

    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr);

    struct uref_mgr *uref_mgr =
        uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    assert(uref_mgr);

    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger =
        uprobe_stdio_alloc(&uprobe, stdout, UPROBE_LOG_LEVEL);
    assert(logger);
    logger = uprobe_uref_mgr_alloc(logger, uref_mgr);
    assert(logger);

    struct upipe_mgr *upipe_autoin_mgr = upipe_autoin_mgr_alloc();
    assert(upipe_autoin_mgr);
    ubase_assert(upipe_autoin_mgr_add_mgr(
            upipe_autoin_mgr, "sink1", &sink1_mgr));
    ubase_assert(upipe_autoin_mgr_add_mgr(
            upipe_autoin_mgr, "sink2", &sink2_mgr));
    struct uref *flow_def = uref_alloc_control(uref_mgr);
    assert(flow_def);
    ret = uref_flow_set_def(flow_def, "void.");
    ubase_assert(ret);
    struct upipe *upipe = upipe_flow_alloc(
        upipe_autoin_mgr,
        uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_VERBOSE, "auto"),
        flow_def);
    assert(upipe);
    uref_free(flow_def);

    flow_def = uref_alloc_control(uref_mgr);
    assert(flow_def);
    ret = uref_flow_set_def(flow_def, "invalid.");
    ubase_assert(ret);
    ret = upipe_set_flow_def(upipe, flow_def);
    uref_free(flow_def);
    assert(!ubase_check(ret));
    upipe_release(upipe);

    ubase_assert(upipe_autoin_mgr_add_mgr(
            upipe_autoin_mgr, "idem", upipe_idem_mgr_alloc()));
    flow_def = uref_alloc_control(uref_mgr);
    assert(flow_def);
    ret = uref_flow_set_def(flow_def, "void.");
    ubase_assert(ret);
    upipe = upipe_flow_alloc(
        upipe_autoin_mgr,
        uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_VERBOSE, "auto"),
        flow_def);
    assert(upipe);
    uref_free(flow_def);
    upipe_mgr_release(upipe_autoin_mgr);

    flow_def = uref_alloc_control(uref_mgr);
    assert(flow_def);
    ret = uref_flow_set_def(flow_def, "invalid.");
    ubase_assert(ret);
    ret = upipe_set_flow_def(upipe, flow_def);
    ubase_assert(ret);
    uref_free(flow_def);

    for (unsigned i = 0; i < COUNT; i++) {
        struct uref *uref = uref_alloc_control(uref_mgr);
        assert(uref);
        upipe_input(upipe, uref, NULL);
    }

    flow_def = uref_alloc_control(uref_mgr);
    assert(flow_def);
    ret = uref_flow_set_def(flow_def, "type1.");
    ubase_assert(ret);
    ret = upipe_set_flow_def(upipe, flow_def);
    ubase_assert(ret);
    uref_free(flow_def);

    for (unsigned i = 0; i < COUNT; i++) {
        struct uref *uref = uref_alloc_control(uref_mgr);
        assert(uref);
        upipe_input(upipe, uref, NULL);
    }

    assert(sink1_count == COUNT);

    flow_def = uref_alloc_control(uref_mgr);
    assert(flow_def);
    ret = uref_flow_set_def(flow_def, "type2.");
    ubase_assert(ret);
    ret = upipe_set_flow_def(upipe, flow_def);
    ubase_assert(ret);
    uref_free(flow_def);

    for (unsigned i = 0; i < COUNT; i++) {
        struct uref *uref = uref_alloc_control(uref_mgr);
        assert(uref);
        upipe_input(upipe, uref, NULL);
    }

    assert(sink2_count == COUNT);

    upipe_release(upipe);
    uprobe_release(logger);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);

    return 0;
}
