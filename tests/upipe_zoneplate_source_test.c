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

/** @file
 * @short unit tests for zoneplate source
 */

#undef NDEBUG

#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_urefcount.h>

#include <upipe/uclock_std.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/uprobe_upump_mgr.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_uclock.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_dump.h>
#include <upipe/upipe.h>
#include <upipe/uref_pic_flow.h>

#include <upipe-filters/upipe_zoneplate_source.h>

#include <upump-ev/upump_ev.h>

#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

struct sink {
    struct upipe upipe;
    struct urefcount urefcount;
};

UPIPE_HELPER_UPIPE(sink, upipe, 0);
UPIPE_HELPER_VOID(sink);
UPIPE_HELPER_UREFCOUNT(sink, urefcount, sink_free);

static struct upipe *upipe_zpsrc = NULL;
static int counter = 0;

/** helper phony pipe */
static struct upipe *sink_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe,
                                uint32_t signature, va_list args)
{
    struct upipe *upipe = sink_alloc_void(mgr, uprobe, signature, args);
    assert(upipe);

    sink_init_urefcount(upipe);

    upipe_throw_ready(upipe);

    return upipe;
}

/** helper phony pipe */
static void sink_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);
    sink_clean_urefcount(upipe);
    sink_free_void(upipe);
}

/** helper phony pipe */
static void sink_input(struct upipe *upipe, struct uref *uref,
                       struct upump **upump_p)
{
    upipe_dbg_va(upipe, "receive frame %u", counter);
    assert(uref != NULL);
    assert(uref->ubuf != NULL);
    counter++;
    uref_free(uref);

    if (counter >= 5) {
        upipe_release(upipe_zpsrc);
        upipe_zpsrc = NULL;
    }
}

/** helper phony pipe */
static int sink_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST:
        case UPIPE_UNREGISTER_REQUEST:
            return upipe_control_provide_request(upipe, command, args);
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            uref_dump(flow_def, upipe->uprobe);
            ubase_assert(uref_flow_match_def(flow_def, UREF_PIC_FLOW_DEF));
            ubase_assert(uref_pic_flow_get_fps(flow_def, NULL));
            return UBASE_ERR_NONE;
        }
        default:
            upipe_err_va(upipe, "unexpected command %s",
                         upipe_command_str(upipe, command));
            assert(0);
            return UBASE_ERR_UNHANDLED;
    }
}

/** helper phony pipe */
static struct upipe_mgr sink_mgr = {
    .refcount = NULL,
    .upipe_alloc = sink_alloc,
    .upipe_input = sink_input,
    .upipe_control = sink_control
};

int main(int argc, char *argv[])
{
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc_default(0, 0);
    assert(upump_mgr != NULL);
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    assert(uref_mgr != NULL);
    struct uclock *uclock = uclock_std_alloc(0);
    assert(uclock);

    struct uprobe *logger = uprobe_stdio_alloc(NULL, stdout,
                                               UPROBE_LOG_LEVEL);
    assert(logger);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, 1, 1);
    assert(logger);
    logger = uprobe_upump_mgr_alloc(logger, upump_mgr);
    assert(logger);
    logger = uprobe_uref_mgr_alloc(logger, uref_mgr);
    assert(logger);
    logger = uprobe_uclock_alloc(logger, uclock);
    assert(logger);

    struct upipe *sink =
        upipe_void_alloc(&sink_mgr,
                         uprobe_pfx_alloc(
                             uprobe_use(logger),
                             UPROBE_LOG_VERBOSE, "sink"));
    assert(sink != NULL);

    struct upipe_mgr *upipe_zpsrc_mgr = upipe_zpsrc_mgr_alloc();
    struct uref *flow_def = uref_pic_flow_alloc_def(uref_mgr, 1);
    assert(flow_def != NULL);
    ubase_assert(uref_pic_flow_add_plane(flow_def, 1, 1, 1, "y8"));
    struct urational fps;
    fps.num = 25;
    fps.den = 1;
    ubase_assert(uref_pic_flow_set_fps(flow_def, fps));
    ubase_assert(uref_pic_flow_set_hsize(flow_def, 1920));
    ubase_assert(uref_pic_flow_set_vsize(flow_def, 1080));
    upipe_zpsrc = upipe_flow_alloc(
        upipe_zpsrc_mgr,
        uprobe_pfx_alloc(uprobe_use(logger),
                         UPROBE_LOG_VERBOSE, "zpsrc"),
        flow_def);
    assert(upipe_zpsrc);
    uref_free(flow_def);
    upipe_mgr_release(upipe_zpsrc_mgr);
    uprobe_release(logger);

    ubase_assert(upipe_set_output(upipe_zpsrc, sink));
    upipe_release(sink);

    upump_mgr_run(upump_mgr, NULL);

    assert(!upipe_zpsrc);

    uclock_release(uclock);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    upump_mgr_release(upump_mgr);

    return 0;
}
