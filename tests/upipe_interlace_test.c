/*
 * Copyright (C) 2026 EasyTools
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

#undef NDEBUG

#include "upipe/uprobe.h"
#include "upipe/uprobe_prefix.h"
#include "upipe/uprobe_stdio.h"
#include "upipe/uprobe_ubuf_mem.h"
#include "upipe/umem.h"
#include "upipe/umem_alloc.h"
#include "upipe/udict.h"
#include "upipe/udict_inline.h"
#include "upipe/ubuf.h"
#include "upipe/ubuf_pic_mem.h"
#include "upipe/ubuf_mem.h"
#include "upipe/uref.h"
#include "upipe/uref_pic_flow.h"
#include "upipe/uref_pic.h"
#include "upipe/uref_std.h"
#include "upipe/upipe.h"
#include "upipe/uref_dump.h"
#include "upipe/uref_pic_flow_formats.h"
#include "upipe-modules/upipe_interlace.h"

#include <stdio.h>
#include <stdbool.h>
#include <assert.h>

#define UDICT_POOL_DEPTH    5
#define UREF_POOL_DEPTH     5
#define UBUF_POOL_DEPTH     5
#define UBUF_PREPEND        0
#define UBUF_APPEND         0
#define UBUF_ALIGN          32
#define UBUF_ALIGN_HOFFSET  0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

#define WIDTH               4
#define HEIGHT              8

static struct urational fps = { .num = 25, .den = 1 };
static struct umem_mgr *umem_mgr = NULL;
static struct uref_mgr *uref_mgr;
static struct upipe_mgr output_mgr;
static struct upipe output;
static int output_counter = 0;
static void (*current_test)(struct upipe *) = NULL;

static void test_no_input_flow_def(struct upipe *);
static void test_rgb_packed(struct upipe *);
static void test_yuv_planar(struct upipe *);
static void test_yuv_interlaced(struct upipe *);

static void (*tests[])(struct upipe *) = {
    test_no_input_flow_def,
    test_rgb_packed,
    test_yuv_planar,
    test_yuv_interlaced,
};

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

static void dump_pic_plane(struct uref *uref, const char *chroma)
{
    size_t height;
    size_t width;
    uint8_t macro;
    uint8_t hsub;
    uint8_t vsub;
    size_t stride;
    uint8_t macropixel;
    const uint8_t *buf;

    printf("    chroma %s:\n", chroma);

    ubase_assert(uref_pic_size(uref, &width, &height, &macro));
    ubase_assert(
        uref_pic_plane_size(uref, chroma, &stride, &hsub, &vsub, &macropixel));

    assert(hsub && vsub && width && height && macro && stride && macropixel);

    ubase_assert(uref_pic_plane_read(uref, chroma, 0, 0, -1, -1, &buf));
    for (uint64_t y = 0; y < (height / vsub); y++) {
        for (uint64_t x = 0; x < width * macropixel / hsub; x += macropixel) {
            printf(x ? " " : "      ");
            for (uint8_t o = 0; o < macropixel; o++)
                printf("%x", buf[x + o]);
        }
        printf("\n");
        buf += stride;
    }
    uref_pic_plane_unmap(uref, chroma, 0, 0, -1, -1);
}

static void dump_pic(struct uref *uref, const char *name)
{
    printf("%s:\n", name);
    const char *chroma;
    uref_pic_foreach_plane(uref, chroma) {
        dump_pic_plane(uref, chroma);
    }
}

static struct uref *pic_alloc(struct ubuf_mgr *ubuf_mgr, unsigned counter)
{
    struct uref *uref = uref_pic_alloc(uref_mgr, ubuf_mgr, WIDTH, HEIGHT);
    assert(uref);
    ubase_assert(uref_pic_set_progressive(uref, true));

    size_t width;
    size_t height;
    uint8_t macro;
    ubase_assert(uref_pic_size(uref, &width, &height, &macro));
    assert(width && height);

    const char *chroma;
    uref_pic_foreach_plane(uref, chroma) {
        uint8_t hsub;
        uint8_t vsub;
        uint8_t macropixel;
        size_t stride;
        uint8_t *buf;

        ubase_assert(uref_pic_plane_write(uref, chroma, 0, 0, -1, -1, &buf));
        ubase_assert(uref_pic_plane_size(uref, chroma, &stride, &hsub, &vsub,
                                         &macropixel));
        assert(stride && hsub && vsub);

        unsigned lines = height / vsub;
        unsigned size = width * macropixel / hsub;
        for (uint64_t y = 0; y < lines; y++) {
            for (uint64_t x = 0; x < size; x++)
                buf[x] = counter;
            buf += stride;
        }
        uref_pic_plane_unmap(uref, chroma, 0, 0, -1, -1);
    }
    return uref;
}

static int output_control(struct upipe *upipe, int command, va_list args)
{
    assert(current_test != test_no_input_flow_def);

    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            uref_dump_notice(flow_def, upipe->uprobe);
            ubase_nassert(uref_pic_check_progressive(flow_def));
            output_counter = 0;
            return UBASE_ERR_NONE;
        }
    }
    return UBASE_ERR_UNHANDLED;
}

static void output_input(struct upipe *upipe, struct uref *uref,
                         struct upump **upump_p)
{
    char *name = NULL;

    assert(current_test != test_no_input_flow_def);
    if (current_test == test_no_input_flow_def)

    uref_dump(uref, upipe->uprobe);
    assert(asprintf(&name, "output %i", output_counter) > 0);
    dump_pic(uref, name);
    free(name);
    ubase_nassert(uref_pic_check_progressive(uref));
    uref_free(uref);

    output_counter++;
}

static struct ubuf_mgr *test_alloc_ubuf_mgr(struct uref *flow_def)
{
    assert(flow_def);

    struct ubuf_mgr *ubuf_mgr = ubuf_mem_mgr_alloc_from_flow_def(
        UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, flow_def);
    assert(ubuf_mgr);

    return ubuf_mgr;
}

static void test_no_input_flow_def(struct upipe *upipe)
{
    struct uref *flow_def = uref_pic_flow_alloc_rgb24(uref_mgr);
    struct ubuf_mgr *ubuf_mgr = test_alloc_ubuf_mgr(flow_def);
    uref_free(flow_def);

    for (int counter = 0; counter < 10; counter++) {
        struct uref *uref = pic_alloc(ubuf_mgr, counter);

        printf("Sending pic %d\n", counter);

        char *name;
        assert(asprintf(&name, "input %i", counter) > 0);
        dump_pic(uref, name);
        free(name);
        upipe_input(upipe, uref, NULL);
    }

    ubuf_mgr_release(ubuf_mgr);
}

static void test_rgb_packed(struct upipe *upipe)
{
    struct uref *flow_def = uref_pic_flow_alloc_rgb24(uref_mgr);
    struct ubuf_mgr *ubuf_mgr = test_alloc_ubuf_mgr(flow_def);
    ubase_assert(uref_pic_set_progressive(flow_def, true));

    ubase_assert(upipe_interlace_set_drop(upipe, true));
    ubase_assert(upipe_interlace_set_tff(upipe, true));
    ubase_assert(upipe_set_flow_def(upipe, flow_def));
    for (int counter = 0; counter < 10; counter++) {
        struct uref *uref = pic_alloc(ubuf_mgr, counter);

        char *name;
        assert(asprintf(&name, "input %i", counter) > 0);
        dump_pic(uref, name);
        free(name);
        upipe_input(upipe, uref, NULL);
    }

    ubase_assert(upipe_interlace_set_drop(upipe, false));
    ubase_assert(upipe_interlace_set_tff(upipe, false));
    ubase_assert(upipe_set_flow_def(upipe, flow_def));
    for (int counter = 0; counter < 10; counter++) {
        struct uref *uref = pic_alloc(ubuf_mgr, counter);

        char *name;
        assert(asprintf(&name, "input %i", counter) > 0);
        dump_pic(uref, name);
        free(name);
        upipe_input(upipe, uref, NULL);
    }

    ubase_assert(uref_pic_flow_set_fps(flow_def, fps));

    ubase_assert(upipe_interlace_set_drop(upipe, true));
    ubase_assert(upipe_interlace_set_tff(upipe, false));
    ubase_assert(upipe_set_flow_def(upipe, flow_def));
    for (int counter = 0; counter < 10; counter++) {
        struct uref *uref = pic_alloc(ubuf_mgr, counter);

        char *name;
        assert(asprintf(&name, "input %i", counter) > 0);
        dump_pic(uref, name);
        free(name);
        upipe_input(upipe, uref, NULL);
    }

    ubase_assert(upipe_interlace_set_drop(upipe, false));
    ubase_assert(upipe_interlace_set_tff(upipe, true));
    ubase_assert(upipe_set_flow_def(upipe, flow_def));
    for (int counter = 0; counter < 10; counter++) {
        struct uref *uref = pic_alloc(ubuf_mgr, counter);

        char *name;
        assert(asprintf(&name, "input %i", counter) > 0);
        dump_pic(uref, name);
        free(name);
        upipe_input(upipe, uref, NULL);
    }

    ubuf_mgr_release(ubuf_mgr);
    uref_free(flow_def);
}

static void test_yuv_planar(struct upipe *upipe)
{
    struct uref *flow_def = uref_pic_flow_alloc_yuv420p(uref_mgr);
    struct ubuf_mgr *ubuf_mgr = test_alloc_ubuf_mgr(flow_def);
    ubase_assert(uref_pic_set_progressive(flow_def, true));

    ubase_assert(upipe_interlace_set_drop(upipe, true));
    ubase_assert(upipe_interlace_set_tff(upipe, true));
    ubase_assert(upipe_set_flow_def(upipe, flow_def));
    for (int counter = 0; counter < 10; counter++) {
        struct uref *uref = pic_alloc(ubuf_mgr, counter);

        char *name;
        assert(asprintf(&name, "input %i", counter) > 0);
        dump_pic(uref, name);
        free(name);
        upipe_input(upipe, uref, NULL);
    }

    ubase_assert(upipe_interlace_set_drop(upipe, false));
    ubase_assert(upipe_interlace_set_tff(upipe, false));
    ubase_assert(upipe_set_flow_def(upipe, flow_def));
    for (int counter = 0; counter < 10; counter++) {
        struct uref *uref = pic_alloc(ubuf_mgr, counter);

        char *name;
        assert(asprintf(&name, "input %i", counter) > 0);
        dump_pic(uref, name);
        free(name);
        upipe_input(upipe, uref, NULL);
    }

    ubase_assert(uref_pic_flow_set_fps(flow_def, fps));

    ubase_assert(upipe_interlace_set_drop(upipe, true));
    ubase_assert(upipe_interlace_set_tff(upipe, false));
    ubase_assert(upipe_set_flow_def(upipe, flow_def));
    for (int counter = 0; counter < 10; counter++) {
        struct uref *uref = pic_alloc(ubuf_mgr, counter);

        char *name;
        assert(asprintf(&name, "input %i", counter) > 0);
        dump_pic(uref, name);
        free(name);
        upipe_input(upipe, uref, NULL);
    }

    ubase_assert(upipe_interlace_set_drop(upipe, false));
    ubase_assert(upipe_interlace_set_tff(upipe, true));
    ubase_assert(upipe_set_flow_def(upipe, flow_def));
    for (int counter = 0; counter < 10; counter++) {
        struct uref *uref = pic_alloc(ubuf_mgr, counter);

        char *name;
        assert(asprintf(&name, "input %i", counter) > 0);
        dump_pic(uref, name);
        free(name);
        upipe_input(upipe, uref, NULL);
    }

    ubuf_mgr_release(ubuf_mgr);
    uref_free(flow_def);
}

static void test_yuv_interlaced(struct upipe *upipe)
{
    struct uref *flow_def = uref_pic_flow_alloc_yuv420p(uref_mgr);
    struct ubuf_mgr *ubuf_mgr = test_alloc_ubuf_mgr(flow_def);
    ubase_assert(uref_pic_set_progressive(flow_def, false));

    ubase_assert(upipe_interlace_set_drop(upipe, true));
    ubase_assert(upipe_interlace_set_tff(upipe, true));
    ubase_assert(upipe_set_flow_def(upipe, flow_def));
    for (int counter = 0; counter < 10; counter++) {
        struct uref *uref = pic_alloc(ubuf_mgr, counter);
        uref_pic_set_progressive(uref, false);

        char *name;
        assert(asprintf(&name, "input %i", counter) > 0);
        dump_pic(uref, name);
        free(name);
        upipe_input(upipe, uref, NULL);
    }

    ubase_assert(upipe_interlace_set_drop(upipe, false));
    ubase_assert(upipe_interlace_set_tff(upipe, false));
    ubase_assert(upipe_set_flow_def(upipe, flow_def));
    for (int counter = 0; counter < 10; counter++) {
        struct uref *uref = pic_alloc(ubuf_mgr, counter);
        uref_pic_set_progressive(uref, false);

        char *name;
        assert(asprintf(&name, "input %i", counter) > 0);
        dump_pic(uref, name);
        free(name);
        upipe_input(upipe, uref, NULL);
    }

    ubase_assert(uref_pic_flow_set_fps(flow_def, fps));

    ubase_assert(upipe_interlace_set_drop(upipe, true));
    ubase_assert(upipe_interlace_set_tff(upipe, false));
    ubase_assert(upipe_set_flow_def(upipe, flow_def));
    for (int counter = 0; counter < 10; counter++) {
        struct uref *uref = pic_alloc(ubuf_mgr, counter);
        uref_pic_set_progressive(uref, false);

        char *name;
        assert(asprintf(&name, "input %i", counter) > 0);
        dump_pic(uref, name);
        free(name);
        upipe_input(upipe, uref, NULL);
    }

    ubase_assert(upipe_interlace_set_drop(upipe, false));
    ubase_assert(upipe_interlace_set_tff(upipe, true));
    ubase_assert(upipe_set_flow_def(upipe, flow_def));
    for (int counter = 0; counter < 10; counter++) {
        struct uref *uref = pic_alloc(ubuf_mgr, counter);
        uref_pic_set_progressive(uref, false);

        char *name;
        assert(asprintf(&name, "input %i", counter) > 0);
        dump_pic(uref, name);
        free(name);
        upipe_input(upipe, uref, NULL);
    }

    ubuf_mgr_release(ubuf_mgr);
    uref_free(flow_def);
}

int main(int argc, char **argv)
{
    printf("Compiled %s %s (%s)\n", __DATE__, __TIME__, __FILE__);

    /* upipe env */
    umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH, umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    assert(uref_mgr != NULL);

    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_LEVEL);
    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_POOL_DEPTH);
    assert(logger != NULL);

    /* output pipe */
    upipe_mgr_init(&output_mgr);
    output_mgr.upipe_input = output_input;
    output_mgr.upipe_control = output_control;

    upipe_init(&output, upipe_mgr_use(&output_mgr), uprobe_use(logger));

    /* interlace */
    struct upipe_mgr *upipe_interlace_mgr = upipe_interlace_mgr_alloc();
    struct upipe *upipe_interlace = upipe_void_alloc(
        upipe_interlace_mgr,
        uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "interlace"));
    assert(upipe_interlace);
    ubase_assert(upipe_set_output(upipe_interlace, &output));

    for (unsigned i = 0; i < UBASE_ARRAY_SIZE(tests); i++) {
        current_test = tests[i];
        current_test(upipe_interlace);
    }

    upipe_release(upipe_interlace);
    upipe_clean(&output);

    upipe_mgr_release(upipe_interlace_mgr);
    uref_mgr_release(uref_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    return 0;
}
