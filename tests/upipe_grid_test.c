/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
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

#include <upipe/uclock_std.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref_std.h>
#include <upipe/ubuf_pic_mem.h>
#include <upipe/ubuf_sound_mem.h>

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>

#include <upipe/uref_void_flow.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_dump.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_clock.h>

#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_flow_def.h>

#include <upipe-modules/upipe_grid.h>

#include <assert.h>

#define UPROBE_LOG_LEVEL    UPROBE_LOG_DEBUG
#define UDICT_POOL_DEPTH    5
#define UREF_POOL_DEPTH     5
#define UBUF_POOL_DEPTH     5
#define UBUF_PREPEND        0
#define UBUF_APPEND         0
#define UBUF_ALIGN          16
#define UBUF_ALIGN_OFFSET   0
#define WIDTH               96
#define HEIGHT              64
#define SAMPLES             16
#define N_UREF              5
#define N_INPUT             2
#define N_OUTPUT            2

UREF_ATTR_SMALL_UNSIGNED(test, input_id, "input_id", input id);

struct sink {
    struct upipe upipe;
    struct urefcount urefcount;
    struct uref *flow_def;
    struct uref *flow_attr;
    uint64_t input_id;
    uint64_t count;
};

UPIPE_HELPER_UPIPE(sink, upipe, 0);
UPIPE_HELPER_UREFCOUNT(sink, urefcount, sink_free);
UPIPE_HELPER_VOID(sink);
UPIPE_HELPER_FLOW_DEF(sink, flow_def, flow_attr);

static void sink_free(struct upipe *upipe)
{
    struct sink *sink = sink_from_upipe(upipe);
    upipe_throw_dead(upipe);

    assert(sink->count == N_UREF);
    sink_clean_flow_def(upipe);
    sink_clean_urefcount(upipe);
    sink_free_void(upipe);
}

static struct upipe *sink_alloc(struct upipe_mgr *mgr,
                                struct uprobe *uprobe,
                                uint32_t signature, va_list args)
{
    struct upipe *upipe = sink_alloc_void(mgr, uprobe, signature, args);;
    if (unlikely(!upipe))
        return NULL;

    sink_init_urefcount(upipe);
    sink_init_flow_def(upipe);

    struct sink *sink = sink_from_upipe(upipe);
    sink->input_id = UINT64_MAX;
    sink->count = 0;

    upipe_throw_ready(upipe);

    return upipe;
}

static void sink_input(struct upipe *upipe, struct uref *uref,
                       struct upump **upump)
{
    struct sink *sink = sink_from_upipe(upipe);
    struct uref *flow_def = sink->flow_def;
    assert(flow_def);
    uref_dump(uref, upipe->uprobe);
    if (uref->ubuf) {
        if (!ubase_check(uref_flow_match_def(flow_def, UREF_PIC_FLOW_DEF)) &&
            !ubase_check(uref_flow_match_def(flow_def, UREF_SOUND_FLOW_DEF)))
            abort();
    }
    else
        ubase_assert(uref_flow_match_def(flow_def, UREF_VOID_FLOW_DEF));

    if (uref->ubuf) {
        uint8_t id;
        ubase_assert(uref_test_get_input_id(uref, &id));
        if (sink->input_id != UINT64_MAX)
            assert(id == (sink->input_id + 2) % N_INPUT);
        sink->input_id = id;
    }
    else
        assert(!sink->count);
    sink->count++;
    uref_free(uref);
}

