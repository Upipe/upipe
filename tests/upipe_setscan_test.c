/*
 * Copyright (C) 2026 EasyTools
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

#include "upipe/uprobe.h"
#include "upipe/uprobe_prefix.h"
#include "upipe/uprobe_stdio.h"
#include "upipe/uprobe_ubuf_mem.h"
#include "upipe/umem.h"
#include "upipe/umem_alloc.h"
#include "upipe/udict.h"
#include "upipe/udict_inline.h"
#include "upipe/ubuf.h"
#include "upipe/ubuf_mem.h"
#include "upipe/uref.h"
#include "upipe/uref_pic_flow.h"
#include "upipe/uref_pic.h"
#include "upipe/uref_pic_flow_formats.h"
#include "upipe/uref_std.h"
#include "upipe/upipe.h"
#include "upipe-modules/upipe_setscan.h"

#include <stdio.h>
#include <stdbool.h>
#include <assert.h>

#undef NDEBUG
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG
#define COUNT               10
#define WIDTH               720
#define HEIGHT              576

static struct uref_mgr *uref_mgr;
static bool output_progressive;
static int in_count = 0, out_count = 0;

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
    }
    return UBASE_ERR_NONE;
}

static int sink_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            bool progressive;
            ubase_assert(uref_pic_get_progressive(flow_def, &progressive));
            assert(progressive == output_progressive);
            return UBASE_ERR_NONE;
        }
    }
    return UBASE_ERR_UNHANDLED;
}

static void sink_input(struct upipe *upipe, struct uref *uref,
                       struct upump **upump_p)
{
    bool progressive;
    uref_pic_get_progressive(uref, &progressive);
    assert(progressive == output_progressive);
    uref_free(uref);
    upipe_dbg_va(upipe, "Receiving pic %d", out_count);
    out_count++;
}

static struct upipe_mgr sink_mgr = {
    .upipe_input = sink_input,
    .upipe_control = sink_control,
};

int main(int argc, char **argv)
{
    struct uref *pic;
    struct uref *input_flow_def, *output_flow_def;
    struct upipe *setscan;
    struct ubuf_mgr *ubuf_mgr;

    /* upipe env */
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(0, umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    uref_mgr = uref_std_mgr_alloc(0, udict_mgr, 0);
    assert(uref_mgr != NULL);

    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_LEVEL);
    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, 0, 0);
    assert(logger != NULL);

    struct upipe_mgr *setscan_mgr = upipe_setscan_mgr_alloc();
    assert(setscan_mgr);

    struct upipe sink;
    upipe_init(&sink, &sink_mgr, uprobe_use(logger));

    /* output progressive */
    output_progressive = true;
    input_flow_def = uref_pic_flow_alloc_yuv420p(uref_mgr);
    uref_pic_set_progressive(input_flow_def, !output_progressive);
    output_flow_def = uref_pic_flow_alloc_yuv420p(uref_mgr);
    uref_pic_set_progressive(output_flow_def, output_progressive);

    /* setscan */
    setscan = upipe_flow_alloc(
        setscan_mgr,
        uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "setscan"),
        output_flow_def);
    assert(setscan);
    ubase_assert(upipe_set_flow_def(setscan, input_flow_def));
    ubase_assert(upipe_set_output(setscan, &sink));

    /* ubuf manager */
    ubuf_mgr = ubuf_mem_mgr_alloc_from_flow_def(0, 0, umem_mgr, input_flow_def);
    for (in_count = 0, out_count = 0; in_count < COUNT; in_count++) {
        uprobe_dbg_va(logger, NULL, "Sending pic %d", in_count);
        pic = uref_pic_alloc(uref_mgr, ubuf_mgr, WIDTH, HEIGHT);
        assert(pic);

        const char *chroma;
        uref_pic_foreach_plane(pic, chroma) {
            int x, y;
            uint8_t *buf, macropixel = 0;
            uint8_t vsub, hsub;
            size_t stride = 0;

            ubase_assert(uref_pic_plane_write(pic, chroma, 0, 0, -1, -1, &buf));
            ubase_assert(uref_pic_plane_size(pic, chroma, &stride, &hsub, &vsub,
                                             &macropixel));
            for (y = 0; y < HEIGHT / vsub; y++) {
                for (x = 0; x < WIDTH / vsub; x++) {
                    buf[macropixel * x] = x + y + in_count * 3;
                    buf[macropixel * x + 1] = x + y + in_count * 3 * 10;
                    buf[macropixel * x + 2] = x + y + in_count * 3 * 10;
                }
                buf += stride;
            }
            uref_pic_plane_unmap(pic, chroma, 0, 0, -1, -1);
        }
        upipe_input(setscan, pic, NULL);
    }
    ubuf_mgr_release(ubuf_mgr);
    upipe_release(setscan);
    uref_free(output_flow_def);
    uref_free(input_flow_def);
    assert(out_count == COUNT);

    /* output interlaced */
    output_progressive = false;
    input_flow_def = uref_pic_flow_alloc_yuv420p(uref_mgr);
    uref_pic_set_progressive(input_flow_def, !output_progressive);
    output_flow_def = uref_pic_flow_alloc_yuv420p(uref_mgr);
    uref_pic_set_progressive(output_flow_def, output_progressive);

    /* setscan */
    setscan = upipe_flow_alloc(
        setscan_mgr,
        uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "setscan"),
        output_flow_def);
    assert(setscan);
    ubase_assert(upipe_set_flow_def(setscan, input_flow_def));
    ubase_assert(upipe_set_output(setscan, &sink));

    /* ubuf manager */
    ubuf_mgr = ubuf_mem_mgr_alloc_from_flow_def(0, 0, umem_mgr, input_flow_def);
    for (in_count = 0, out_count = 0; in_count < COUNT; in_count++) {
        uprobe_dbg_va(logger, NULL, "Sending pic %d", in_count);
        pic = uref_pic_alloc(uref_mgr, ubuf_mgr, WIDTH, HEIGHT);
        assert(pic);

        const char *chroma;
        uref_pic_foreach_plane(pic, chroma) {
            int x, y;
            uint8_t *buf, macropixel = 0;
            uint8_t vsub, hsub;
            size_t stride = 0;

            ubase_assert(uref_pic_plane_write(pic, chroma, 0, 0, -1, -1, &buf));
            ubase_assert(uref_pic_plane_size(pic, chroma, &stride, &hsub, &vsub,
                                             &macropixel));
            for (y = 0; y < HEIGHT / vsub; y++) {
                for (x = 0; x < WIDTH / vsub; x++) {
                    buf[macropixel * x] = x + y + in_count * 3;
                    buf[macropixel * x + 1] = x + y + in_count * 3 * 10;
                    buf[macropixel * x + 2] = x + y + in_count * 3 * 10;
                }
                buf += stride;
            }
            uref_pic_plane_unmap(pic, chroma, 0, 0, -1, -1);
        }
        upipe_input(setscan, pic, NULL);
    }
    ubuf_mgr_release(ubuf_mgr);
    upipe_release(setscan);
    uref_free(output_flow_def);
    assert(out_count == COUNT);

    uref_free(input_flow_def);
    upipe_clean(&sink);
    upipe_mgr_release(setscan_mgr);
    uref_mgr_release(uref_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    return 0;
}
