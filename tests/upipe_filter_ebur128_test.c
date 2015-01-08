/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
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

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/uclock.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/upipe.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/ubuf_sound_mem.h>
#include <upipe-filters/upipe_filter_ebur128.h>
#include <upipe-modules/upipe_null.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>

#define UDICT_POOL_DEPTH    5
#define UREF_POOL_DEPTH     5
#define UBUF_POOL_DEPTH     0
#define ITERATIONS          200
#define RATE                48000
#define SAMPLES             1024
#define DURATION            SAMPLES * UCLOCK_FREQ / RATE
#define CHANNELS            2
#define FREQ                440
#define STEP                (2. * M_PI * FREQ / RATE)
#define UPROBE_LOG_LEVEL    UPROBE_LOG_VERBOSE
#define ALIGN               0

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

int main(int argc, char **argv)
{
    printf("Compiled %s %s - %s\n", __DATE__, __TIME__, __FILE__);
    int i, j, k;

    /* uref and mem management */
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH,
                                                   udict_mgr, 0); 
    assert(uref_mgr != NULL);

    /* sound */
    struct ubuf_mgr *sound_mgr = ubuf_sound_mem_mgr_alloc(UBUF_POOL_DEPTH,
                                             UBUF_POOL_DEPTH, umem_mgr, 4, ALIGN);
    assert(sound_mgr);
    ubase_assert(ubuf_sound_mem_mgr_add_plane(sound_mgr, "lr"));

    /* uprobe stuff */
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_LEVEL);
    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                                     UBUF_POOL_DEPTH);
    assert(logger != NULL);

    /* build r128 pipe */
    struct upipe_mgr *upipe_filter_ebur128_mgr = upipe_filter_ebur128_mgr_alloc();
    assert(upipe_filter_ebur128_mgr);
    struct upipe *r128 = upipe_void_alloc(upipe_filter_ebur128_mgr,
        uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "r128"));
    assert(r128);

    struct uref *flow = uref_sound_flow_alloc_def(uref_mgr, "s16.", CHANNELS,
                                                  2 * CHANNELS);
    ubase_assert(uref_sound_flow_add_plane(flow, "lr"));
    ubase_assert(uref_sound_flow_set_rate(flow, RATE));
    ubase_assert(upipe_set_flow_def(r128, flow));

    struct upipe *null = upipe_void_alloc_output(r128,
        upipe_null_mgr_alloc(),
        uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "null"));
    assert(null);
    upipe_release(null);
    upipe_null_dump_dict(null, true);

    uref_free(flow);

    printf("packets duration : %"PRIu64"\n", DURATION);

    /* now send reference urefs */
    double phase = 0;
    for (i=0; i < ITERATIONS; i++) {
        struct uref *uref = uref_sound_alloc(uref_mgr, sound_mgr, SAMPLES);
        assert(uref);
        const char *channel = NULL;
        int16_t *sample = NULL;
        while (ubase_check(uref_sound_plane_iterate(uref, &channel))
                                                           && channel) {
            uref_sound_plane_write_int16_t(uref, channel, 0, -1, &sample);
            memset(sample, 0, 2 * CHANNELS * SAMPLES);
            #if 1
            for (j=0; j < SAMPLES; j++) {
                int16_t val = sin(phase) * INT16_MAX;
                for (k=0; k < CHANNELS; k++) {
                    sample[CHANNELS*j+k] = val;
                }
                phase += STEP;
                if (phase >= 2. * M_PI) {
                    phase = 0;
                }
            }
            #endif
            uref_sound_plane_unmap(uref, channel, 0, -1);
        }

        uref_clock_set_pts_sys(uref, UCLOCK_FREQ + i * DURATION);
        uref_clock_set_duration(uref, DURATION);
        upipe_input(r128, uref, NULL);
    }

    /* release pipe */
    upipe_release(r128);

    /* release managers */
    upipe_mgr_release(upipe_filter_ebur128_mgr); // no-op
    ubuf_mgr_release(sound_mgr);
    uref_mgr_release(uref_mgr);
    umem_mgr_release(umem_mgr);
    udict_mgr_release(udict_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe);

    return 0;
}
