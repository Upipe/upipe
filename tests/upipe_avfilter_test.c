/*
 * Copyright (C) 2025 EasyTools
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

/** @file
 * @short unit tests for avfilter pipes
 */

#undef NDEBUG

#include "upipe/umem_alloc.h"
#include "upipe/udict_inline.h"
#include "upipe/uref_std.h"
#include "upipe/uref_pic.h"
#include "upipe/uref_pic_flow.h"
#include "upipe/uref_pic_flow_formats.h"
#include "upipe/uprobe_stdio.h"
#include "upipe/uprobe_prefix.h"
#include "upipe/uprobe_ubuf_mem.h"
#include "upipe/ubuf_pic_mem.h"
#include "upipe-modules/upipe_probe_uref.h"
#include "upipe-modules/upipe_null.h"
#include "upipe-av/uref_avfilter_flow.h"
#include "upipe-av/upipe_av.h"
#include "upipe-av/upipe_avfilter.h"
#include "upump-ev/upump_ev.h"

#include <assert.h>

static const unsigned count = 32;
static unsigned count_output = 0;

static int catch_probe_uref(struct uprobe *uprobe, struct upipe *upipe,
                            int event, va_list args)
{
    struct uref *uref = NULL;
    struct upump **upump_p = NULL;
    bool *drop = NULL;

    if (uprobe_probe_uref_check(event, args, &uref, &upump_p, &drop)) {
        count_output++;
        return UBASE_ERR_NONE;
    }
    return uprobe_throw_next(uprobe, upipe, event, args);
}

static void test_avfilt(struct upump_mgr *upump_mgr, struct uprobe *uprobe,
                        struct uref_mgr *uref_mgr, struct ubuf_mgr *pic_mgr)
{
    struct upipe_mgr *upipe_avfilt_mgr = upipe_avfilt_mgr_alloc();
    assert(upipe_avfilt_mgr);
    struct upipe_mgr *upipe_null_mgr = upipe_null_mgr_alloc();
    assert(upipe_null_mgr);
    struct upipe_mgr *upipe_probe_uref_mgr = upipe_probe_uref_mgr_alloc();
    assert(upipe_probe_uref_mgr);

    struct upipe *upipe_avfilt = upipe_void_alloc(
        upipe_avfilt_mgr,
        uprobe_pfx_alloc(uprobe_use(uprobe), UPROBE_LOG_VERBOSE, "avfilt"));
    assert(upipe_avfilt);

    struct upipe *upipe_probe_uref = upipe_void_alloc_output(
        upipe_avfilt, upipe_probe_uref_mgr,
        uprobe_pfx_alloc(uprobe_alloc(catch_probe_uref, uprobe_use(uprobe)),
                         UPROBE_LOG_VERBOSE, "probe"));
    assert(upipe_probe_uref);

    struct upipe *upipe_null = upipe_void_alloc_output(
        upipe_probe_uref, upipe_null_mgr,
        uprobe_pfx_alloc(uprobe_use(uprobe), UPROBE_LOG_VERBOSE,  "null"));
    assert(upipe_null);

    upipe_avfilt_set_filters_desc(upipe_avfilt, "copy");

    {
        /* set pic flow def */
        struct uref *flow_def = uref_pic_flow_alloc_yuv420p(uref_mgr);
        assert(flow_def);
        ubase_assert(uref_pic_flow_set_hsize(flow_def, 32));
        ubase_assert(uref_pic_flow_set_vsize(flow_def, 32));
        upipe_set_flow_def(upipe_avfilt, flow_def);
        uref_free(flow_def);
    }

    for (unsigned i = 0; i < count; i++) {
        /* valid input */
        struct uref *uref = uref_pic_alloc(uref_mgr, pic_mgr, 32, 32);
        assert(uref);
        upipe_input(upipe_avfilt, uref, NULL);
    }

    upump_mgr_run(upump_mgr, NULL);

    assert(count_output == count);

    upipe_release(upipe_null);
    upipe_release(upipe_probe_uref);
    upipe_release(upipe_avfilt);
    upipe_mgr_release(upipe_null_mgr);
    upipe_mgr_release(upipe_probe_uref_mgr);
    upipe_mgr_release(upipe_avfilt_mgr);
}

static void test_avfilt_sub(struct upump_mgr *upump_mgr, struct uprobe *uprobe,
                            struct uref_mgr *uref_mgr, struct ubuf_mgr *pic_mgr)
{
    struct upipe_mgr *upipe_avfilt_mgr = upipe_avfilt_mgr_alloc();
    assert(upipe_avfilt_mgr);
    struct upipe_mgr *upipe_null_mgr = upipe_null_mgr_alloc();
    assert(upipe_null_mgr);
    struct upipe_mgr *upipe_probe_uref_mgr = upipe_probe_uref_mgr_alloc();
    assert(upipe_probe_uref_mgr);

