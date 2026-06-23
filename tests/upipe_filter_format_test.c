/*
 * Copyright (C) 2026 EasyTools
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short unit tests for filter format pipe
 */

#undef NDEBUG

#include "config.h"
#include "upipe/umem_alloc.h"
#include "upipe/udict_inline.h"
#include "upipe/uref_std.h"
#include "upipe/uref_pic.h"
#include "upipe/uref_pic_flow.h"
#include "upipe/uref_pic_flow_formats.h"
#include "upipe/uref_dump.h"
#include "upipe/uprobe_stdio.h"
#include "upipe/uprobe_prefix.h"
#include "upipe/uprobe_ubuf_mem.h"
#include "upipe/ubuf_mem.h"
#include "upipe/ubuf_pic_mem.h"
#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_flow.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe-modules/upipe_setscan.h"
#include "upipe-modules/upipe_interlace.h"
#include "upipe-filters/upipe_filter_blend.h"
#include "upipe-filters/upipe_filter_format.h"
#ifdef HAVE_LIBSWSCALE
#include "upipe-swscale/upipe_sws.h"
#endif
#ifdef HAVE_LIBAVFILTER
#include "upipe-av/upipe_avfilter.h"
#endif

#include <assert.h>
#include <unistd.h>

#define NB_FRAMES 10

UREF_ATTR_VOID(test, input, "test.in", input test attribute)
UREF_ATTR_VOID(test, output, "test.out", output test attribute)
UREF_ATTR_VOID(test, wanted, "test.want", wanted test attibutes)
UREF_ATTR_UNSIGNED(test, count, "test.count", expected number of frames)

static struct umem_mgr *umem_mgr = NULL;
static struct uref_mgr *uref_mgr = NULL;
static struct uprobe *logger = NULL;
static struct upipe_mgr *sws_mgr = NULL;
static struct upipe_mgr *avfilt_mgr = NULL;
static struct upipe_mgr *deint_mgr = NULL;
static struct upipe_mgr *interlace_mgr = NULL;
static struct upipe_mgr *setscan_mgr = NULL;

static void test_passthrough(void);
static void test_deint(void);
static void test_interlace(void);
static void test_interlace_tff(void);
static void test_interlace_bff(void);
static void test_scale(void);
static void test_format(void);
static void test_scale_format(void);
static void test_reconfigure(void);

typedef void (*test_func)(void);

struct test {
    const char *name;
    test_func run;
};

static const struct test tests[] = {
    { "passthrough",    test_passthrough },
    { "deint",          test_deint },
    { "interlace",      test_interlace },
    { "interlace tff",  test_interlace_tff },
    { "interlace bff",  test_interlace_bff },
    { "scale",          test_scale },
    { "format",         test_format },
    { "scale_format",   test_scale_format },
    { "reconfigure",    test_reconfigure },
};
struct sink {
    struct urefcount urefcount;
    struct upipe upipe;
    struct uref *flow_def;
    uint64_t hsize;
    uint64_t vsize;
    uint64_t count;
    uint64_t expected;
};

#define SINK_SIGNATURE UBASE_FOURCC('s','i','n','k')

UPIPE_HELPER_UPIPE(sink, upipe, SINK_SIGNATURE)
UPIPE_HELPER_FLOW(sink, NULL)
UPIPE_HELPER_UREFCOUNT(sink, urefcount, sink_free)

static void sink_free(struct upipe *upipe)
{
    struct sink *sink = sink_from_upipe(upipe);

    upipe_throw_dead(upipe);

    assert(sink->count == sink->expected);

    uref_free(sink->flow_def);
    sink_clean_urefcount(upipe);
    sink_free_flow(upipe);
}

static struct upipe *sink_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe,
                                uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe =
        sink_alloc_flow(mgr, uprobe, signature, args, &flow_def);
    assert(flow_def && upipe);
    struct sink *sink = sink_from_upipe(upipe);
    sink_init_urefcount(upipe);
    ubase_assert(uref_pic_flow_get_hsize(flow_def, &sink->hsize));
    ubase_assert(uref_pic_flow_get_vsize(flow_def, &sink->vsize));
    sink->flow_def = flow_def;
    sink->count = 0;
    ubase_assert(uref_test_get_count(flow_def, &sink->expected));

    upipe_throw_ready(upipe);

    return upipe;
}

