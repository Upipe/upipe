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

#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/ubuf_sound_mem.h>
#include <upipe/uclock_std.h>
#include <upipe/uref_std.h>

#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_ubuf_mem.h>

#include <upipe/uref.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_void_flow.h>
#include <upipe/uref_dump.h>
#include <upipe/uref_sound.h>

#include <upipe/upipe.h>

#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>

#include <upipe-modules/upipe_audio_copy.h>

#define UDICT_POOL_DEPTH        5
#define UREF_POOL_DEPTH         5
#define UBUF_POOL_DEPTH         5
#define UBUF_SHARED_POOL_DEPTH  1
#define UPROBE_LOG_LEVEL        UPROBE_LOG_VERBOSE
#define CHANNELS                2
#define RATE                    48000
#define LIMIT                   8
#define OUTPUT_SIZE             1024

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

    assert(sink->count == 3 * LIMIT + 1);

    upipe_throw_dead(upipe);

    sink_clean_urefcount(upipe);
    sink_free_void(upipe);
}

static struct upipe *sink_alloc(struct upipe_mgr *mgr,
                                struct uprobe *uprobe,
                                uint32_t signature,
                                va_list args)
{
    struct upipe *upipe = sink_alloc_void(mgr, uprobe, signature, args);
    assert(upipe);

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
    uref_dump(uref, upipe->uprobe);
    assert(uref->ubuf);
    size_t size;
    ubase_assert(uref_sound_size(uref, &size, NULL));
    assert(size == OUTPUT_SIZE);
    static uint64_t last_pts = 0;
    uint64_t pts;
    ubase_assert(uref_clock_get_pts_prog(uref, &pts));
    assert(pts > last_pts);
    last_pts = pts;
    uref_free(uref);
}

static int sink_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    ubase_assert(uref_flow_match_def(flow_def, UREF_SOUND_FLOW_DEF));
    uint64_t samples;
    ubase_assert(uref_sound_flow_get_samples(flow_def, &samples));
    assert(samples == OUTPUT_SIZE);
    return UBASE_ERR_NONE;
}

static int sink_control(struct upipe *upipe,
                        int command, va_list args)
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
    abort();
    return UBASE_ERR_UNHANDLED;
}

static struct upipe_mgr sink_mgr = {
    .refcount = NULL,
    .signature = 0,
    .upipe_alloc = sink_alloc,
    .upipe_input = sink_input,
    .upipe_control = sink_control,
};

int main(int argc, char *argv[])
{
    struct uclock *uclock = uclock_std_alloc(0);

    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr);

    struct udict_mgr *udict_mgr =
        udict_inline_mgr_alloc(UDICT_POOL_DEPTH, umem_mgr, -1, -1);
    assert(udict_mgr);

    struct ubuf_mgr *ubuf_mgr =
        ubuf_sound_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH,
                                 umem_mgr, 4 * 2, 4 * 2);

    struct uref_mgr *uref_mgr =
        uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    assert(uref_mgr);

    struct uprobe *uprobe;

    uprobe = uprobe_stdio_alloc(NULL, stdout, UPROBE_LOG_LEVEL);
    assert(uprobe);
    uprobe = uprobe_ubuf_mem_alloc(uprobe, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_SHARED_POOL_DEPTH);
    assert(uprobe);

    struct upipe_mgr *upipe_audio_copy_mgr = upipe_audio_copy_mgr_alloc();
    assert(upipe_audio_copy_mgr);

    struct uref *flow_def =
        uref_sound_flow_alloc_def(uref_mgr, "s16.", CHANNELS, 2 * CHANNELS);
    ubase_assert(uref_sound_flow_set_samples(flow_def, OUTPUT_SIZE));

    struct upipe *upipe_audio_copy =
        upipe_flow_alloc(upipe_audio_copy_mgr,
                         uprobe_pfx_alloc(uprobe_use(uprobe),
                                          UPROBE_LOG_LEVEL,
                                          "frame"),
                         flow_def);
    uref_free(flow_def);
    upipe_mgr_release(upipe_audio_copy_mgr);
    assert(upipe_audio_copy);

    struct upipe *sink =
        upipe_void_alloc_output(upipe_audio_copy, &sink_mgr,
                                uprobe_pfx_alloc(uprobe_use(uprobe),
                                                 UPROBE_LOG_LEVEL,
                                                 "sink"));
    assert(sink);
    upipe_release(sink);

    flow_def =
        uref_sound_flow_alloc_def(uref_mgr, "s16.", CHANNELS, 2 * CHANNELS);
    assert(flow_def);
    ubase_assert(uref_sound_flow_set_rate(flow_def, RATE));
    ubase_assert(uref_sound_flow_set_planes(flow_def, 2));
    ubase_assert(upipe_set_flow_def(upipe_audio_copy, flow_def));
    uref_free(flow_def);

    uint64_t pts = 1000;
    for (unsigned i = 0; i < LIMIT; i++) {
        size_t size = 3 * OUTPUT_SIZE + (OUTPUT_SIZE / LIMIT) + (i ? 0 : 1);
        struct uref *uref = uref_sound_alloc(uref_mgr, ubuf_mgr,
                                             3 * OUTPUT_SIZE + (OUTPUT_SIZE / LIMIT) + (i ? 0 : 1));
        uref_clock_set_pts_prog(uref, pts);
        pts += 1000 + (UCLOCK_FREQ * size / RATE);
        upipe_input(upipe_audio_copy, uref, NULL);
    }

    upipe_release(upipe_audio_copy);
    uprobe_release(uprobe);
    ubuf_mgr_release(ubuf_mgr);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uclock_release(uclock);
    return 0;
}