    struct upipe *upipe_avfilt = upipe_void_alloc(
        upipe_avfilt_mgr,
        uprobe_pfx_alloc(uprobe_use(uprobe), UPROBE_LOG_VERBOSE, "avfilt"));
    assert(upipe_avfilt);

    struct uref *flow_def_in = uref_avfilt_flow_alloc_def(uref_mgr, "in");
    assert(flow_def_in);
    struct upipe *upipe_avfilt_in = upipe_flow_alloc_sub(
        upipe_avfilt,
        uprobe_pfx_alloc(uprobe_use(uprobe), UPROBE_LOG_VERBOSE, "in"),
        flow_def_in);
    assert(upipe_avfilt_in);
    uref_free(flow_def_in);

    struct uref *flow_def_out = uref_avfilt_flow_alloc_def(uref_mgr, "out");
    assert(flow_def_out);
    struct upipe *upipe_avfilt_out = upipe_flow_alloc_sub(
        upipe_avfilt,
        uprobe_pfx_alloc(uprobe_use(uprobe), UPROBE_LOG_VERBOSE, "out"),
        flow_def_out);
    assert(upipe_avfilt_out);
    uref_free(flow_def_out);

    struct upipe *upipe_probe_uref = upipe_void_alloc_output(
        upipe_avfilt_out, upipe_probe_uref_mgr,
        uprobe_pfx_alloc(uprobe_alloc(catch_probe_uref, uprobe_use(uprobe)),
                         UPROBE_LOG_VERBOSE, "probe"));
    assert(upipe_probe_uref);

    struct upipe *upipe_null = upipe_void_alloc_output(
        upipe_probe_uref, upipe_null_mgr,
        uprobe_pfx_alloc(uprobe_use(uprobe), UPROBE_LOG_VERBOSE,  "null"));
    assert(upipe_null);

    upipe_avfilt_set_filters_desc(upipe_avfilt, "[in] copy [out]");

    {
        /* set pic flow def */
        struct uref *flow_def = uref_pic_flow_alloc_yuv420p(uref_mgr);
        assert(flow_def);
        ubase_assert(uref_pic_flow_set_hsize(flow_def, 32));
        ubase_assert(uref_pic_flow_set_vsize(flow_def, 32));
        upipe_set_flow_def(upipe_avfilt, flow_def);
        uref_free(flow_def);
    }

    for (unsigned i = 0; i < count; i++) {
        /* valid input */
        struct uref *uref = uref_pic_alloc(uref_mgr, pic_mgr, 32, 32);
        assert(uref);
        upipe_input(upipe_avfilt, uref, NULL);
    }

    upump_mgr_run(upump_mgr, NULL);

    assert(count_output == count);

    upipe_release(upipe_null);
    upipe_release(upipe_probe_uref);
    upipe_release(upipe_avfilt_in);
    upipe_release(upipe_avfilt_out);
    upipe_release(upipe_avfilt);
    upipe_mgr_release(upipe_null_mgr);
    upipe_mgr_release(upipe_probe_uref_mgr);
    upipe_mgr_release(upipe_avfilt_mgr);
}

int main(int argc, char *argv[])
{
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(0, umem_mgr, -1, -1);
    assert(udict_mgr);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(0, udict_mgr, 0);
    assert(uref_mgr);
    struct ubuf_mgr *pic_mgr =
        ubuf_pic_mem_mgr_alloc_fourcc(0, 0, umem_mgr, "I420", 0, 0, 0, 0, 0, 0);
    assert(pic_mgr);

    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc_default(0, 0);
    assert(upump_mgr);

    struct uprobe *uprobe =
        uprobe_stdio_alloc(NULL, stderr, UPROBE_LOG_VERBOSE);
    assert(uprobe);
    uprobe = uprobe_ubuf_mem_alloc(uprobe, umem_mgr, 0, 0);
    assert(uprobe);

    assert(upipe_av_init(false, uprobe_use(uprobe)));

    test_avfilt(upump_mgr, uprobe, uref_mgr, pic_mgr);
    test_avfilt_sub(upump_mgr, uprobe, uref_mgr, pic_mgr);

    upipe_av_clean();

    uprobe_release(uprobe);

    upump_mgr_release(upump_mgr);
    ubuf_mgr_release(pic_mgr);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);

    return 0;
}
