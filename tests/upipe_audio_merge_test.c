/*
 * Copyright (C) 2014-2016 OpenHeadend S.A.R.L.
 *               2019 Open Broadcast Systems Ltd.
 *
 * Authors: Benjamin Cohen
 *          Christophe Massiot
 *          Sam Willcocks
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
 * @short unit tests for audio merge pipes
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_dump.h>
#include <upipe/uref_std.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/ubuf_mem.h>
#include <upipe/upipe.h>
#include <upipe-modules/upipe_audio_merge.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/ubuf_sound_mem.h>

#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#define UDICT_POOL_DEPTH    0
#define UREF_POOL_DEPTH     0
#define UBUF_POOL_DEPTH     0
#define SAMPLES             1024
#define UPROBE_LOG_LEVEL    UPROBE_LOG_VERBOSE
#define TEST_VALUE_1 0.75
#define TEST_VALUE_2 -0.5

// Fill ubuf with the provided value.
static void fill_in(struct ubuf *ubuf, float value)
{
    size_t size;
    uint8_t sample_size;
    ubase_assert(ubuf_sound_size(ubuf, &size, &sample_size));

    const char *channel = NULL;
    while (ubase_check(ubuf_sound_iterate_plane(ubuf, &channel)) &&
           channel != NULL) {
        float *buffer;
        ubase_assert(ubuf_sound_plane_write_float(ubuf, channel, 0, -1,
                                                    &buffer));

        for (int x = 0; x < size; x++)
            buffer[x] = value;
        ubase_assert(ubuf_sound_plane_unmap(ubuf, channel, 0, -1));
    }
}

/** definition of our uprobe */
static int catch(struct uprobe *uprobe, struct upipe *upipe,
                 int event, va_list args)
{
    switch (event) {
        default:
            printf("event: %d\n", event);
            //assert(0);
            break;
        case UPROBE_READY:
        case UPROBE_DEAD:
        case UPROBE_NEW_FLOW_DEF:
        case UPROBE_SOURCE_END:
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
    const float *r;

    size_t size;
    uint8_t sample_size;
    ubase_assert(ubuf_sound_size(uref->ubuf, &size, &sample_size));

    ubase_assert(uref_sound_plane_read_float(uref, "l", 0, -1, &r));
    for (int x = 0; x < size; x++)
        assert(r[x] == TEST_VALUE_1);
    ubase_assert(uref_sound_plane_unmap(uref, "l", 0, -1));

    ubase_assert(uref_sound_plane_read_float(uref, "r", 0, -1, &r));
    for (int x = 0; x < size; x++)
        assert(r[x] == TEST_VALUE_2);
    ubase_assert(uref_sound_plane_unmap(uref, "r", 0, -1));
    uref_free(uref);
}

/** helper phony pipe */
static int test_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *urequest = va_arg(args, struct urequest *);
            return upipe_throw_provide_request(upipe, urequest);
        }
        case UPIPE_UNREGISTER_REQUEST:
            return UBASE_ERR_NONE;
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
static struct upipe_mgr merge_test_mgr = {
    .refcount = NULL,
    .upipe_alloc = test_alloc,
    .upipe_input = test_input,
    .upipe_control = test_control
};

