/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
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

#include <upipe-modules/upipe_segment_source.h>
#include <upipe-modules/upipe_burst.h>
#include <upipe-modules/upipe_probe_uref.h>
#include <upipe-modules/upipe_auto_source.h>

#include <upipe/upipe_helper_uclock.h>
#include <upipe/upipe_helper_uprobe.h>
#include <upipe/upipe_helper_bin_output.h>
#include <upipe/upipe_helper_inner.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_urefcount_real.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_upipe.h>

#include <upipe/uref_block.h>

#include <upipe/uprobe_prefix.h>

struct upipe_seg_src {
    struct upipe upipe;
    struct urefcount urefcount;
    struct urefcount urefcount_real;
    struct uchain requests;

    struct urequest request_uclock;
    struct uprobe probe_src;
    struct uprobe probe_uref;
    struct uprobe probe_burst;
    struct upipe_mgr *source_mgr;
    struct upipe *src;
    struct upipe *last_inner;
    struct upipe *output;
    struct uclock *uclock;
    uint64_t start;
    size_t size;
    bool first_uref;
};

static int probe_burst(struct uprobe *uprobe, struct upipe *inner,
                     int event, va_list args);
static int probe_uref(struct uprobe *uprobe, struct upipe *inner,
                      int event, va_list args);
static int probe_src(struct uprobe *uprobe, struct upipe *inner,
                     int event, va_list args);

UPIPE_HELPER_UPIPE(upipe_seg_src, upipe, UPIPE_SEG_SRC_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_seg_src, urefcount, upipe_seg_src_no_ref);
UPIPE_HELPER_UREFCOUNT_REAL(upipe_seg_src, urefcount_real, upipe_seg_src_free);
UPIPE_HELPER_VOID(upipe_seg_src);
UPIPE_HELPER_UPROBE(upipe_seg_src, urefcount_real, probe_src, probe_src);
UPIPE_HELPER_UPROBE(upipe_seg_src, urefcount_real, probe_uref, probe_uref);
UPIPE_HELPER_UPROBE(upipe_seg_src, urefcount_real, probe_burst, probe_burst);
UPIPE_HELPER_INNER(upipe_seg_src, src);
UPIPE_HELPER_INNER(upipe_seg_src, last_inner);
UPIPE_HELPER_BIN_OUTPUT(upipe_seg_src, last_inner, output, requests);
UPIPE_HELPER_UCLOCK(upipe_seg_src, uclock, request_uclock, NULL,
                    upipe_seg_src_register_bin_output_request,
                    upipe_seg_src_unregister_bin_output_request);

static int upipe_seg_src_throw_update(struct upipe *upipe,
                                      uint64_t size,
                                      uint64_t delta)
{
    upipe_dbg_va(upipe, "throw update %"PRIu64" bytes in %"PRIu64" ms",
                 size, delta / (UCLOCK_FREQ / 1000));
    return upipe_throw(upipe, UPROBE_SEG_SRC_UPDATE, UPIPE_SEG_SRC_SIGNATURE,
                       size, delta);
}

static int upipe_seg_src_update(struct upipe *upipe)
{
    struct upipe_seg_src *upipe_seg_src = upipe_seg_src_from_upipe(upipe);

    if (likely(upipe_seg_src->uclock) && upipe_seg_src->start != UINT64_MAX) {
        uint64_t now = uclock_now(upipe_seg_src->uclock);
        assert(now >= upipe_seg_src->start);
        uint64_t delta = now - upipe_seg_src->start;
        return upipe_seg_src_throw_update(upipe, upipe_seg_src->size, delta);
    }
    upipe_warn(upipe, "no uclock set");
    return UBASE_ERR_INVALID;
}

static int probe_burst(struct uprobe *uprobe, struct upipe *inner,
                     int event, va_list args)
{
    struct upipe_seg_src *upipe_seg_src =
        upipe_seg_src_from_probe_burst(uprobe);
    struct upipe *upipe = upipe_seg_src_to_upipe(upipe_seg_src);

    switch (event) {
    case UPROBE_BURST_UPDATE:
        UBASE_SIGNATURE_CHECK(args, UPIPE_BURST_SIGNATURE);
        bool empty = va_arg(args, int);
        if (empty && upipe_seg_src->src == NULL)
            upipe_seg_src_clean_last_inner(upipe);
        return UBASE_ERR_NONE;

    case UPROBE_DEAD:
        return upipe_throw_source_end(upipe);
    }

    return upipe_throw_proxy(upipe, inner, event, args);
}

