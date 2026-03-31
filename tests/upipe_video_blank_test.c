/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
 * Copyright (C) 2026 EasyTools
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

#undef NDEBUG

#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_flow.h"
#include "upipe/upipe_helper_flow_def_check.h"

#include "upipe/uclock.h"
#include "upipe/upipe.h"
#include "upipe/uprobe.h"
#include "upipe/uref.h"
#include "upipe/uref_attr.h"
#include "upipe/uref_std.h"
#include "upipe/uref_void_flow.h"
#include "upipe/uref_pic_flow.h"
#include "upipe/uref_pic.h"
#include "upipe/uref_dump.h"
#include "upipe/umem.h"
#include "upipe/umem_alloc.h"
#include "upipe/udict.h"
#include "upipe/udict_inline.h"
#include "upipe/uclock_std.h"

#include "upipe/uprobe_stdio.h"
#include "upipe/uprobe_prefix.h"
#include "upipe/uprobe_uref_mgr.h"
#include "upipe/uprobe_uclock.h"
#include "upipe/uprobe_ubuf_mem.h"

#include "upipe-modules/upipe_video_blank.h"

#include <assert.h>

#define UPUMP_POOL  1
#define UPUMP_BLOCKER_POOL 1
#define UDICT_POOL_DEPTH 5
#define UREF_POOL_DEPTH 5
#define UBUF_POOL_DEPTH 5
#define UBUF_SHARED_POOL_DEPTH 1
#define UPROBE_LOG_LEVEL UPROBE_LOG_VERBOSE
#define LIMIT 5
#define HSIZE 10
#define VSIZE 10

UREF_ATTR_VOID(test, output, "test.out", output test attribute)
UREF_ATTR_VOID(test, wanted, "test.want", wanted test attibutes)
UREF_ATTR_UNSIGNED(test, count, "test.count", frame count)

struct sink {
    struct upipe upipe;
    struct urefcount urefcount;
    struct urequest *urequest;
    struct uref *flow_def_wanted;
    struct uref *flow_def_input;
    uint64_t count;
    uint64_t hsize;
    uint64_t vsize;
};

UPIPE_HELPER_UPIPE(sink, upipe, 0);
UPIPE_HELPER_UREFCOUNT(sink, urefcount, sink_free);
UPIPE_HELPER_FLOW(sink, UREF_PIC_FLOW_DEF);
UPIPE_HELPER_FLOW_DEF_CHECK(sink, flow_def_wanted);
UPIPE_HELPER_FLOW_DEF_CHECK(sink, flow_def_input);

static void sink_free(struct upipe *upipe)
{
    struct sink *sink = sink_from_upipe(upipe);

    assert(sink->count == LIMIT);

    upipe_throw_dead(upipe);

    assert(!sink->urequest);
    sink_clean_flow_def_wanted(upipe);
    sink_clean_flow_def_input(upipe);
    sink_clean_urefcount(upipe);
    sink_free_flow(upipe);
}

static struct upipe *sink_alloc(struct upipe_mgr *mgr,
                                struct uprobe *uprobe,
                                uint32_t signature, va_list args)
{
    struct uref *flow_def = NULL;
    struct upipe *upipe =
        sink_alloc_flow(mgr, uprobe, signature, args, &flow_def);
    if (unlikely(!upipe))
        return NULL;

    sink_init_urefcount(upipe);
    sink_init_flow_def_input(upipe);
    sink_init_flow_def_wanted(upipe);

    struct sink *sink = sink_from_upipe(upipe);
    sink->count = 0;
    sink->urequest = NULL;
    sink->hsize = 0;
    sink->vsize = 0;

    sink_store_flow_def_wanted(upipe, flow_def);
    uref_pic_flow_get_hsize(flow_def, &sink->hsize);
    uref_pic_flow_get_vsize(flow_def, &sink->vsize);

    upipe_throw_ready(upipe);

    return upipe;
}

