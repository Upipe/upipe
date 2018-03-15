/*
 * Copyright (C) 2014-2016 OpenHeadend S.A.R.L.
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

/** @file
 * @short unit tests for audio split pipes
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
#include <upipe/uref_std.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/ubuf_mem.h>
#include <upipe/upipe.h>
#include <upipe-modules/upipe_audio_split.h>

#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#define UDICT_POOL_DEPTH    0
#define UREF_POOL_DEPTH     0
#define UBUF_POOL_DEPTH     0
#define SAMPLES             1024
#define UPROBE_LOG_LEVEL    UPROBE_LOG_VERBOSE

static int counter = 0;

static void fill_in(struct ubuf *ubuf)
{
    size_t size;
    uint8_t sample_size;
    ubase_assert(ubuf_sound_size(ubuf, &size, &sample_size));
    int octets = size * sample_size;

    const char *channel = NULL;
    while (ubase_check(ubuf_sound_iterate_plane(ubuf, &channel)) &&
           channel != NULL) {
        uint8_t *buffer;
        ubase_assert(ubuf_sound_plane_write_uint8_t(ubuf, channel, 0, -1,
                                                    &buffer));

        for (int x = 0; x < octets; x++)
            buffer[x] = (uint8_t)channel[0] + x;
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
            assert(0);
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
    const uint8_t *r;
    switch (counter) {
        case 0:
        case 1: /* this assumes a certain ordering of subpipes */
            ubase_assert(uref_sound_plane_read_uint8_t(uref, "r", 0, -1, &r));
            assert(r[0] == 'l' + 2);
            assert(r[1] == 'l' + 3);
            assert(r[2] == 'l' + 10);
            ubase_assert(uref_sound_plane_unmap(uref, "r", 0, -1));
            break;
        case 2:
            ubase_assert(uref_sound_plane_read_uint8_t(uref, "l", 0, -1, &r));
            assert(r[0] == 'l' + 0);
            assert(r[1] == 'l' + 1);
            assert(r[2] == 'l' + 8);
            ubase_assert(uref_sound_plane_unmap(uref, "l", 0, -1));
            ubase_assert(uref_sound_plane_read_uint8_t(uref, "r", 0, -1, &r));
            assert(r[0] == 'l' + 2);
            assert(r[1] == 'l' + 3);
            assert(r[2] == 'l' + 10);
            ubase_assert(uref_sound_plane_unmap(uref, "r", 0, -1));
            break;
        case 3:
            ubase_assert(uref_sound_plane_read_uint8_t(uref, "lr", 0, -1, &r));
            assert(r[0] == 'l' + 0);
            assert(r[1] == 'l' + 1);
            assert(r[2] == 'l' + 2);
            assert(r[3] == 'l' + 3);
            assert(r[4] == 'l' + 8);
            assert(r[6] == 'l' + 10);
            ubase_assert(uref_sound_plane_unmap(uref, "lr", 0, -1));
            break;
        default:
            assert(0);
    }
    counter++;
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
static struct upipe_mgr split_test_mgr = {
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
    struct uref *flow;
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stderr,
                                               UPROBE_LOG_LEVEL);
    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                               UBUF_POOL_DEPTH);
    assert(logger != NULL);

    /* test pipes */
    struct upipe *upipe_sink0 = upipe_void_alloc(&split_test_mgr,
                                                 uprobe_use(logger));
    assert(upipe_sink0 != NULL);
    struct upipe *upipe_sink1 = upipe_void_alloc(&split_test_mgr,
                                                 uprobe_use(logger));
    assert(upipe_sink1 != NULL);

    /* input flow definition */
    flow = uref_sound_flow_alloc_def(uref_mgr, "s16.", 4, 8);
    ubase_assert(uref_sound_flow_add_plane(flow, "lrLR"));
    assert(flow != NULL);

    /* input sound ubuf manager */
    struct ubuf_mgr *sound_mgr = ubuf_mem_mgr_alloc_from_flow_def(
                 UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, flow);
    assert(sound_mgr);


    /* grand pipe */
    struct upipe_mgr *upipe_audio_split_mgr = upipe_audio_split_mgr_alloc();
    assert(upipe_audio_split_mgr != NULL);
    struct upipe *upipe_audio_split = upipe_void_alloc(upipe_audio_split_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "split"));
    assert(upipe_audio_split != NULL);
    ubase_assert(upipe_set_flow_def(upipe_audio_split, flow));
    uref_free(flow);

    /* split subpipe */
    flow = uref_sound_flow_alloc_def(uref_mgr, "", 1, 0);
    ubase_assert(uref_sound_flow_add_plane(flow, "r"));
    ubase_assert(uref_audio_split_set_bitfield(flow, 0x2));
    struct upipe *upipe_audio_split_output0 = upipe_flow_alloc_sub(upipe_audio_split,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "split output 0"), flow);
    uref_free(flow);
    assert(upipe_audio_split_output0 != NULL);
    ubase_assert(upipe_set_output(upipe_audio_split_output0, upipe_sink0));

    /* feed samples */
    uref = uref_sound_alloc(uref_mgr, sound_mgr, SAMPLES);
    assert(uref != NULL);
    fill_in(uref->ubuf);
    upipe_input(upipe_audio_split, uref, NULL);
    assert(counter == 1);

    /* split subpipe */
    flow = uref_sound_flow_alloc_def(uref_mgr, "", 2, 0);
    ubase_assert(uref_sound_flow_add_plane(flow, "l"));
    ubase_assert(uref_sound_flow_add_plane(flow, "r"));
    ubase_assert(uref_audio_split_set_bitfield(flow, 0x3));
    struct upipe *upipe_audio_split_output1 = upipe_flow_alloc_sub(upipe_audio_split,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "split output 1"), flow);
    uref_free(flow);
    assert(upipe_audio_split_output1 != NULL);
    ubase_assert(upipe_set_output(upipe_audio_split_output1, upipe_sink1));
    assert(counter == 1);

    /* feed samples again */
    uref = uref_sound_alloc(uref_mgr, sound_mgr, SAMPLES);
    assert(uref != NULL);
    fill_in(uref->ubuf);
    upipe_input(upipe_audio_split, uref, NULL);
    assert(counter == 3);

    upipe_release(upipe_audio_split_output0);
    upipe_release(upipe_audio_split_output1);

    /* split subpipe */
    flow = uref_sound_flow_alloc_def(uref_mgr, "", 2, 0);
    ubase_assert(uref_sound_flow_add_plane(flow, "lr"));
    ubase_assert(uref_audio_split_set_bitfield(flow, 0x3));
    upipe_audio_split_output0 = upipe_flow_alloc_sub(upipe_audio_split,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "split output 0"), flow);
    uref_free(flow);
    assert(upipe_audio_split_output0 != NULL);
    ubase_assert(upipe_set_output(upipe_audio_split_output0, upipe_sink0));

    /* feed samples again */
    uref = uref_sound_alloc(uref_mgr, sound_mgr, SAMPLES);
    assert(uref != NULL);
    fill_in(uref->ubuf);
    upipe_input(upipe_audio_split, uref, NULL);
    assert(counter == 4);

    upipe_release(upipe_audio_split_output0);

    /* clean */
    ubuf_mgr_release(sound_mgr);
    upipe_release(upipe_audio_split);
    upipe_mgr_release(upipe_audio_split_mgr); // nop

    test_free(upipe_sink0);
    test_free(upipe_sink1);

    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);

    uprobe_release(logger);
    uprobe_clean(&uprobe);
    return 0;
}