static void sink_input(struct upipe *upipe, struct uref *uref,
                       struct upump **upump_p)
{
    struct sink *sink = sink_from_upipe(upipe);
    size_t hsize = 0, vsize = 0;
    uref_pic_size(uref, &hsize, &vsize, NULL);
    upipe_info_va(upipe, "received buffer %" PRIu64 "/%" PRIu64,
                  sink->count + 1, sink->expected);
    assert(hsize == sink->hsize && vsize == sink->vsize);
    sink->count++;
    uref_free(uref);
}

static int sink_control(struct upipe *upipe, int cmd, va_list args)
{
    struct sink *sink = sink_from_upipe(upipe);

    switch (cmd) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            upipe_info(upipe, "output flow");
            uref_dump_info(flow_def, upipe->uprobe);
            assert(uref_pic_flow_compare_format(flow_def, sink->flow_def));
            assert(!uref_pic_flow_cmp_hsize(flow_def, sink->flow_def));
            assert(!uref_pic_flow_cmp_vsize(flow_def, sink->flow_def));
            assert(uref_pic_check_progressive(flow_def) ==
                   uref_pic_check_progressive(sink->flow_def));
            ubase_assert(uref_test_get_input(flow_def));
            ubase_assert(uref_test_get_output(flow_def));
            ubase_assert(uref_test_get_wanted(flow_def));
            return UBASE_ERR_NONE;
        }
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *urequest = va_arg(args, struct urequest *);
            if (urequest->type != UREQUEST_FLOW_FORMAT)
                return upipe_throw_provide_request(upipe, urequest);

            struct uref *flow_def = uref_dup(sink->flow_def);
            assert(flow_def);
            ubase_assert(uref_test_set_output(flow_def));
            return urequest_provide_flow_format(urequest, flow_def);
        }
        case UPIPE_UNREGISTER_REQUEST:
            return UBASE_ERR_NONE;
    }
    return UBASE_ERR_UNHANDLED;
}

static struct upipe_mgr sink_mgr = {
    .signature = SINK_SIGNATURE,
    .upipe_alloc = sink_alloc,
    .upipe_input = sink_input,
    .upipe_control = sink_control,
};

static struct upipe *build_pipeline(struct upipe_mgr *avfilt,
                                    struct upipe_mgr *deint,
                                    struct upipe_mgr *interlace,
                                    struct upipe_mgr *sws,
                                    struct uref *flow_def_wanted)
{
    struct upipe_mgr *ffmt_mgr = upipe_ffmt_mgr_alloc();
    assert(ffmt_mgr);
    upipe_ffmt_mgr_set_avfilter_mgr(ffmt_mgr, avfilt);
    upipe_ffmt_mgr_set_deint_mgr(ffmt_mgr, deint);
    upipe_ffmt_mgr_set_interlace_mgr(ffmt_mgr, interlace);
    upipe_ffmt_mgr_set_sws_mgr(ffmt_mgr, sws);

    ubase_assert(uref_test_set_wanted(flow_def_wanted));

    struct upipe *ffmt = upipe_flow_alloc(
        ffmt_mgr,
        uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_VERBOSE, "ffmt"),
        flow_def_wanted);
    assert(ffmt);

    struct upipe *sink = upipe_flow_alloc_output(
        ffmt, &sink_mgr,
        uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_VERBOSE, "sink"),
        flow_def_wanted);
    assert(sink);
    upipe_release(sink);
    upipe_mgr_release(ffmt_mgr);

    return ffmt;
}

static void send_frames(struct upipe *upipe, struct uref *flow_def)
{
    assert(flow_def);

    uint64_t hsize = 0, vsize = 0;
    ubase_assert(uref_pic_flow_get_hsize(flow_def, &hsize));
    ubase_assert(uref_pic_flow_get_vsize(flow_def, &vsize));
    ubase_assert(uref_test_set_input(flow_def));
    ubase_assert(upipe_set_flow_def(upipe, flow_def));

    struct uref_mgr *uref_mgr = flow_def->mgr;
    struct ubuf_mgr *ubuf_mgr =
        ubuf_mem_mgr_alloc_from_flow_def(0, 0, umem_mgr, flow_def);
    assert(ubuf_mgr);

    for (unsigned i = 0; i < NB_FRAMES; i++) {
        struct uref *uref = uref_pic_alloc(uref_mgr, ubuf_mgr, hsize, vsize);
        assert(uref);
        uref_pic_clear(uref, 0, 0, hsize, vsize, 0);
        upipe_info_va(upipe, "sending frame %u/%u", i + 1, NB_FRAMES);
        upipe_input(upipe, uref, NULL);
    }

    ubuf_mgr_release(ubuf_mgr);
}

