/*
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
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
#include <upipe/uref_sound.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_dump.h>
#include <upipe/upipe.h>
#include <upipe/ubuf_sound_mem.h>
#include <upipe-filters/upipe_audio_max.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>

#define UDICT_POOL_DEPTH    5
#define UREF_POOL_DEPTH     5
#define UBUF_POOL_DEPTH     0
#define SAMPLES             1024
#define UPROBE_LOG_LEVEL    UPROBE_LOG_VERBOSE
#define ALIGN               0

static bool got_urequest = false;
static bool got_input = false;

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

/** helper phony pipe */
static struct upipe *test_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe,
                                uint32_t signature, va_list args)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    upipe_init(upipe, mgr, uprobe);
    upipe_throw_ready(upipe);
    return upipe;
}

/** helper phony pipe */
static void test_input(struct upipe *upipe, struct uref *uref,
                       struct upump **upump_p)
{
    assert(uref != NULL);
    upipe_dbg(upipe, "===> received input uref");
    uref_dump(uref, upipe->uprobe);
    double amplitude;
    ubase_assert(uref_amax_get_amplitude(uref, &amplitude, 0));
    assert(amplitude == (SAMPLES - 1) * 1.0f / INT16_MAX);
    ubase_assert(uref_amax_get_amplitude(uref, &amplitude, 1));
    assert(amplitude == (SAMPLES * 2 - 1) * 1.0f / INT16_MAX);

    uref_free(uref);
    got_input = true;
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
    upipe_dbg(upipe, "releasing pipe");
    upipe_throw_dead(upipe);
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe */
static struct upipe_mgr test_mgr = {
    .refcount = NULL,
    .signature = 0,
    .upipe_alloc = test_alloc,
    .upipe_input = test_input,
    .upipe_control = test_control
};

static int provide_urequest(struct urequest *urequest, va_list args)
{
    struct uref *flow_format = va_arg(args, struct uref *);
    assert(flow_format != NULL);
    uint8_t channels;
    ubase_assert(uref_sound_flow_get_channels(flow_format, &channels));
    assert(channels == 2);
    uint8_t planes;
    ubase_assert(uref_sound_flow_get_planes(flow_format, &planes));
    assert(planes == 2);
    ubase_assert(uref_sound_flow_check_channel(flow_format, "l"));
    ubase_assert(uref_sound_flow_check_channel(flow_format, "r"));
    got_urequest = true;
    uref_free(flow_format);
    return UBASE_ERR_NONE;
}

static void fill_in(struct ubuf *ubuf)
{
    size_t size;
    uint8_t sample_size;
    ubase_assert(ubuf_sound_size(ubuf, &size, &sample_size));

    const char *channel;
    int16_t v = 0;
    ubuf_sound_foreach_plane(ubuf, channel) {
        int16_t *buffer;
        ubase_assert(ubuf_sound_plane_write_int16_t(ubuf, channel, 0, -1,
                                                    &buffer));

        for (int x = 0; x < size; x++)
            buffer[x] = v++;
        ubase_assert(ubuf_sound_plane_unmap(ubuf, channel, 0, -1));
    }
}

int main(int argc, char **argv)
{
    printf("Compiled %s %s - %s\n", __DATE__, __TIME__, __FILE__);

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
                                             UBUF_POOL_DEPTH, umem_mgr, 2, ALIGN);
    assert(sound_mgr);
    ubase_assert(ubuf_sound_mem_mgr_add_plane(sound_mgr, "l"));
    ubase_assert(ubuf_sound_mem_mgr_add_plane(sound_mgr, "r"));

    /* uprobe stuff */
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_LEVEL);
    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                                     UBUF_POOL_DEPTH);
    assert(logger != NULL);

    /* build amax pipe */
    struct upipe_mgr *upipe_amax_mgr = upipe_amax_mgr_alloc();
    assert(upipe_amax_mgr);
    struct upipe *amax = upipe_void_alloc(upipe_amax_mgr,
        uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "amax"));
    assert(amax);

    /* build phony pipe */
    struct upipe *test = upipe_void_alloc(&test_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "test"));
    assert(test != NULL);
    ubase_assert(upipe_set_output(amax, test));

    /* test urequest flow format */
    struct uref *flow_def =
        uref_sound_flow_alloc_def(uref_mgr, "s16.", 2, 2 * 2);
    ubase_assert(uref_sound_flow_add_plane(flow_def, "lr"));
    struct urequest request;
    urequest_init_flow_format(&request, flow_def, provide_urequest, NULL);
    upipe_register_request(amax, &request);
    assert(got_urequest);
    upipe_unregister_request(amax, &request);
    urequest_clean(&request);

    flow_def = uref_sound_flow_alloc_def(uref_mgr, "s16.", 2, 2);
    ubase_assert(uref_sound_flow_add_plane(flow_def, "l"));
    ubase_assert(uref_sound_flow_add_plane(flow_def, "r"));
    ubase_assert(upipe_set_flow_def(amax, flow_def));
    uref_free(flow_def);

    struct uref *uref = uref_sound_alloc(uref_mgr, sound_mgr, SAMPLES);
    fill_in(uref->ubuf);
    upipe_input(amax, uref, NULL);
    assert(got_input);

    /* release pipe */
    upipe_release(amax);
    test_free(test);

    /* release managers */
    upipe_mgr_release(upipe_amax_mgr); // no-op
    ubuf_mgr_release(sound_mgr);
    uref_mgr_release(uref_mgr);
    umem_mgr_release(umem_mgr);
    udict_mgr_release(udict_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe);

    return 0;
}
