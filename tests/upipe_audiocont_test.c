/*
 * Copyright (C) 2014-2015 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *          Christophe Massiot
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
#include <upipe/ubuf_mem.h>
#include <upipe-modules/upipe_audiocont.h>
#include <upipe-modules/upipe_null.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#define UDICT_POOL_DEPTH    5
#define UREF_POOL_DEPTH     5
#define UBUF_POOL_DEPTH     0
#define ITERATIONS          5
#define INPUT_NUM           7
#define INPUT_RATE          48000
#define SAMPLES             1024
#define DURATION            SAMPLES * UCLOCK_FREQ / INPUT_RATE
#define UPROBE_LOG_LEVEL    UPROBE_LOG_VERBOSE

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
    int i, j;
    struct upipe *subpipe[INPUT_NUM];

    /* uref and mem management */
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH,
                                                   udict_mgr, 0); 
    assert(uref_mgr != NULL);

    /* uprobe stuff */
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_LEVEL);
    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                                     UBUF_POOL_DEPTH);
    assert(logger != NULL);

    /* reference flow definition */
    struct uref *ref_flow = uref_sound_flow_alloc_def(uref_mgr, "f32.", 2, 8);
    ubase_assert(uref_sound_flow_add_plane(ref_flow, "lr"));
    ubase_assert(uref_sound_flow_set_rate(ref_flow, INPUT_RATE));

    /* build audiocont pipe */
    struct upipe_mgr *upipe_audiocont_mgr = upipe_audiocont_mgr_alloc();
    assert(upipe_audiocont_mgr);
    struct upipe *audiocont = upipe_flow_alloc(upipe_audiocont_mgr,
        uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "audiocont"),
        ref_flow);
    assert(audiocont);
    ubase_assert(upipe_set_flow_def(audiocont, ref_flow));

    const char *input_name;
    ubase_assert(upipe_audiocont_get_input(audiocont, &input_name));
    assert(input_name == NULL);

    ubase_assert(upipe_audiocont_set_input(audiocont, "bar3"));

    ubase_assert(upipe_audiocont_get_input(audiocont, &input_name));
    assert(input_name != NULL);
    
    ubase_assert(upipe_audiocont_get_current_input(audiocont, &input_name));
    assert(input_name == NULL);

    /* wrong input (sub) flow definition */
    struct uref *wrong_flow = uref_sound_flow_alloc_def(uref_mgr, "f32.", 1, 4);
    ubase_assert(uref_sound_flow_add_plane(wrong_flow, "c"));
    ubase_assert(uref_sound_flow_set_rate(wrong_flow, INPUT_RATE));
    struct upipe *sub = upipe_void_alloc_sub(audiocont,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "sub"));
    ubase_nassert(upipe_set_flow_def(sub, wrong_flow));
    uref_free(wrong_flow);
    upipe_release(sub);

    struct upipe *null = upipe_void_alloc_output(audiocont,
        upipe_null_mgr_alloc(),
        uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "null"));
    assert(null);
    upipe_release(null);
    upipe_null_dump_dict(null, true);

    /* input subpipes */
    for (i=0; i < INPUT_NUM; i++) {
        subpipe[i] = upipe_void_alloc_sub(audiocont,
            uprobe_pfx_alloc_va(uprobe_use(logger),
                                UPROBE_LOG_LEVEL, "sub%d", i));
        assert(subpipe[i]);
        struct uref *subflow = uref_dup(ref_flow);
        char name[20];
        snprintf(name, sizeof(name), "bar%d", i);
        uref_flow_set_name(subflow, name);
        ubase_assert(upipe_set_flow_def(subpipe[i], subflow));
        uref_free(subflow);
    }

    /* ref sound ubuf manager */
    struct ubuf_mgr *ref_sound_mgr = ubuf_mem_mgr_alloc_from_flow_def(
                UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, ref_flow);
    assert(ref_sound_mgr);

    uref_free(ref_flow);

    /* test input commutation controles */
    input_name = NULL;
    ubase_assert(upipe_audiocont_get_current_input(audiocont, &input_name));
    assert(input_name != NULL);

    ubase_assert(upipe_audiocont_set_input(audiocont, "bar2"));
    ubase_assert(upipe_audiocont_sub_set_input(subpipe[1]));
    ubase_assert(upipe_audiocont_set_input(audiocont, NULL));
    ubase_assert(upipe_audiocont_sub_set_input(subpipe[1]));

    input_name = NULL;
    ubase_assert(upipe_audiocont_get_current_input(audiocont, &input_name));
    assert(input_name != NULL);

    printf("packets duration : %"PRIu64"\n", DURATION);

    /* input urefs */
    for (j=0; j < INPUT_NUM; j++) {
        for (i=0; i < ITERATIONS + j; i++) {
            struct uref *uref = uref_sound_alloc(uref_mgr, ref_sound_mgr, SAMPLES);
            uref_clock_set_pts_sys(uref, UCLOCK_FREQ + i * DURATION - (DURATION / 10));
            uref_clock_set_duration(uref, DURATION);
            upipe_input(subpipe[j], uref, NULL);
        }
    }

    /* now send reference urefs */
    for (i=0; i < ITERATIONS; i++) {
        struct uref *uref = uref_sound_alloc(uref_mgr, ref_sound_mgr, SAMPLES);
        uref_clock_set_pts_sys(uref, UCLOCK_FREQ + i * DURATION);
        uref_clock_set_duration(uref, DURATION);
        struct uref *dup = NULL;
        if (i % 2 == 0) {
            dup = uref_dup(uref);
        }
        upipe_input(audiocont, uref, NULL);
        if (dup) uref_free(dup);
    }

    ubuf_mgr_release(ref_sound_mgr);

    for (i=0; i < INPUT_NUM; i++) {
        upipe_release(subpipe[i]);
    }

    /* release pipe */
    upipe_release(audiocont);

    /* release managers */
    upipe_mgr_release(upipe_audiocont_mgr); // no-op
    uref_mgr_release(uref_mgr);
    umem_mgr_release(umem_mgr);
    udict_mgr_release(udict_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe);

    return 0;
}
