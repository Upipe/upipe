/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
 *
 * Authors: Clément Vasseur
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short unit tests for upipe x265 module
 */

#undef NDEBUG

#include "upipe/uclock.h"
#include "upipe/uprobe.h"
#include "upipe/uprobe_prefix.h"
#include "upipe/uprobe_stdio.h"
#include "upipe/uprobe_ubuf_mem.h"
#include "upipe/uprobe_upump_mgr.h"
#include "upipe/umem.h"
#include "upipe/umem_alloc.h"
#include "upipe/ubuf.h"
#include "upipe/ubuf_mem.h"
#include "upipe/udict.h"
#include "upipe/udict_inline.h"
#include "upipe/udict_dump.h"
#include "upipe/uref.h"
#include "upipe/uref_attr.h"
#include "upipe/uref_dump.h"
#include "upipe/uref_std.h"
#include "upipe/uref_clock.h"
#include "upipe/uref_pic.h"
#include "upipe/uref_pic_flow.h"
#include "upipe/uref_pic_flow_formats.h"
#include "upipe/uref_block.h"
#include "upipe/upipe.h"
#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_void.h"
#include "upipe/upipe_helper_output.h"
#include "upump-ev/upump_ev.h"
#include "upipe-x265/upipe_x265.h"
#include "upipe-modules/upipe_file_sink.h"
#include "upipe-modules/upipe_null.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <limits.h>
#include <getopt.h>

#define UDICT_POOL_DEPTH    0
#define UREF_POOL_DEPTH     0
#define UBUF_POOL_DEPTH     0
#define UBUF_PREPEND        0
#define UBUF_APPEND         0
#define UBUF_ALIGN          16
#define UBUF_ALIGN_OFFSET   0
#define UPROBE_LOG_LEVEL    UPROBE_LOG_DEBUG
#define FPS                 25
#define WIDTH               96
#define HEIGHT              64
#define LIMIT               8

UREF_ATTR_UNSIGNED(x265_test, test_id, "x265_test.id", test id);
UREF_ATTR_UNSIGNED(x265_test, counter, "x265_test.counter", frame counter);

static struct umem_mgr *umem_mgr = NULL;
static struct uref_mgr *uref_mgr = NULL;
static struct uprobe *logger = NULL;
static const char *prefix = NULL;
static uint64_t pts = UINT32_MAX;

/** phony pipe to test upipe_x265 */
struct x265_test {
    struct upipe upipe;
    struct upipe *output;
    enum upipe_helper_output_state output_state;
    struct uref *flow_def;
    struct uchain requests;
    unsigned test_id;
    unsigned counter;
};

/** helper phony pipe to test upipe_x265 */
UPIPE_HELPER_UPIPE(x265_test, upipe, 0);
UPIPE_HELPER_VOID(x265_test);
UPIPE_HELPER_OUTPUT(x265_test, output, flow_def, output_state, requests);

/** helper phony pipe */
static struct upipe *test_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe,
                                uint32_t signature, va_list args)
{
    struct upipe *upipe = x265_test_alloc_void(mgr, uprobe, signature, args);
    assert(upipe != NULL);

    x265_test_init_output(upipe);

    struct x265_test *x265_test = x265_test_from_upipe(upipe);
    x265_test->test_id = 0;
    x265_test->counter = 0;
    x265_test->output = NULL;

    upipe_throw_ready(upipe);

    return upipe;
}

/** helper phony pipe */
static void test_input(struct upipe *upipe, struct uref *uref,
                       struct upump **upump_p)
{
    struct x265_test *x265_test = x265_test_from_upipe(upipe);
    uint64_t pts = 0, dts = 0;
    uint64_t test_id = 0;
    uint64_t counter = 0;
    size_t size = 0;

    uref_x265_test_get_test_id(uref, &test_id);
    uref_x265_test_get_counter(uref, &counter);
    uref_block_size(uref, &size);

    uprobe_notice_va(logger, NULL,
                     "Receiving pic %" PRIu64 "-%" PRIu64 ": %zu bytes",
                     test_id, counter, size);