static void sink_input(struct upipe *upipe, struct uref *uref,
                       struct upump **upump_p)
{
    struct sink *sink = sink_from_upipe(upipe);
    size_t hsize = 0;
    size_t vsize = 0;
    ubase_assert(uref_pic_size(uref, &hsize, &vsize, NULL));
    if (sink->hsize)
        assert(hsize == sink->hsize);
    else if (sink->count < LIMIT - 1)
        assert(hsize == HSIZE);
    else
        assert(hsize == HSIZE * 2);
    if (sink->vsize)
        assert(vsize == sink->vsize);
    else if (sink->count < LIMIT - 1)
        assert(vsize == VSIZE);
    else
        assert(vsize == VSIZE * 2);

    sink->count++;
    upipe_info_va(upipe, "receive frame %" PRIu64, sink->count);
    assert(sink->count <= LIMIT);
    uref_dump(uref, upipe->uprobe);
    assert(uref->ubuf);
    uref_free(uref);

    switch (sink->count) {
        case 1: {
            upipe_info_va(upipe, "negotiate same");
            struct uref *flow_def = uref_dup(sink->flow_def_input);
            assert(flow_def);
            ubase_assert(
                urequest_provide_flow_format(sink->urequest, flow_def));
            break;
        }
        case 2: {
            upipe_info_va(upipe, "negotiate new attr");
            struct uref *flow_def = uref_dup(sink->flow_def_input);
            assert(flow_def);
            ubase_assert(uref_test_set_count(flow_def, sink->count));
            ubase_assert(
                urequest_provide_flow_format(sink->urequest, flow_def));
            break;
        }
        case LIMIT - 1: {
            upipe_info_va(upipe, "negotiate new size");
            struct uref *flow_def = uref_dup(sink->flow_def_input);
            assert(flow_def);
            ubase_assert(uref_pic_flow_set_hsize(flow_def, HSIZE * 2));
            ubase_assert(uref_pic_flow_set_vsize(flow_def, VSIZE * 2));
            ubase_assert(
                urequest_provide_flow_format(sink->urequest, flow_def));
            break;
        }
    }
}

static int sink_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    assert(!sink_check_flow_def_input(upipe, flow_def));

    struct sink *sink = sink_from_upipe(upipe);
    uint64_t hsize = 0, vsize = 0;
    ubase_assert(uref_flow_match_def(flow_def, "pic."));
    ubase_assert(uref_pic_flow_get_hsize(flow_def, &hsize));
    ubase_assert(uref_pic_flow_get_vsize(flow_def, &vsize));
    ubase_assert(uref_test_get_output(flow_def));
    ubase_assert(uref_test_get_wanted(flow_def));
    if (sink->count > 2)
        ubase_assert(uref_test_get_count(flow_def, NULL));
    if (sink->count < LIMIT - 1)
        assert(hsize == HSIZE && vsize == VSIZE);
    else
        assert(hsize == HSIZE * 2 && vsize == VSIZE * 2);
    flow_def = uref_dup(flow_def);
    assert(flow_def);
    sink_store_flow_def_input(upipe, flow_def);
    return UBASE_ERR_NONE;
}

static int sink_control(struct upipe *upipe, int command, va_list args)
{
    struct sink *sink = sink_from_upipe(upipe);

    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *urequest = va_arg(args, struct urequest *);
            if (urequest->type != UREQUEST_FLOW_FORMAT)
                return upipe_throw_provide_request(upipe, urequest);
            assert(!sink->urequest);
            sink->urequest = urequest_alloc_proxy(urequest);
            assert(sink->urequest);

            struct uref *flow_def = uref_dup(urequest->uref);
            assert(flow_def);
            if (unlikely(!ubase_check(uref_pic_flow_get_hsize(flow_def, NULL))))
                uref_pic_flow_set_hsize(flow_def, HSIZE);
            if (unlikely(!ubase_check(uref_pic_flow_get_vsize(flow_def, NULL))))
                uref_pic_flow_set_vsize(flow_def, VSIZE);
            uref_test_set_output(flow_def);
            return urequest_provide_flow_format(sink->urequest, flow_def);
        }
        case UPIPE_UNREGISTER_REQUEST:
            struct urequest *urequest = va_arg(args, struct urequest *);
            if (urequest->type != UREQUEST_FLOW_FORMAT)
                return UBASE_ERR_NONE;
            assert(sink->urequest);
            struct urequest *upstream =
                urequest_get_opaque(sink->urequest, struct urequest *);
            assert(urequest == upstream);
            urequest_free_proxy(sink->urequest);
            sink->urequest = NULL;
            return UBASE_ERR_NONE;

        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return sink_set_flow_def(upipe, flow_def);
        }
    }
    return UBASE_ERR_UNHANDLED;
}