static int probe_uref(struct uprobe *uprobe, struct upipe *inner,
                      int event, va_list args)
{
    struct upipe_seg_src *upipe_seg_src = upipe_seg_src_from_probe_uref(uprobe);
    struct upipe *upipe = upipe_seg_src_to_upipe(upipe_seg_src);

    switch (event) {
    case UPROBE_NEED_OUTPUT: {
        struct upipe_mgr *upipe_burst_mgr = upipe_burst_mgr_alloc();
        UBASE_ALLOC_RETURN(upipe_burst_mgr);
        struct upipe *output = upipe_void_alloc_output(
            inner, upipe_burst_mgr,
            uprobe_pfx_alloc(uprobe_use(&upipe_seg_src->probe_burst),
                             UPROBE_LOG_VERBOSE, "burst"));
        upipe_mgr_release(upipe_burst_mgr);
        UBASE_ALLOC_RETURN(output);
        upipe_seg_src_store_bin_output(upipe, output);
        return UBASE_ERR_NONE;
    }

    case UPROBE_PROBE_UREF: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_PROBE_UREF_SIGNATURE);
        struct uref *uref = va_arg(args, struct uref *);

        size_t size;
        UBASE_RETURN(uref_block_size(uref, &size));
        upipe_seg_src->size += size;

        if (unlikely(upipe_seg_src->first_uref)) {
            upipe_seg_src->first_uref = false;
            if (unlikely(upipe_seg_src->uclock == NULL))
                return UBASE_ERR_INVALID;
            upipe_seg_src->start = uclock_now(upipe_seg_src->uclock);
        }
        return UBASE_ERR_NONE;
    }

    case UPROBE_DEAD:
        return upipe_seg_src_update(upipe);
    }
    return upipe_throw_proxy(upipe, inner, event, args);
}

static int probe_src(struct uprobe *uprobe, struct upipe *inner,
                     int event, va_list args)
{
    struct upipe_seg_src *upipe_seg_src = upipe_seg_src_from_probe_src(uprobe);
    struct upipe *upipe = upipe_seg_src_to_upipe(upipe_seg_src);

    switch (event) {
    case UPROBE_NEED_OUTPUT: {
        struct upipe_mgr *upipe_probe_uref_mgr = upipe_probe_uref_mgr_alloc();
        UBASE_ALLOC_RETURN(upipe_probe_uref_mgr);
        struct upipe *output = upipe_void_alloc_output(
            inner, upipe_probe_uref_mgr,
            uprobe_pfx_alloc(uprobe_use(&upipe_seg_src->probe_uref),
                             UPROBE_LOG_VERBOSE, "uref"));
        upipe_mgr_release(upipe_probe_uref_mgr);
        UBASE_ALLOC_RETURN(output);
        upipe_release(output);
        return UBASE_ERR_NONE;
    }

    case UPROBE_SOURCE_END:
        upipe_seg_src_clean_src(upipe);
        return UBASE_ERR_NONE;
    }
    return upipe_throw_proxy(upipe, inner, event, args);
}

static struct upipe *upipe_seg_src_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature,
                                         va_list args)
{
    struct upipe *upipe =
        upipe_seg_src_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_seg_src_init_urefcount(upipe);
    upipe_seg_src_init_urefcount_real(upipe);
    upipe_seg_src_init_probe_src(upipe);
    upipe_seg_src_init_probe_uref(upipe);
    upipe_seg_src_init_probe_burst(upipe);
    upipe_seg_src_init_src(upipe);
    upipe_seg_src_init_bin_output(upipe);
    upipe_seg_src_init_uclock(upipe);

    struct upipe_seg_src *upipe_seg_src = upipe_seg_src_from_upipe(upipe);
    upipe_seg_src->source_mgr = NULL;
    upipe_seg_src->size = 0;
    upipe_seg_src->first_uref = true;
    upipe_seg_src->start = UINT64_MAX;

    upipe_throw_ready(upipe);

    return upipe;
}

