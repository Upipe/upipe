/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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
#include <upipe/upipe.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/uref_std.h>
#include <upipe/uref_block.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_dump.h>
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
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG
#define FRAMES_LIMIT        100

/** definition of our uprobe */
bool catch(struct uprobe *uprobe, struct upipe *upipe, enum uprobe_event event,
           va_list args)
{
    switch (event) {
        case UPROBE_READY:
        case UPROBE_DEAD:
        case UPROBE_NEW_FLOW_DEF:
            break;
        case UPROBE_SINK_END:
        case UPROBE_NEED_UREF_MGR:
        case UPROBE_NEED_UPUMP_MGR:
        case UPROBE_NEED_UBUF_MGR:
        default:
            assert(event & UPROBE_HANDLED_FLAG);
            break;
    }
    return true;
}

struct uref_mgr *uref_mgr;
struct ubuf_mgr *block_mgr;
struct uprobe *logger;

int main(int argc, char **argv)
{
    /* uref and mem management */
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH, umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0); 
    assert(uref_mgr != NULL);

    /* block */
    block_mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH,
            UBUF_POOL_DEPTH, umem_mgr,
            UBUF_ALIGN,
            UBUF_ALIGN_OFFSET);
    assert(block_mgr);


    /* uprobe stuff */
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    logger = uprobe_stdio_alloc(&uprobe, stdout, UPROBE_LOG_LEVEL);
    assert(logger != NULL);

    /* managers */
    struct upipe_mgr *upipe_null_mgr = upipe_null_mgr_alloc();
    struct upipe_mgr *upipe_swr_mgr = upipe_swr_mgr_alloc();
    assert(upipe_swr_mgr);

    /* alloc swr pipe */
    struct uref *flow = uref_sound_flow_alloc_def(uref_mgr, "pcm_s16le.", 2, 2);
    assert(flow != NULL);
    assert(uref_sound_flow_set_rate(flow, 48000));
    assert(uref_sound_flow_set_channels(flow, 2));
    struct uref *flow_output = uref_sound_flow_alloc_def(uref_mgr, "pcm_f32.", 2, 2);
    assert(flow_output != NULL);
    assert(uref_sound_flow_set_rate(flow_output, 48000));
    assert(uref_sound_flow_set_channels(flow_output, 2));
    struct upipe *swr = upipe_flow_alloc(upipe_swr_mgr,
        uprobe_pfx_adhoc_alloc(logger, UPROBE_LOG_LEVEL, "swr"), flow_output);
    assert(swr);
    assert(upipe_set_flow_def(swr, flow));
    assert(upipe_set_ubuf_mgr(swr, block_mgr));
    uref_free(flow);
    uref_free(flow_output);

    /* /dev/null */
    struct upipe *null = upipe_void_alloc(upipe_null_mgr,
        uprobe_pfx_adhoc_alloc(logger, UPROBE_LOG_LEVEL, "null"));
    assert(null);
    upipe_null_dump_dict(null, true);
    assert(upipe_set_output(swr, null));
    upipe_release(null);

    struct uref *sound;
    int i;

    for (i=0; i < FRAMES_LIMIT; i++) {
        uint8_t *buf = NULL;
        int size = -1;
        int samples = (1024+i-FRAMES_LIMIT/2);
        sound = uref_block_alloc(uref_mgr, block_mgr, 2*2*samples);
        uref_sound_flow_set_samples(sound, samples);
        uref_block_write(sound, 0, &size, &buf);
        memset(buf, 0, size);
        uref_block_unmap(sound, 0);
        upipe_input(swr, sound, NULL);
    }

    upipe_release(swr);
    printf("Everything good so far, cleaning\n");

    /* clean managers and probes */
    upipe_mgr_release(upipe_swr_mgr);
    upipe_mgr_release(upipe_null_mgr);
    ubuf_mgr_release(block_mgr);
    uref_mgr_release(uref_mgr);
    umem_mgr_release(umem_mgr);
    udict_mgr_release(udict_mgr);
    uprobe_stdio_free(logger);
}