/* same format and size → setflowdef path, no conversion */
static void test_passthrough(void)
{
    struct uref *flow_def_wanted = uref_pic_flow_alloc_yuv420p(uref_mgr);
    assert(flow_def_wanted);
    ubase_assert(uref_pic_flow_set_hsize(flow_def_wanted, 32));
    ubase_assert(uref_pic_flow_set_vsize(flow_def_wanted, 32));
    ubase_assert(uref_test_set_count(flow_def_wanted, NB_FRAMES));
    struct upipe *ffmt =
        build_pipeline(NULL, setscan_mgr, setscan_mgr, NULL, flow_def_wanted);
    uref_free(flow_def_wanted);

    struct uref *flow_def = uref_pic_flow_alloc_yuv420p(uref_mgr);
    assert(flow_def);
    ubase_assert(uref_pic_flow_set_hsize(flow_def, 32));
    ubase_assert(uref_pic_flow_set_vsize(flow_def, 32));
    send_frames(ffmt, flow_def);
    uref_free(flow_def);
    upipe_release(ffmt);
}

/* deinterlace only */
static void test_deint_run(struct upipe_mgr *avfilt_mgr,
                           struct upipe_mgr *deint_mgr,
                           struct upipe_mgr *interlace_mgr,
                           struct upipe_mgr *sws_mgr)
{
    struct uref *flow_def_wanted = uref_pic_flow_alloc_yuv420p(uref_mgr);
    assert(flow_def_wanted);
    ubase_assert(uref_pic_flow_set_hsize(flow_def_wanted, 32));
    ubase_assert(uref_pic_flow_set_vsize(flow_def_wanted, 32));
    ubase_assert(uref_pic_set_progressive(flow_def_wanted, true));
    ubase_assert(uref_test_set_count(flow_def_wanted, NB_FRAMES));
    struct upipe *ffmt = build_pipeline(avfilt_mgr, deint_mgr, interlace_mgr,
                                        sws_mgr, flow_def_wanted);
    uref_free(flow_def_wanted);

    struct uref *flow_def = uref_pic_flow_alloc_yuv420p(uref_mgr);
    assert(flow_def);
    ubase_assert(uref_pic_flow_set_hsize(flow_def, 32));
    ubase_assert(uref_pic_flow_set_vsize(flow_def, 32));
    send_frames(ffmt, flow_def);
    uref_free(flow_def);
    upipe_release(ffmt);
}

static void test_deint(void)
{
    if (avfilt_mgr)
        test_deint_run(avfilt_mgr, NULL, NULL, NULL);
    test_deint_run(NULL, deint_mgr, NULL, NULL);
    test_deint_run(NULL, setscan_mgr, NULL, NULL);
}

/* interlace top field first */
static void test_interlace_run(struct upipe_mgr *avfilt_mgr,
                               struct upipe_mgr *deint_mgr,
                               struct upipe_mgr *interlace_mgr,
                               struct upipe_mgr *sws_mgr)
{
    unsigned nb_frames = 2 * NB_FRAMES + NB_FRAMES / 2;
    if (interlace_mgr == setscan_mgr)
        nb_frames = 3 * NB_FRAMES;

    struct uref *flow_def_wanted = uref_pic_flow_alloc_yuv420p(uref_mgr);
    assert(flow_def_wanted);
    ubase_assert(uref_pic_flow_set_hsize(flow_def_wanted, 32));
    ubase_assert(uref_pic_flow_set_vsize(flow_def_wanted, 32));
    ubase_assert(uref_pic_set_progressive(flow_def_wanted, false));
    ubase_assert(uref_test_set_count(flow_def_wanted, nb_frames));
    struct upipe *ffmt = build_pipeline(avfilt_mgr, deint_mgr, interlace_mgr,
                                        sws_mgr, flow_def_wanted);
    uref_free(flow_def_wanted);

