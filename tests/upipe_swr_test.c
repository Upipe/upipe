/*
 * Copyright (C) 2013-2014 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen <bencoh@notk.org>
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
 * @short unit tests for upipe_swr pipe
 */

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/upipe.h>
#include <upipe/uclock.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_sound.h>
#include <upipe/ubuf_sound_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/uref_std.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_dump.h>
#include <upipe/uref_clock.h>
#include <upipe-swresample/upipe_swr.h>
#include <upipe-modules/upipe_null.h>

#undef NDEBUG

#define UDICT_POOL_DEPTH    0
#define UREF_POOL_DEPTH     0
#define UBUF_POOL_DEPTH     0
#define UBUF_PREPEND        0
#define UBUF_APPEND         0
#define UBUF_ALIGN          32
#define UBUF_ALIGN_OFFSET   0
#define UPROBE_LOG_LEVEL UPROBE_LOG_VERBOSE
#define FRAMES_LIMIT        100
#define INPUT_RATE          48000
#define OUTPUT_RATE         44100

/** definition of our uprobe */
static int catch(struct uprobe *uprobe, struct upipe *upipe,
                 int event, va_list args)
{
    switch (event) {
        case UPROBE_READY:
        case UPROBE_DEAD:
        case UPROBE_NEW_FLOW_DEF:
            break;
        default:
            assert(0);
            break;
    }
    return UBASE_ERR_NONE;
}

static struct uref_mgr *uref_mgr;
static struct ubuf_mgr *sound_mgr;
static struct uprobe *logger;

int main(int argc, char **argv)
{
    /* uref and mem management */
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH, umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    assert(uref_mgr != NULL);

    /* sound */
    sound_mgr = ubuf_sound_mem_mgr_alloc(UBUF_POOL_DEPTH,
            UBUF_POOL_DEPTH, umem_mgr, 4, 32);
    assert(sound_mgr);
    ubase_assert(ubuf_sound_mem_mgr_add_plane(sound_mgr, "lr"));

    /* uprobe stuff */
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    logger = uprobe_stdio_alloc(&uprobe, stdout, UPROBE_LOG_LEVEL);
    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_POOL_DEPTH);
    assert(logger != NULL);

    /* managers */
    struct upipe_mgr *upipe_null_mgr = upipe_null_mgr_alloc();
    struct upipe_mgr *upipe_swr_mgr = upipe_swr_mgr_alloc();
    assert(upipe_swr_mgr);

    /* alloc swr pipe */
    struct uref *flow = uref_sound_flow_alloc_def(uref_mgr, "s16.", 2, 4);
    assert(flow != NULL);
    ubase_assert(uref_sound_flow_add_plane(flow, "lr"));
    ubase_assert(uref_sound_flow_set_rate(flow, INPUT_RATE));
    struct uref *flow_output = uref_sound_flow_alloc_def(uref_mgr, "f32.", 2, 8);
    assert(flow_output != NULL);
    ubase_assert(uref_sound_flow_add_plane(flow_output, "lr"));
    ubase_assert(uref_sound_flow_set_rate(flow_output, OUTPUT_RATE));
    /*ubase_assert(uref_sound_flow_set_channels(flow_output, 2));*/
    struct upipe *swr = upipe_flow_alloc(upipe_swr_mgr,
        uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "swr"),
        flow_output);
    assert(swr);
    ubase_assert(upipe_set_flow_def(swr, flow));
    uref_free(flow);
    uref_free(flow_output);

    /* /dev/null */
    struct upipe *null = upipe_void_alloc(upipe_null_mgr,
        uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "null"));
    assert(null);
    upipe_null_dump_dict(null, true);
    ubase_assert(upipe_set_output(swr, null));
    upipe_release(null);

    struct uref *sound;
    int i;

    uint64_t next_pts = UCLOCK_FREQ;
    for (i=0; i < FRAMES_LIMIT; i++) {
        uint8_t *buf = NULL;
        int samples = (1024+i-FRAMES_LIMIT/2);
        //int samples = 1024;
        sound = uref_sound_alloc(uref_mgr, sound_mgr, samples);
        assert(sound != NULL);
        ubase_assert(uref_sound_plane_write_uint8_t(sound, "lr", 0, -1, &buf));
        memset(buf, 0, 2*2*samples);
        ubase_assert(uref_sound_plane_unmap(sound, "lr", 0, -1));

        uref_clock_set_pts_sys(sound, next_pts);
        next_pts += samples * UCLOCK_FREQ / INPUT_RATE;
        upipe_input(swr, sound, NULL);
    }

    upipe_release(swr);
    printf("Everything good so far, cleaning\n");

    /* clean managers and probes */
    upipe_mgr_release(upipe_swr_mgr);
    upipe_mgr_release(upipe_null_mgr);
    ubuf_mgr_release(sound_mgr);
    uref_mgr_release(uref_mgr);
    umem_mgr_release(umem_mgr);
    udict_mgr_release(udict_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe);
    return 0;
}