static int sink_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    uref_dump(flow_def, upipe->uprobe);
    struct uref *flow_def_dup = uref_dup(flow_def);
    assert(flow_def_dup);
    sink_store_flow_def_input(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

static int sink_control(struct upipe *upipe,
                        int command, va_list args)
{
    switch (command) {
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

static int catch(struct uprobe *uprobe, struct upipe *upipe,
                 int event, va_list args)
{
    return UBASE_ERR_NONE;
}

int main(int argc, char *argv[])
{
    struct uclock *uclock = uclock_std_alloc(0);
    assert(uclock);

    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr);

    struct udict_mgr *udict_mgr =
        udict_inline_mgr_alloc(UDICT_POOL_DEPTH, umem_mgr, -1, -1);
    assert(udict_mgr);

    struct uref_mgr *uref_mgr =
        uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    assert(uref_mgr);

    struct ubuf_mgr *ubuf_pic_mgr =
        ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH,
                               umem_mgr, 1, UBUF_PREPEND, UBUF_APPEND,
                               UBUF_PREPEND, UBUF_APPEND,
                               UBUF_ALIGN, UBUF_ALIGN_OFFSET);
    assert(ubuf_pic_mgr);

    struct ubuf_mgr *ubuf_sound_mgr =
        ubuf_sound_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH,
                                 umem_mgr, 4 * 2, 4 * 2);
    assert(ubuf_sound_mgr);

    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);

    struct uprobe *logger =
        uprobe_stdio_alloc(&uprobe, stderr, UPROBE_LOG_LEVEL);
    assert(logger);

    struct upipe_mgr *upipe_grid_mgr = upipe_grid_mgr_alloc();
    assert(upipe_grid_mgr);

    struct upipe *upipe_grid =
        upipe_void_alloc(upipe_grid_mgr,
                         uprobe_pfx_alloc(uprobe_use(logger),
                                          UPROBE_LOG_LEVEL,
                                          "grid"));
    assert(upipe_grid);

    struct uref *pic_flow_def = uref_pic_flow_alloc_def(uref_mgr, 0);
    assert(pic_flow_def);

    struct uref *sound_flow_def = uref_sound_flow_alloc_def(uref_mgr, "f32.",
                                                            2, 4 * 2);
    assert(sound_flow_def);

    struct upipe *inputs[N_INPUT * 2];
    for (unsigned i = 0; i < N_INPUT * 2; i++) {
        inputs[i] =
            upipe_grid_alloc_input(upipe_grid,
                                    uprobe_pfx_alloc_va(
                                        uprobe_use(logger),
                                        UPROBE_LOG_LEVEL,
                                        "in %s %u",
                                        i % 2 ? "sound" : "pic", i));
        assert(inputs[i]);

        if (i % 2)
            ubase_assert(upipe_set_flow_def(inputs[i], sound_flow_def));
        else
            ubase_assert(upipe_set_flow_def(inputs[i], pic_flow_def));
    }
    uref_free(pic_flow_def);
    uref_free(sound_flow_def);

    struct uref *flow_def = uref_void_flow_alloc_def(uref_mgr);
    assert(flow_def);
    struct upipe *outputs[N_OUTPUT * 2];
    for (unsigned i = 0; i < N_OUTPUT * 2; i++) {
        outputs[i] =
            upipe_grid_alloc_output(upipe_grid,
                                     uprobe_pfx_alloc_va(
                                        uprobe_use(logger),
                                        UPROBE_LOG_LEVEL,
                                        "out %s %u",
                                        i % 2 ? "sound" : "pic", i));
        assert(outputs[i]);
        ubase_assert(upipe_set_flow_def(outputs[i], flow_def));

        struct upipe *sink =
            upipe_void_alloc_output(outputs[i],
                                    &sink_mgr,
                                    uprobe_pfx_alloc_va(uprobe_use(logger),
                                                        UPROBE_LOG_LEVEL,
                                                        "sink %u", i));
        assert(sink);
        upipe_release(sink);
    }
    uref_free(flow_def);

    struct uref *urefs[N_INPUT * 2][N_UREF];
    for (unsigned i = 0; i < N_INPUT * 2; i++) {
        for (unsigned j = 0; j < N_UREF; j++) {
            if (i % 2)
                urefs[i][j] =
                    uref_sound_alloc(uref_mgr, ubuf_sound_mgr, SAMPLES);
            else
                urefs[i][j] =
                    uref_pic_alloc(uref_mgr, ubuf_pic_mgr, WIDTH, HEIGHT);
            assert(urefs[i][j]);
            ubase_assert(uref_test_set_input_id(urefs[i][j], i));
        }
    }

    uint64_t now = 4242;
    static const uint64_t duration = 42;
    for (unsigned i = 0; i < N_UREF; i++) {
        if (i) {
            for (unsigned j = 0; j < N_OUTPUT * 2; j++) {
                ubase_assert(upipe_grid_out_set_input(
                    outputs[j], inputs[(j + i * 2 - 2) % N_INPUT]));
            }
        }

        for (unsigned j = 0; j < N_INPUT * 2; j++) {
            uref_clock_set_pts_sys(urefs[j][i], now);
            uref_clock_set_duration(urefs[j][i], duration);
            upipe_input(inputs[j], urefs[j][i], NULL);
        }

        struct uref *uref = uref_alloc_control(uref_mgr);
        assert(uref);
        uref_clock_set_pts_sys(uref, now);
        ubase_assert(uref_clock_set_duration(uref, duration));

        for (unsigned j = 0; j < N_OUTPUT * 2; j++) {
            struct uref *copy = uref_dup(uref);
            assert(copy);
            upipe_input(outputs[j], copy, NULL);
        }

        uref_free(uref);

        now += duration;
    }

    for (unsigned i = 0; i < N_OUTPUT * 2; i++)
        upipe_release(outputs[i]);
    for (unsigned i = 0; i < N_INPUT * 2; i++)
        upipe_release(inputs[i]);
    assert(upipe_single(upipe_grid));
    upipe_release(upipe_grid);
    upipe_mgr_release(upipe_grid_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe);
    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_pic_mgr);
    ubuf_mgr_release(ubuf_sound_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uclock_release(uclock);

    return 0;
}