    /* progressive input */
    struct uref *flow_def = uref_pic_flow_alloc_yuv420p(uref_mgr);
    assert(flow_def);
    ubase_assert(uref_pic_flow_set_hsize(flow_def, 32));
    ubase_assert(uref_pic_flow_set_vsize(flow_def, 32));
    ubase_assert(uref_pic_set_progressive(flow_def, true));
    send_frames(ffmt, flow_def);
    uref_free(flow_def);

    /** tff input */
    flow_def = uref_pic_flow_alloc_yuv420p(uref_mgr);
    assert(flow_def);
    ubase_assert(uref_pic_flow_set_hsize(flow_def, 32));
    ubase_assert(uref_pic_flow_set_vsize(flow_def, 32));
    ubase_assert(uref_pic_set_progressive(flow_def, false));
    ubase_assert(uref_pic_set_tff(flow_def, true));
    send_frames(ffmt, flow_def);
    uref_free(flow_def);

    /** bff input */
    flow_def = uref_pic_flow_alloc_yuv420p(uref_mgr);
    assert(flow_def);
    ubase_assert(uref_pic_flow_set_hsize(flow_def, 32));
    ubase_assert(uref_pic_flow_set_vsize(flow_def, 32));
    ubase_assert(uref_pic_set_progressive(flow_def, false));
    ubase_assert(uref_pic_set_tff(flow_def, false));
    send_frames(ffmt, flow_def);
    uref_free(flow_def);
    upipe_release(ffmt);
}

static void test_interlace(void)
{
    if (avfilt_mgr)
        test_interlace_run(avfilt_mgr, NULL, NULL, NULL);
    test_interlace_run(NULL, NULL, interlace_mgr, NULL);
    test_interlace_run(NULL, NULL, setscan_mgr, NULL);
}

/* interlace top field first */
static void test_interlace_tff_run(struct upipe_mgr *avfilt_mgr,
                                   struct upipe_mgr *deint_mgr,
                                   struct upipe_mgr *interlace_mgr,
                                   struct upipe_mgr *sws_mgr)
{
    unsigned nb_frames = 2 * NB_FRAMES;
    if (interlace_mgr == setscan_mgr)
        nb_frames = 3 * NB_FRAMES;

    struct uref *flow_def_wanted = uref_pic_flow_alloc_yuv420p(uref_mgr);
    assert(flow_def_wanted);
    ubase_assert(uref_pic_flow_set_hsize(flow_def_wanted, 32));
    ubase_assert(uref_pic_flow_set_vsize(flow_def_wanted, 32));
    ubase_assert(uref_pic_set_progressive(flow_def_wanted, false));
    ubase_assert(uref_pic_set_tff(flow_def_wanted, true));
    ubase_assert(uref_test_set_count(flow_def_wanted, nb_frames));
    struct upipe *ffmt = build_pipeline(avfilt_mgr, deint_mgr, interlace_mgr,
                                        sws_mgr, flow_def_wanted);
    uref_free(flow_def_wanted);

    /* progressive input */
    struct uref *flow_def = uref_pic_flow_alloc_yuv420p(uref_mgr);
    assert(flow_def);
    ubase_assert(uref_pic_flow_set_hsize(flow_def, 32));
    ubase_assert(uref_pic_flow_set_vsize(flow_def, 32));
    ubase_assert(uref_pic_set_progressive(flow_def, true));
    send_frames(ffmt, flow_def);
    uref_free(flow_def);

    /** tff input */
    flow_def = uref_pic_flow_alloc_yuv420p(uref_mgr);
    assert(flow_def);
    ubase_assert(uref_pic_flow_set_hsize(flow_def, 32));
    ubase_assert(uref_pic_flow_set_vsize(flow_def, 32));
    ubase_assert(uref_pic_set_progressive(flow_def, false));
    ubase_assert(uref_pic_set_tff(flow_def, true));
    send_frames(ffmt, flow_def);
    uref_free(flow_def);

    /** bff input */
    flow_def = uref_pic_flow_alloc_yuv420p(uref_mgr);
    assert(flow_def);
    ubase_assert(uref_pic_flow_set_hsize(flow_def, 32));
    ubase_assert(uref_pic_flow_set_vsize(flow_def, 32));
    ubase_assert(uref_pic_set_progressive(flow_def, false));
    ubase_assert(uref_pic_set_tff(flow_def, false));
    send_frames(ffmt, flow_def);
    uref_free(flow_def);
    upipe_release(ffmt);
}