    if (test_id != x265_test->test_id) {
        struct upipe_mgr *output_mgr = NULL;
        if (prefix)
            output_mgr = upipe_fsink_mgr_alloc();
        else
            output_mgr = upipe_null_mgr_alloc();
        assert(output_mgr);

        struct upipe *output = upipe_void_alloc(
            output_mgr,
            uprobe_pfx_alloc_va(uprobe_use(logger), UPROBE_LOG_VERBOSE,
                                "output %" PRIu64, test_id));
        upipe_mgr_release(output_mgr);
        assert(output);

        if (prefix) {
            char path[PATH_MAX];
            sprintf(path, "%s%" PRIu64 ".h265", prefix, test_id);
            upipe_fsink_set_path(output, path, UPIPE_FSINK_OVERWRITE);
        }
        upipe_set_output(upipe, output);
        upipe_release(output);
    }

    if (uref->udict != NULL) {
        udict_dump(uref->udict, upipe->uprobe);
    }
    if (!ubase_check(uref_clock_get_pts_prog(uref, &pts))) {
        upipe_warn(upipe, "received packet with no pts");
    }
    if (!ubase_check(uref_clock_get_dts_prog(uref, &dts))) {
        upipe_warn(upipe, "received packet with no dts");
    }
    upipe_dbg_va(upipe, "received pic %"PRIu64", pts: %.2f ms, dts: %.2f ms",
                 counter, ((int64_t)pts - UINT32_MAX) * 1000. / UCLOCK_FREQ,
                 ((int64_t)dts - UINT32_MAX) * 1000. / UCLOCK_FREQ);


    x265_test->counter = counter;
    x265_test->test_id = test_id;
    x265_test_output(upipe, uref, upump_p);
}

/** helper phony pipe */
static int test_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(x265_test_control_output(upipe, command, args));

    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            uref_dump_notice(flow_def, upipe->uprobe);
            x265_test_store_flow_def(upipe, uref_dup(flow_def));
            return UBASE_ERR_NONE;
        }
        default:
            assert(0);
            return UBASE_ERR_UNHANDLED;
    }
}

/** helper phony pipe */
static void test_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    x265_test_clean_output(upipe);
    x265_test_free_void(upipe);
}

/** helper phony pipe */
static struct upipe_mgr x265_test_mgr = {
    .refcount = NULL,
    .signature = 0,
    .upipe_alloc = test_alloc,
    .upipe_input = test_input,
    .upipe_control = test_control
};


static void fill_pic(struct uref *uref, int counter)
{
    size_t hsize, vsize;
    uint8_t macropixel;
    assert(ubase_check(uref_pic_size(uref, &hsize, &vsize, &macropixel)));

    const char *chroma;
    uref_pic_foreach_plane(uref, chroma) {
        size_t stride;
        uint8_t hsub, vsub, macropixel_size;
        assert(ubase_check(uref_pic_plane_size(uref, chroma, &stride, &hsub, &vsub,
                                   &macropixel_size)));
        int hoctets = hsize * macropixel_size / hsub / macropixel;
        uint8_t *buffer;
        assert(ubase_check(uref_pic_plane_write(uref, chroma, 0, 0, -1, -1, &buffer)));

        for (int y = 0; y < vsize / vsub; y++) {
            for (int x = 0; x < hoctets; x++)
                buffer[x] = 1 + (y * hoctets) + x + counter * 5;
            buffer += stride;
        }
        assert(ubase_check(uref_pic_plane_unmap(uref, chroma, 0, 0, -1, -1)));
    }
}

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

struct test_config {
    const struct uref_pic_flow_format *format;
    struct urational fps;
    size_t width;
    size_t height;
    bool progressive;
    bool fullrange;
};

static void test_config_print(const struct test_config *config)
{
    uprobe_notice_va(logger, NULL,
                     "run config %s %zux%zu%s at %" PRIi64 "/%" PRIu64 " fps",
                     config->format->name, config->width, config->height,
                     config->progressive ? "p" : "i",
                     config->fps.num, config->fps.den);
}

