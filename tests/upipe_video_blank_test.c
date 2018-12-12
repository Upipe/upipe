/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
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

#include <upipe/uclock.h>
#include <upipe/upipe.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_void_flow.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_dump.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uclock_std.h>

#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_uclock.h>
#include <upipe/uprobe_ubuf_mem.h>

#include <upipe-modules/upipe_video_blank.h>

#include <assert.h>

#define UPUMP_POOL  1
#define UPUMP_BLOCKER_POOL 1
#define UDICT_POOL_DEPTH 5
#define UREF_POOL_DEPTH 5
#define UBUF_POOL_DEPTH 5
#define UBUF_SHARED_POOL_DEPTH 1
#define UPROBE_LOG_LEVEL UPROBE_LOG_VERBOSE
#define LIMIT 5

struct sink {
    struct upipe upipe;
    struct urefcount urefcount;
    uint64_t count;
};

UPIPE_HELPER_UPIPE(sink, upipe, 0);
UPIPE_HELPER_UREFCOUNT(sink, urefcount, sink_free);
UPIPE_HELPER_VOID(sink);

static void sink_free(struct upipe *upipe)
{
    struct sink *sink = sink_from_upipe(upipe);

    assert(sink->count == LIMIT);

    upipe_throw_dead(upipe);

    sink_clean_urefcount(upipe);
    sink_free_void(upipe);
}

static struct upipe *sink_alloc(struct upipe_mgr *mgr,
                                struct uprobe *uprobe,
                                uint32_t signature, va_list args)
{
    struct upipe *upipe = sink_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(!upipe))
        return NULL;

    sink_init_urefcount(upipe);

    struct sink *sink = sink_from_upipe(upipe);
    sink->count = 0;

    upipe_throw_ready(upipe);

    return upipe;
}

static void sink_input(struct upipe *upipe, struct uref *uref,
                       struct upump **upump_p)
{
    struct sink *sink = sink_from_upipe(upipe);

    sink->count++;
    assert(sink->count <= LIMIT);
    uref_dump(uref, upipe->uprobe);
    assert(uref->ubuf);
    uref_free(uref);
}

static int sink_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    uint64_t hsize = 0, vsize = 0;
    ubase_assert(uref_flow_match_def(flow_def, "pic."));
    ubase_assert(uref_pic_flow_get_hsize(flow_def, &hsize));
    ubase_assert(uref_pic_flow_get_vsize(flow_def, &vsize));
    return UBASE_ERR_NONE;
}

static int sink_control(struct upipe *upipe, int command, va_list args)
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

    struct uref *flow_def = uref_pic_flow_alloc_def(uref_mgr, 1);
    ubase_assert(uref_pic_flow_set_hsize(flow_def, 10));
    ubase_assert(uref_pic_flow_set_vsize(flow_def, 10));
    assert(flow_def);

    struct upipe *source = upipe_flow_alloc(upipe_vblk_mgr,
                              uprobe_pfx_alloc(uprobe_use(logger),
                                               UPROBE_LOG_LEVEL,
                                               "vblk"),
                              flow_def);
    uref_free(flow_def);
    assert(source);

    struct upipe *sink =
        upipe_void_alloc_output(source, &sink_mgr,
                                uprobe_pfx_alloc(uprobe_use(logger),
                                                 UPROBE_LOG_LEVEL, "sink"));
    assert(sink);

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
    uprobe_release(logger);
    upipe_mgr_release(upipe_vblk_mgr);
    uclock_release(uclock);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);

    return 0;
}