static void test_interlace_tff(void)
{
    if (avfilt_mgr)
        test_interlace_tff_run(avfilt_mgr, NULL, NULL, NULL);
    test_interlace_tff_run(NULL, deint_mgr, interlace_mgr, NULL);
    test_interlace_tff_run(NULL, setscan_mgr, setscan_mgr, NULL);
}

/* interlace bottom field first */
static void test_interlace_bff_run(struct upipe_mgr *avfilt_mgr,
                                   struct upipe_mgr *deint_mgr,
                                   struct upipe_mgr *interlace_mgr,
                                   struct upipe_mgr *sws_mgr)
{
    unsigned nb_frames = 2 * NB_FRAMES;
    if (interlace_mgr == setscan_mgr)
        nb_frames = 3 * NB_FRAMES;

    struct uref *flow_def_wanted = uref_pic_flow_alloc_yuv420p(uref_mgr);
    assert(flow_def_wanted);
    ubase_assert(uref_pic_flow_set_hsize(flow_def_wanted, 32));
    ubase_assert(uref_pic_flow_set_vsize(flow_def_wanted, 32));
    ubase_assert(uref_pic_set_progressive(flow_def_wanted, false));
    ubase_assert(uref_pic_set_tff(flow_def_wanted, false));
    ubase_assert(uref_test_set_count(flow_def_wanted, nb_frames));
    struct upipe *ffmt = build_pipeline(avfilt_mgr, deint_mgr, interlace_mgr,
                                        sws_mgr, flow_def_wanted);
    uref_free(flow_def_wanted);

    /* progressive input */
    struct uref *flow_def = uref_pic_flow_alloc_yuv420p(uref_mgr);
    assert(flow_def);
    ubase_assert(uref_pic_flow_set_hsize(flow_def, 32));
    ubase_assert(uref_pic_flow_set_vsize(flow_def, 32));
    ubase_assert(uref_pic_set_progressive(flow_def, true));
    send_frames(ffmt, flow_def);
    uref_free(flow_def);

    /** tff input */
    flow_def = uref_pic_flow_alloc_yuv420p(uref_mgr);
    assert(flow_def);
    ubase_assert(uref_pic_flow_set_hsize(flow_def, 32));
    ubase_assert(uref_pic_flow_set_vsize(flow_def, 32));
    ubase_assert(uref_pic_set_progressive(flow_def, false));
    ubase_assert(uref_pic_set_tff(flow_def, true));
    send_frames(ffmt, flow_def);
    uref_free(flow_def);

    /** bff input */
    flow_def = uref_pic_flow_alloc_yuv420p(uref_mgr);
    assert(flow_def);
    ubase_assert(uref_pic_flow_set_hsize(flow_def, 32));
    ubase_assert(uref_pic_flow_set_vsize(flow_def, 32));
    ubase_assert(uref_pic_set_progressive(flow_def, false));
    ubase_assert(uref_pic_set_tff(flow_def, false));
    send_frames(ffmt, flow_def);
    uref_free(flow_def);
    upipe_release(ffmt);
}

static void test_interlace_bff(void)
{
    if (avfilt_mgr)
        test_interlace_bff_run(avfilt_mgr, NULL, NULL, NULL);
    test_interlace_bff_run(NULL, deint_mgr, interlace_mgr, NULL);
    test_interlace_bff_run(NULL, setscan_mgr, setscan_mgr, NULL);
}

/* same format, different size → sws scale */
static void test_scale_run(struct upipe_mgr *avfilt_mgr,
                           struct upipe_mgr *deint_mgr,
                           struct upipe_mgr *interlace_mgr,
                           struct upipe_mgr *sws_mgr)
{
    struct uref *flow_def_wanted = uref_pic_flow_alloc_yuv420p(uref_mgr);
    assert(flow_def_wanted);
    ubase_assert(uref_pic_flow_set_hsize(flow_def_wanted, 16));
    ubase_assert(uref_pic_flow_set_vsize(flow_def_wanted, 16));
    ubase_assert(uref_test_set_count(flow_def_wanted, NB_FRAMES));
    struct upipe *ffmt = build_pipeline(avfilt_mgr, deint_mgr, interlace_mgr,
                                        sws_mgr, flow_def_wanted);
    uref_free(flow_def_wanted);