static int test_run(struct upipe *upipe,
                    const struct test_config *config,
                    const struct test_config *config_pic)
{
    static unsigned test_id = 0;
    test_id++;

    test_config_print(config);

    /* send flow definition */
    struct uref *flow_def =
        uref_pic_flow_alloc_format(uref_mgr, config->format);
    assert(flow_def);
    ubase_assert(uref_pic_flow_set_hsize(flow_def, config->width));
    ubase_assert(uref_pic_flow_set_vsize(flow_def, config->height));
    ubase_assert(uref_pic_flow_set_fps(flow_def, config->fps));
    ubase_assert(uref_pic_set_progressive(flow_def, config->progressive));
    if (config->fullrange)
        ubase_assert(uref_pic_flow_set_full_range(flow_def));
    int ret = upipe_set_flow_def(upipe, flow_def);

    if (unlikely(!ubase_check(ret))) {
        uref_free(flow_def);
        return ret;
    }

    struct ubuf_mgr *pic_mgr = ubuf_mem_mgr_alloc_from_flow_def(
        UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, flow_def);
    uref_free(flow_def);
    assert(pic_mgr);

    /* encoding test */
    for (int counter = 0; counter < LIMIT; counter++) {
        uprobe_notice_va(logger, NULL, "Sending pic %u-%d", test_id, counter);
        struct uref *pic = uref_pic_alloc(uref_mgr, pic_mgr, config_pic->width,
                                          config_pic->height);
        assert(pic);
        uref_x265_test_set_test_id(pic, test_id);
        uref_x265_test_set_counter(pic, counter);
        uref_pic_set_progressive(pic, config_pic->progressive);
        fill_pic(pic, counter);
        uref_clock_set_pts_sys(pic, pts);
        uref_clock_set_pts_prog(pic, pts);
        upipe_input(upipe, pic, NULL);
        pts += UCLOCK_FREQ * config_pic->fps.den / config_pic->fps.num;
    }
    ubuf_mgr_release(pic_mgr);
    return UBASE_ERR_NONE;
}