static struct upipe_mgr sink_mgr = {
    .refcount = NULL,
    .signature = 0,
    .upipe_alloc = sink_alloc,
    .upipe_input = sink_input,
    .upipe_control = sink_control,
};

static int catch(struct uprobe *uprobe, struct upipe *upipe,
                 int event, va_list args)
{
    return UBASE_ERR_NONE;
}

int main(int argc, char *argv[])
{
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr);

    struct udict_mgr *udict_mgr =
        udict_inline_mgr_alloc(UDICT_POOL_DEPTH, umem_mgr, -1, -1);
    assert(udict_mgr);

    struct uref_mgr *uref_mgr =
        uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    assert(uref_mgr);

    struct uclock *uclock = uclock_std_alloc(0);
    assert(uclock);

    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger =
        uprobe_stdio_alloc(&uprobe, stdout, UPROBE_LOG_LEVEL);
    assert(logger);
    logger = uprobe_uref_mgr_alloc(logger, uref_mgr);
    assert(logger);
    logger = uprobe_uclock_alloc(logger, uclock);
    assert(logger);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_SHARED_POOL_DEPTH);
    assert(logger);

    struct upipe_mgr *upipe_vblk_mgr = upipe_vblk_mgr_alloc();
    assert(upipe_vblk_mgr);

    {
        uprobe_notice_va(logger, NULL, "starting test 1");

        struct uref *flow_def = uref_pic_flow_alloc_def(uref_mgr, 1);
        ubase_assert(uref_pic_flow_set_hsize(flow_def, HSIZE));
        ubase_assert(uref_pic_flow_set_vsize(flow_def, VSIZE));
        ubase_assert(uref_test_set_wanted(flow_def));
        assert(flow_def);

        struct upipe *source = upipe_flow_alloc(
            upipe_vblk_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "vblk"),
            flow_def);
        assert(source);

        struct upipe *sink = upipe_flow_alloc_output(
            source, &sink_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "sink"),
            flow_def);
        assert(sink);
        uref_free(flow_def);

        struct uref *input_flow_def = uref_void_flow_alloc_def(uref_mgr);
        assert(input_flow_def);
        ubase_assert(upipe_set_flow_def(source, input_flow_def));
        uref_free(input_flow_def);

        for (unsigned i = 0; i < LIMIT; i++) {
            struct uref *uref = uref_alloc_control(uref_mgr);
            upipe_input(source, uref, NULL);
        }
        upipe_release(source);
        upipe_release(sink);
    }

    {
        uprobe_notice_va(logger, NULL, "starting test 2");

        struct uref *flow_def = uref_pic_flow_alloc_def(uref_mgr, 1);
        ubase_assert(uref_test_set_wanted(flow_def));
        assert(flow_def);

        struct upipe *source = upipe_flow_alloc(
            upipe_vblk_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "vblk"),
            flow_def);
        assert(source);

        struct upipe *sink = upipe_flow_alloc_output(
            source, &sink_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "sink"),
            flow_def);
        assert(sink);
        uref_free(flow_def);

        struct uref *input_flow_def = uref_void_flow_alloc_def(uref_mgr);
        assert(input_flow_def);
        ubase_assert(upipe_set_flow_def(source, input_flow_def));
        uref_free(input_flow_def);

        for (unsigned i = 0; i < LIMIT; i++) {
            struct uref *uref = uref_alloc_control(uref_mgr);
            upipe_input(source, uref, NULL);
        }
        upipe_release(source);
        upipe_release(sink);
    }

    uprobe_release(logger);
    upipe_mgr_release(upipe_vblk_mgr);
    uclock_release(uclock);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);

    return 0;
}