    struct uref *flow_def = uref_pic_flow_alloc_yuv420p(uref_mgr);
    assert(flow_def);
    ubase_assert(uref_pic_flow_set_hsize(flow_def, 32));
    ubase_assert(uref_pic_flow_set_vsize(flow_def, 32));
    send_frames(ffmt, flow_def);
    uref_free(flow_def);
    upipe_release(ffmt);
}

static void test_scale(void)
{
    if (avfilt_mgr)
        test_scale_run(avfilt_mgr, NULL, NULL, NULL);
    if (sws_mgr)
        test_scale_run(NULL, NULL, NULL, sws_mgr);
}

/* different format, same size → sws format conversion */
static void test_format_run(struct upipe_mgr *avfilt_mgr,
                            struct upipe_mgr *deint_mgr,
                            struct upipe_mgr *interlace_mgr,
                            struct upipe_mgr *sws_mgr)
{
    struct uref *flow_def_wanted = uref_pic_flow_alloc_nv12(uref_mgr);
    assert(flow_def_wanted);
    ubase_assert(uref_pic_flow_set_hsize(flow_def_wanted, 32));
    ubase_assert(uref_pic_flow_set_vsize(flow_def_wanted, 32));
    ubase_assert(uref_test_set_count(flow_def_wanted, NB_FRAMES));
    struct upipe *ffmt = build_pipeline(avfilt_mgr, deint_mgr, interlace_mgr,
                                        sws_mgr, flow_def_wanted);
    uref_free(flow_def_wanted);

    struct uref *flow_def = uref_pic_flow_alloc_yuv420p(uref_mgr);
    assert(flow_def);
    ubase_assert(uref_pic_flow_set_hsize(flow_def, 32));
    ubase_assert(uref_pic_flow_set_vsize(flow_def, 32));
    send_frames(ffmt, flow_def);
    uref_free(flow_def);
    upipe_release(ffmt);
}

static void test_format(void)
{
    if (avfilt_mgr)
        test_format_run(avfilt_mgr, NULL, NULL, NULL);
    if (sws_mgr)
        test_format_run(NULL, NULL, NULL, sws_mgr);
}

/* different format and different size → sws scale + format conversion */
static void test_scale_format_run(struct upipe_mgr *avfilt_mgr,
                                  struct upipe_mgr *deint_mgr,
                                  struct upipe_mgr *interlace_mgr,
                                  struct upipe_mgr *sws_mgr)
{
    struct uref *flow_def_wanted = uref_pic_flow_alloc_nv12(uref_mgr);
    assert(flow_def_wanted);
    ubase_assert(uref_pic_flow_set_hsize(flow_def_wanted, 16));
    ubase_assert(uref_pic_flow_set_vsize(flow_def_wanted, 16));
    ubase_assert(uref_test_set_count(flow_def_wanted, NB_FRAMES));
    struct upipe *ffmt = build_pipeline(avfilt_mgr, deint_mgr, interlace_mgr,
                                        sws_mgr, flow_def_wanted);
    uref_free(flow_def_wanted);

    struct uref *flow_def = uref_pic_flow_alloc_yuv420p(uref_mgr);
    assert(flow_def);
    ubase_assert(uref_pic_flow_set_hsize(flow_def, 32));
    ubase_assert(uref_pic_flow_set_vsize(flow_def, 32));
    send_frames(ffmt, flow_def);
    uref_free(flow_def);
    upipe_release(ffmt);
}

static void test_scale_format(void)
{
    if (avfilt_mgr)
        test_scale_format_run(avfilt_mgr, NULL, NULL, NULL);
    if (sws_mgr)
        test_scale_format_run(NULL, NULL, NULL, sws_mgr);
}

/* input flow def change mid-stream triggers reconfiguration */
static void test_reconfigure_run(struct upipe_mgr *avfilt_mgr,
                                 struct upipe_mgr *deint_mgr,
                                 struct upipe_mgr *interlace_mgr,
                                 struct upipe_mgr *sws_mgr)
{
    struct uref *flow_def_wanted = uref_pic_flow_alloc_yuv420p(uref_mgr);
    assert(flow_def_wanted);
    ubase_assert(uref_pic_flow_set_hsize(flow_def_wanted, 32));
    ubase_assert(uref_pic_flow_set_vsize(flow_def_wanted, 32));
    ubase_assert(uref_test_set_count(flow_def_wanted, 4 * NB_FRAMES));
    struct upipe *ffmt = build_pipeline(avfilt_mgr, deint_mgr, interlace_mgr,
                                        sws_mgr, flow_def_wanted);
    uref_free(flow_def_wanted);

