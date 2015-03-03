/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
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
 * @short unit tests for vtrim pipe
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_std.h>
#include <upipe/upipe.h>
#include <upipe-framers/upipe_video_trim.h>
#include <upipe-framers/uref_mpgv.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>

#include <bitstream/mpeg/mp2v.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UBUF_POOL_DEPTH 0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

static bool sync_acquired = false;
static unsigned int nb_packets = 0;

/** definition of our uprobe */
static int catch(struct uprobe *uprobe, struct upipe *upipe,
                 int event, va_list args)
{
    switch (event) {
        default:
            assert(0);
            break;
        case UPROBE_READY:
        case UPROBE_DEAD:
        case UPROBE_NEW_FLOW_DEF:
            break;
        case UPROBE_SYNC_ACQUIRED:
            sync_acquired = true;
            break;
    }
    return UBASE_ERR_NONE;
}

/** helper phony pipe */
static struct upipe *test_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe,
                                uint32_t signature, va_list args)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_init(upipe, mgr, uprobe);
    return upipe;
}

/** helper phony pipe */
static void test_input(struct upipe *upipe, struct uref *uref,
                       struct upump **upump_p)
{
    assert(uref != NULL);
    uref_free(uref);
    nb_packets++;
}

/** helper phony pipe */
static int test_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF:
            return UBASE_ERR_NONE;
        default:
            assert(0);
            return UBASE_ERR_UNHANDLED;
    }
}

/** helper phony pipe */
static void test_free(struct upipe *upipe)
{
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe */
static struct upipe_mgr test_mgr = {
    .refcount = NULL,
    .upipe_alloc = test_alloc,
    .upipe_input = test_input,
    .upipe_control = test_control
};

int main(int argc, char *argv[])
{
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    assert(uref_mgr != NULL);
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_LEVEL);
    assert(uprobe_stdio != NULL);

    struct upipe *upipe_sink = upipe_void_alloc(&test_mgr,
                                                uprobe_use(uprobe_stdio));
    assert(upipe_sink != NULL);

    /* Standard MPEG-2 stream */
    struct uref *uref;
    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_flow_set_def(uref, "block.mpeg2video."));

    struct upipe_mgr *upipe_vtrim_mgr = upipe_vtrim_mgr_alloc();
    assert(upipe_vtrim_mgr != NULL);
    struct upipe *upipe_vtrim = upipe_void_alloc(upipe_vtrim_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "vtrim"));
    assert(upipe_vtrim != NULL);
    ubase_assert(upipe_set_flow_def(upipe_vtrim, uref));
    ubase_assert(upipe_set_output(upipe_vtrim, upipe_sink));
    uref_free(uref);

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_mpgv_set_type(uref, MP2VPIC_TYPE_B));
    upipe_input(upipe_vtrim, uref, NULL);
    assert(nb_packets == 0);
    assert(!sync_acquired);

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_mpgv_set_type(uref, MP2VPIC_TYPE_P));
    upipe_input(upipe_vtrim, uref, NULL);
    assert(nb_packets == 0);
    assert(!sync_acquired);

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_mpgv_set_type(uref, MP2VPIC_TYPE_I));
    ubase_assert(uref_flow_set_random(uref));
    upipe_input(upipe_vtrim, uref, NULL);
    assert(nb_packets == 1);
    assert(sync_acquired);

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_mpgv_set_type(uref, MP2VPIC_TYPE_B));
    upipe_input(upipe_vtrim, uref, NULL);
    assert(nb_packets == 1);

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_mpgv_set_type(uref, MP2VPIC_TYPE_P));
    upipe_input(upipe_vtrim, uref, NULL);
    assert(nb_packets == 2);

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_mpgv_set_type(uref, MP2VPIC_TYPE_B));
    upipe_input(upipe_vtrim, uref, NULL);
    assert(nb_packets == 3);

    upipe_release(upipe_vtrim);

    /* Intra-refresh MPEG-2 stream */
    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_flow_set_def(uref, "block.mpeg2video."));

    upipe_vtrim = upipe_void_alloc(upipe_vtrim_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "vtrim"));
    assert(upipe_vtrim != NULL);
    ubase_assert(upipe_set_flow_def(upipe_vtrim, uref));
    ubase_assert(upipe_set_output(upipe_vtrim, upipe_sink));
    uref_free(uref);
    sync_acquired = false;
    nb_packets = 0;

    for (int i = 0; i < 30; i++) {
        uref = uref_alloc(uref_mgr);
        assert(uref != NULL);
        ubase_assert(uref_mpgv_set_type(uref, MP2VPIC_TYPE_P));
        upipe_input(upipe_vtrim, uref, NULL);
        assert(nb_packets == 0);
        assert(!sync_acquired);
    }

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_mpgv_set_type(uref, MP2VPIC_TYPE_P));
    upipe_input(upipe_vtrim, uref, NULL);
    assert(nb_packets == 31);
    assert(sync_acquired);

    upipe_release(upipe_vtrim);

    /* Closed GOP H264 stream */
    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_flow_set_def(uref, "block.h264."));

    upipe_vtrim_mgr = upipe_vtrim_mgr_alloc();
    assert(upipe_vtrim_mgr != NULL);
    upipe_vtrim = upipe_void_alloc(upipe_vtrim_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "vtrim"));
    assert(upipe_vtrim != NULL);
    ubase_assert(upipe_set_flow_def(upipe_vtrim, uref));
    ubase_assert(upipe_set_output(upipe_vtrim, upipe_sink));
    uref_free(uref);
    sync_acquired = false;
    nb_packets = 0;

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    upipe_input(upipe_vtrim, uref, NULL);
    assert(nb_packets == 0);
    assert(!sync_acquired);

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_flow_set_random(uref));
    upipe_input(upipe_vtrim, uref, NULL);
    assert(nb_packets == 1);
    assert(sync_acquired);

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    upipe_input(upipe_vtrim, uref, NULL);
    assert(nb_packets == 2);

    upipe_release(upipe_vtrim);

    /* Open GOP or intra-refresh H264 stream */
    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_flow_set_def(uref, "block.h264."));

    upipe_vtrim_mgr = upipe_vtrim_mgr_alloc();
    assert(upipe_vtrim_mgr != NULL);
    upipe_vtrim = upipe_void_alloc(upipe_vtrim_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "vtrim"));
    assert(upipe_vtrim != NULL);
    ubase_assert(upipe_set_flow_def(upipe_vtrim, uref));
    ubase_assert(upipe_set_output(upipe_vtrim, upipe_sink));
    uref_free(uref);
    sync_acquired = false;
    nb_packets = 0;

    for (int i = 0; i < 30; i++) {
        uref = uref_alloc(uref_mgr);
        assert(uref != NULL);
        upipe_input(upipe_vtrim, uref, NULL);
        assert(nb_packets == 0);
        assert(!sync_acquired);
    }

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    upipe_input(upipe_vtrim, uref, NULL);
    assert(nb_packets == 31);
    assert(sync_acquired);

    upipe_release(upipe_vtrim);

    upipe_mgr_release(upipe_vtrim_mgr); // nop
    test_free(upipe_sink);

    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(uprobe_stdio);
    uprobe_clean(&uprobe);

    return 0;
}