int main(int argc, char **argv)
{
    int c;
    int log_level = UPROBE_LOG_LEVEL;

    printf("Compiled %s %s (%s)\n", __DATE__, __TIME__, __FILE__);

    while ((c = getopt(argc, argv, "vqf:")) != -1) {
        switch (c) {
            case 'v':
                log_level--;
                if (log_level < UPROBE_LOG_VERBOSE)
                    log_level = UPROBE_LOG_VERBOSE;
                break;

            case 'q':
                log_level++;
                if (log_level > UPROBE_LOG_ERROR)
                    log_level = UPROBE_LOG_ERROR;
                break;

            case 'f':
                prefix = optarg;
                break;
        }
    }

    /* upipe env */
    umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr =
        udict_inline_mgr_alloc(UDICT_POOL_DEPTH, umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    assert(uref_mgr != NULL);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc_default(0, 0);


    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    logger = uprobe_stdio_alloc(&uprobe, stdout, log_level);

    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_POOL_DEPTH);
    assert(logger != NULL);
    logger = uprobe_upump_mgr_alloc(logger, upump_mgr);
    upump_mgr_release(upump_mgr);
    assert(logger != NULL);

    struct upipe *x265_test = upipe_void_alloc(
        &x265_test_mgr,
        uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "x265_test"));

    /* x265 manager */
    struct upipe_mgr *upipe_x265_mgr = upipe_x265_mgr_alloc();

    /* x265 pipe */
    struct upipe *x265 = upipe_void_alloc(
        upipe_x265_mgr,
        uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "x265"));
    assert(x265);
    /* x265_test */
    ubase_assert(upipe_set_output(x265, x265_test));
    /* test controls */
    ubase_assert(upipe_x265_set_default_preset(x265, "placebo", "grain"));
    ubase_assert(upipe_x265_set_profile(x265, "main"));
    ubase_assert(upipe_x265_set_default_preset(x265, "faster", NULL));
    ubase_assert(upipe_x265_set_profile(x265, "mainstillpicture"));
    ubase_assert(upipe_x265_set_default(x265, 0));
    ubase_assert(upipe_x265_set_default_preset(x265, "ultrafast", NULL));
    /* disable assembly (not valgrind safe) */
    ubase_assert(upipe_set_option(x265, "asm", "0"));

    /* yuv420p */
    struct test_config config = {
        .format = &uref_pic_flow_format_yuv420p,
        .fps = { .num = FPS, .den = 1 },
        .width = WIDTH,
        .height = HEIGHT,
        .progressive = true,
    };
    struct test_config config_pic = config;
    ubase_assert(upipe_x265_set_profile(x265, "mainstillpicture"));
    ubase_assert(test_run(x265, &config, &config_pic));

    /* double input width/height */
    ubase_assert(upipe_x265_set_profile(x265, "mainstillpicture"));
    config.width = WIDTH * 2;
    config.height = HEIGHT * 2;
    config_pic.width = WIDTH * 2;
    config_pic.height = HEIGHT * 2;
    ubase_assert(test_run(x265, &config, &config_pic));

    /* double input buffer width/height */
    ubase_assert(upipe_x265_set_profile(x265, "mainstillpicture"));
    config.width = WIDTH;
    config.height = HEIGHT;
    config_pic.width = WIDTH * 2;
    config_pic.height = HEIGHT * 2;
    ubase_assert(test_run(x265, &config, &config_pic));

    /* double frame rate */
    ubase_assert(upipe_x265_set_profile(x265, "mainstillpicture"));
    config.fps.num = FPS * 2;
    config_pic.fps.num = FPS * 2;
    config_pic.width = WIDTH;
    config_pic.height = HEIGHT;
    ubase_assert(test_run(x265, &config, &config_pic));

    /* interlaced */
    ubase_assert(upipe_x265_set_profile(x265, "mainstillpicture"));
    config.fps.num = FPS;
    config.progressive = false;
    config_pic.fps.num = FPS;
    config_pic.progressive = false;
    ubase_assert(test_run(x265, &config, &config_pic));

    /* full range */
    ubase_assert(upipe_x265_set_profile(x265, "mainstillpicture"));
    config.progressive = true;
    config.fullrange = true;
    config_pic.progressive = true;
    config_pic.fullrange = true;
    ubase_assert(test_run(x265, &config, &config_pic));

    /* yuv422p */
    ubase_assert(upipe_x265_set_profile(x265, "main422-10"));
    config.format = &uref_pic_flow_format_yuv422p;
    config_pic.format = &uref_pic_flow_format_yuv422p;
    ubase_assert(test_run(x265, &config, &config_pic));

    /* yuv422p10le */
    ubase_assert(upipe_x265_set_profile(x265, "main422-10"));
    config.format = &uref_pic_flow_format_yuv422p10le;
    config_pic.format = &uref_pic_flow_format_yuv422p10le;
    ubase_assert(test_run(x265, &config, &config_pic));

    /* yuv444p */
    ubase_assert(upipe_x265_set_profile(x265, "main444-stillpicture"));
    config.format = &uref_pic_flow_format_yuv444p;
    config_pic.format = &uref_pic_flow_format_yuv444p;
    ubase_assert(test_run(x265, &config, &config_pic));

    /* yuv420p again */
    ubase_assert(upipe_x265_set_profile(x265, "main"));
    config.format = &uref_pic_flow_format_yuv420p;
    config_pic.format = &uref_pic_flow_format_yuv420p;
    ubase_assert(test_run(x265, &config, &config_pic));

    /* release pipes */
    upipe_release(x265);
    test_free(x265_test);

    /* clean everything */
    upipe_mgr_release(upipe_x265_mgr);
    upipe_x265_cleanup();
    uref_mgr_release(uref_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);

    return 0;
}