int main(int argc, char *argv[])
{
    /* upipe env */
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    assert(uref_mgr != NULL);

    struct uref *uref;
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stderr,
                                               UPROBE_LOG_LEVEL);
    assert(logger != NULL);
    logger = uprobe_uref_mgr_alloc(logger, uref_mgr);
    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                               UBUF_POOL_DEPTH);
    assert(logger != NULL);

    /* test pipes */
    struct upipe *upipe_sink = upipe_void_alloc(&merge_test_mgr,
                                                 uprobe_use(logger));
    assert(upipe_sink != NULL);

    /* merge superpipe */
    struct uref *output_flow = uref_sound_flow_alloc_def(uref_mgr, "f32.", 1, 4);
    assert(output_flow);
    ubase_assert(uref_sound_flow_set_rate(output_flow, 48000));
    ubase_assert(uref_sound_flow_set_samples(output_flow, SAMPLES));

    /* before we add channels/planes duplicate the flowdef for out input flow defs */
    struct uref *flow0 = uref_dup(output_flow);
    struct uref *flow1 = uref_dup(output_flow);

    ubase_assert(uref_sound_flow_add_plane(output_flow, "l"));
    ubase_assert(uref_sound_flow_add_plane(output_flow, "r"));
    ubase_assert(uref_sound_flow_set_channels(output_flow, 2));
    struct upipe_mgr *upipe_audio_merge_mgr = upipe_audio_merge_mgr_alloc();
    assert(upipe_audio_merge_mgr != NULL);
    struct upipe *upipe_audio_merge = upipe_flow_alloc(upipe_audio_merge_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "merge"), output_flow);
    assert(upipe_audio_merge != NULL);

    /* connect output of merge pipe to sink */
    ubase_assert(upipe_set_output(upipe_audio_merge, upipe_sink));

    /* merge subpipe 0 */
    ubase_assert(uref_sound_flow_add_plane(flow0, "l"));
    ubase_assert(uref_sound_flow_set_channels(flow0, 1));
    uref_dump(flow0, logger);
    struct upipe *upipe_audio_merge_input0 = upipe_void_alloc_sub(upipe_audio_merge,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "merge input 0"));
    assert(upipe_audio_merge_input0 != NULL);
    ubase_assert(upipe_set_flow_def(upipe_audio_merge_input0, flow0));

    /* merge subpipe 1 */
    ubase_assert(uref_sound_flow_add_plane(flow1, "r"));
    ubase_assert(uref_sound_flow_set_channels(flow1, 1));
    uref_dump(flow1, logger);
    struct upipe *upipe_audio_merge_input1 = upipe_void_alloc_sub(upipe_audio_merge,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "merge input 1"));
    assert(upipe_audio_merge_input1 != NULL);
    ubase_assert(upipe_set_flow_def(upipe_audio_merge_input1, flow1));

    uref_free(output_flow);

    /* input 0 sound ubuf manager */
    struct ubuf_mgr *sound_mgr_0 = ubuf_mem_mgr_alloc_from_flow_def(
                 UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, flow0);
    assert(sound_mgr_0);

    /* feed samples to input 0 */
    uref = uref_sound_alloc(uref_mgr, sound_mgr_0, SAMPLES);
    assert(uref != NULL);
    fill_in(uref->ubuf, TEST_VALUE_1);
    ubase_assert(uref_sound_flow_set_samples(uref, SAMPLES));
    upipe_input(upipe_audio_merge_input0, uref, NULL);

    /* input 1 sound ubuf manager */
    struct ubuf_mgr *sound_mgr_1 = ubuf_mem_mgr_alloc_from_flow_def(
                 UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, flow1);
    assert(sound_mgr_1);

    /* feed samples to input 1 */
    uref = uref_sound_alloc(uref_mgr, sound_mgr_1, SAMPLES);
    assert(uref != NULL);
    fill_in(uref->ubuf, TEST_VALUE_2);
    ubase_assert(uref_sound_flow_set_samples(uref, SAMPLES));
    upipe_input(upipe_audio_merge_input1, uref, NULL);

    upipe_release(upipe_audio_merge_input0);
    upipe_release(upipe_audio_merge_input1);

    /* clean */
    uref_free(flow0);
    uref_free(flow1);
    ubuf_mgr_release(sound_mgr_0);
    ubuf_mgr_release(sound_mgr_1);
    upipe_release(upipe_audio_merge);
    upipe_mgr_release(upipe_audio_merge_mgr); // nop

    test_free(upipe_sink);

    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);

    uprobe_release(logger);
    uprobe_clean(&uprobe);
    return 0;
}