static void upipe_seg_src_free(struct upipe *upipe)
{
    struct upipe_seg_src *upipe_seg_src = upipe_seg_src_from_upipe(upipe);

    upipe_throw_dead(upipe);

    upipe_mgr_release(upipe_seg_src->source_mgr);
    upipe_seg_src_clean_uclock(upipe);
    upipe_seg_src_clean_probe_burst(upipe);
    upipe_seg_src_clean_probe_uref(upipe);
    upipe_seg_src_clean_probe_src(upipe);
    upipe_seg_src_clean_bin_output(upipe);
    upipe_seg_src_clean_urefcount(upipe);
    upipe_seg_src_clean_urefcount_real(upipe);
    upipe_seg_src_free_void(upipe);
}

static void upipe_seg_src_no_ref(struct upipe *upipe)
{
    upipe_seg_src_clean_src(upipe);
    upipe_seg_src_clean_last_inner(upipe);
    upipe_seg_src_release_urefcount_real(upipe);
}

static int upipe_seg_src_check_source_mgr(struct upipe *upipe)
{
    struct upipe_seg_src *upipe_seg_src = upipe_seg_src_from_upipe(upipe);
    if (likely(upipe_seg_src->source_mgr != NULL))
        return UBASE_ERR_NONE;
    return upipe_throw_need_source_mgr(upipe, &upipe_seg_src->source_mgr);
}

static int upipe_seg_src_check_src(struct upipe *upipe)
{
    struct upipe_seg_src *upipe_seg_src = upipe_seg_src_from_upipe(upipe);

    if (likely(upipe_seg_src->src != NULL))
        return UBASE_ERR_NONE;

    upipe_seg_src->first_uref = true;
    upipe_seg_src_clean_src(upipe);
    UBASE_RETURN(upipe_seg_src_check_source_mgr(upipe));
    struct upipe *src = upipe_void_alloc(
        upipe_seg_src->source_mgr,
        uprobe_pfx_alloc(uprobe_use(&upipe_seg_src->probe_src),
                         UPROBE_LOG_VERBOSE, "src"));
    UBASE_ALLOC_RETURN(src);
    upipe_seg_src_store_src(upipe, src);
    return UBASE_ERR_NONE;
}

static int upipe_seg_src_control(struct upipe *upipe,
                                 int command,
                                 va_list args)
{
    switch (command) {
    case UPIPE_ATTACH_UCLOCK:
        upipe_seg_src_require_uclock(upipe);
        return UBASE_ERR_NONE;

    case UPIPE_SET_URI:
        upipe_seg_src_clean_src(upipe);
        /* fallthrough */
    case UPIPE_GET_OUTPUT_SIZE:
    case UPIPE_SET_OUTPUT_SIZE:
    case UPIPE_SRC_GET_SIZE:
    case UPIPE_SRC_GET_POSITION:
    case UPIPE_SRC_SET_POSITION:
    case UPIPE_SRC_SET_RANGE:
    case UPIPE_SRC_GET_RANGE: {
        UBASE_RETURN(upipe_seg_src_check_src(upipe));
        return upipe_seg_src_control_src(upipe, command, args);
    }
    case UPIPE_BIN_GET_FIRST_INNER: {
        struct upipe_seg_src *upipe_seg_src = upipe_seg_src_from_upipe(upipe);
        struct upipe **p = va_arg(args, struct upipe **);
        *p = upipe_seg_src->src;
        return (*p != NULL) ? UBASE_ERR_NONE : UBASE_ERR_UNHANDLED;
    }
    }

    return upipe_seg_src_control_bin_output(upipe, command, args);
}

static struct upipe_mgr upipe_seg_src_mgr = {
    .refcount = NULL,
    .signature = UPIPE_SEG_SRC_SIGNATURE,
    .upipe_alloc = upipe_seg_src_alloc,
    .upipe_control = upipe_seg_src_control,
    .upipe_event_str = upipe_seg_src_event_str,
};

struct upipe_mgr *upipe_seg_src_mgr_alloc(void)
{
    return &upipe_seg_src_mgr;
}