    /* first config: 32x32 input */
    struct uref *flow_def = uref_pic_flow_alloc_yuv420p(uref_mgr);
    assert(flow_def);
    ubase_assert(uref_pic_flow_set_hsize(flow_def, 32));
    ubase_assert(uref_pic_flow_set_vsize(flow_def, 32));
    send_frames(ffmt, flow_def);
    uref_free(flow_def);

    /* reconfigure: same 32x32 input (different uref, same content → no-op)
     */
    flow_def = uref_pic_flow_alloc_yuv420p(uref_mgr);
    assert(flow_def);
    ubase_assert(uref_pic_flow_set_hsize(flow_def, 32));
    ubase_assert(uref_pic_flow_set_vsize(flow_def, 32));
    send_frames(ffmt, flow_def);
    uref_free(flow_def);

    /* reconfigure: use scaling 16x16 → 32x32 */
    flow_def = uref_pic_flow_alloc_yuv420p(uref_mgr);
    assert(flow_def);
    ubase_assert(uref_pic_flow_set_hsize(flow_def, 16));
    ubase_assert(uref_pic_flow_set_vsize(flow_def, 16));
    send_frames(ffmt, flow_def);
    uref_free(flow_def);

    /* reconfigure: same 32x32 input (different uref, same content → no-op)
     */
    flow_def = uref_pic_flow_alloc_yuv420p(uref_mgr);
    assert(flow_def);
    ubase_assert(uref_pic_flow_set_hsize(flow_def, 32));
    ubase_assert(uref_pic_flow_set_vsize(flow_def, 32));
    send_frames(ffmt, flow_def);
    uref_free(flow_def);

    upipe_release(ffmt);
}

static void test_reconfigure(void)
{
    if (avfilt_mgr)
        test_reconfigure_run(avfilt_mgr, NULL, NULL, NULL);
    if (sws_mgr)
        test_reconfigure_run(NULL, NULL, NULL, sws_mgr);
}

int main(int argc, char *argv[])
{
    int verbose = 4;
    int opt;
    while ((opt = getopt(argc, argv, "vq")) != -1) {
        switch (opt) {
            case 'v':
                verbose++;
                break;
            case 'q':
                verbose--;
                break;
        }
    }
    const enum uprobe_log_level log_levels[] = {
        UPROBE_LOG_ERROR,
        UPROBE_LOG_WARNING,
        UPROBE_LOG_NOTICE,
        UPROBE_LOG_INFO,
        UPROBE_LOG_DEBUG,
        UPROBE_LOG_VERBOSE,
    };
    if (verbose < 0)
        verbose = 0;
    else if (verbose >= UBASE_ARRAY_SIZE(log_levels))
        verbose = UBASE_ARRAY_SIZE(log_levels) - 1;
    enum uprobe_log_level log_level = log_levels[verbose];

    umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(0, umem_mgr, -1, -1);
    assert(udict_mgr);
    uref_mgr = uref_std_mgr_alloc(0, udict_mgr, 0);
    assert(uref_mgr);

    logger = uprobe_stdio_alloc(NULL, stderr, log_level);
    assert(logger);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, 0, 0);
    assert(logger);

#ifdef HAVE_LIBSWSCALE
    sws_mgr = upipe_sws_mgr_alloc();
    assert(sws_mgr);
#endif
#ifdef HAVE_LIBAVFILTER
    avfilt_mgr = upipe_avfilt_mgr_alloc();
    assert(avfilt_mgr);
#endif
    deint_mgr = upipe_filter_blend_mgr_alloc();
    assert(deint_mgr);
    interlace_mgr = upipe_interlace_mgr_alloc();
    assert(interlace_mgr);
    setscan_mgr = upipe_setscan_mgr_alloc();
    assert(setscan_mgr);

    for (unsigned i = 0; i < UBASE_ARRAY_SIZE(tests); i++) {
        uprobe_notice_va(logger, NULL, "start %s", tests[i].name);
        tests[i].run();
    }

    upipe_mgr_release(setscan_mgr);
    upipe_mgr_release(interlace_mgr);
    upipe_mgr_release(deint_mgr);
    upipe_mgr_release(avfilt_mgr);
    upipe_mgr_release(sws_mgr);
    uprobe_release(logger);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);

    return 0;
}
