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

#include <upipe/upipe.h>
#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/ubuf_pic.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-modules/upipe_crop.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/** we only accept pictures */
#define EXPECTED_FLOW_DEF "pic."

/** @internal @This is the private context of a crop pipe */
struct upipe_crop {
    /** refcount management structure */
    struct urefcount urefcount;

    /** configured offset from the left border */
    int64_t loffset;
    /** configured offset from the right border */
    int64_t roffset;
    /** configured offset from the top border */
    int64_t toffset;
    /** configured offset from the bottom border */
    int64_t boffset;

    /** output pipe */
    struct upipe *output;
    /** output flow_definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** computed output horizontal size */
    uint64_t out_hsize;
    /** computed output vertical size */
    uint64_t out_vsize;
    /** computed horizontal skip */
    uint64_t hskip;
    /** computed vertical skip */
    uint64_t vskip;

    /** number of pixels in a macropixel of the input picture */
    uint8_t macropixel;
    /** highest horizontal subsampling of the input picture */
    uint8_t hsub;
    /** highest vertical subsampling of the input picture */
    uint8_t vsub;
    /** horizontal size of the input picture */
    uint64_t hsize;
    /** vertical size of the input picture */
    uint64_t vsize;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_crop, upipe, UPIPE_CROP_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_crop, urefcount, upipe_crop_free)
UPIPE_HELPER_VOID(upipe_crop)
UPIPE_HELPER_OUTPUT(upipe_crop, output, flow_def, output_state, request_list)

/** @internal @This allocates a crop pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_crop_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_crop_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_crop *crop = upipe_crop_from_upipe(upipe);
    upipe_crop_init_urefcount(upipe);
    upipe_crop_init_output(upipe);
    crop->loffset = crop->roffset = crop->toffset = crop->boffset = 0;
    crop->out_hsize = crop->out_vsize = crop->hskip = crop->vskip = UINT64_MAX;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This receives incoming uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure describing the picture
 * @param upump_p reference to pump that generated the buffer
 *
 */
static void upipe_crop_input(struct upipe *upipe, struct uref *uref,
                             struct upump **upump_p)
{
    struct upipe_crop *crop = upipe_crop_from_upipe(upipe);
    if (unlikely(uref->ubuf == NULL)) {
        upipe_warn(upipe, "invalid uref received");
        uref_free(uref);
        return;
    }

    size_t hsize, vsize;
    uint8_t macropixel;
    if (unlikely(!ubase_check(uref_pic_size(uref, &hsize, &vsize,
                                            &macropixel)) ||
                 hsize != crop->hsize || vsize != crop->vsize ||
                 macropixel != crop->macropixel)) {
        upipe_warn(upipe, "dropping incompatible subpicture");
        upipe_throw_error(upipe, UBASE_ERR_INVALID);
        uref_free(uref);
        return;
    }

    UBASE_ERROR(upipe, uref_pic_resize(uref, crop->hskip, crop->vskip,
                                       crop->out_hsize, crop->out_vsize))

    upipe_crop_output(upipe, uref, upump_p);
}

/** @internal @This pre-calculates the offsets.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_crop_prepare(struct upipe *upipe)
{
    struct upipe_crop *crop = upipe_crop_from_upipe(upipe);
    if (crop->flow_def == NULL)
        return UBASE_ERR_NONE;

    struct uref *flow_def = crop->flow_def;
    crop->flow_def = NULL;
    uref_pic_delete_lpadding(flow_def);
    uref_pic_delete_rpadding(flow_def);
    uref_pic_delete_tpadding(flow_def);
    uref_pic_delete_bpadding(flow_def);

    /* Round parameters */
    uint8_t hround = crop->hsub * crop->macropixel;
    int64_t loffset = crop->loffset;
    int64_t roffset = crop->roffset;
    if (hround > 1) {
        loffset -= loffset % hround;
        roffset -= roffset % hround;
    }

    uint8_t vround = crop->vsub;
    int64_t toffset = crop->toffset;
    int64_t boffset = crop->boffset;
    if (vround > 1) {
        toffset -= toffset % vround;
        boffset -= boffset % vround;
    }

#define OFFSET(dir)                                                         \
    if (dir##offset < 0) {                                                  \
        UBASE_RETURN(uref_pic_set_##dir##padding(flow_def, -dir##offset))   \
        dir##offset = 0;                                                    \
    }
    OFFSET(l)
    OFFSET(r)
    OFFSET(t)
    OFFSET(b)
#undef OFFSET

    if (crop->hsize < loffset + roffset ||
        crop->vsize < toffset + boffset) {
        uref_free(flow_def);
        return UBASE_ERR_INVALID;
    }

    crop->hskip = loffset;
    crop->vskip = toffset;
    crop->out_hsize = crop->hsize - loffset - roffset;
    crop->out_vsize = crop->vsize - toffset - boffset;
    UBASE_RETURN(uref_pic_flow_set_hsize(flow_def, crop->out_hsize))
    UBASE_RETURN(uref_pic_flow_set_vsize(flow_def, crop->out_vsize))
    upipe_crop_store_flow_def(upipe, flow_def);
    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_crop_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))

    uint8_t macropixel, hsub, vsub;
    uint64_t hsize, vsize;
    struct urational sar;
    sar.num = sar.den = 1;
    UBASE_RETURN(uref_pic_flow_get_macropixel(flow_def, &macropixel))
    UBASE_RETURN(uref_pic_flow_max_subsampling(flow_def, &hsub, &vsub))
    UBASE_RETURN(uref_pic_flow_get_hsize(flow_def, &hsize))
    UBASE_RETURN(uref_pic_flow_get_vsize(flow_def, &vsize))

    flow_def = uref_dup(flow_def);
    if (unlikely(flow_def == NULL))
        return UBASE_ERR_ALLOC;
    upipe_crop_store_flow_def(upipe, flow_def);

    struct upipe_crop *upipe_crop = upipe_crop_from_upipe(upipe);
    upipe_crop->macropixel = macropixel;
    upipe_crop->hsub = hsub;
    upipe_crop->vsub = vsub;
    upipe_crop->hsize = hsize;
    upipe_crop->vsize = vsize;
    return upipe_crop_prepare(upipe);
}

/** @internal @This gets the offsets (from the respective borders of the frame)
 * of the cropped rectangle.
 *
 * @param upipe description structure of the pipe
 * @param loffset_p filled in with the offset from the left border
 * @param roffset_p filled in with the offset from the right border
 * @param toffset_p filled in with the offset from the top border
 * @param boffset_p filled in with the offset from the bottom border
 * @return an error code
 */
static int _upipe_crop_get_rect(struct upipe *upipe,
        int64_t *loffset_p, int64_t *roffset_p,
        int64_t *toffset_p, int64_t *boffset_p)
{
    struct upipe_crop *crop = upipe_crop_from_upipe(upipe);
    *loffset_p = crop->loffset;
    *roffset_p = crop->roffset;
    *toffset_p = crop->toffset;
    *boffset_p = crop->boffset;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the offsets (from the respective borders of the frame)
 * of the cropped rectangle.
 *
 * @param upipe description structure of the pipe
 * @param loffset offset from the left border
 * @param roffset offset from the right border
 * @param toffset offset from the top border
 * @param boffset offset from the bottom border
 * @return an error code
 */
static int _upipe_crop_set_rect(struct upipe *upipe,
        int64_t loffset, int64_t roffset, int64_t toffset, int64_t boffset)
{
    struct upipe_crop *crop = upipe_crop_from_upipe(upipe);
    crop->loffset = loffset;
    crop->roffset = roffset;
    crop->toffset = toffset;
    crop->boffset = boffset;
    return upipe_crop_prepare(upipe);
}

/** @internal @This processes control commands on a file source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_crop_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_crop_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow = va_arg(args, struct uref *);
            return upipe_crop_set_flow_def(upipe, flow);
        }

        case UPIPE_CROP_GET_RECT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_CROP_SIGNATURE);
            int64_t *loffset_p = va_arg(args, int64_t *);
            int64_t *roffset_p = va_arg(args, int64_t *);
            int64_t *toffset_p = va_arg(args, int64_t *);
            int64_t *boffset_p = va_arg(args, int64_t *);
            return _upipe_crop_get_rect(upipe,
                    loffset_p, roffset_p, toffset_p, boffset_p);
        }
        case UPIPE_CROP_SET_RECT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_CROP_SIGNATURE);
            int64_t loffset = va_arg(args, int64_t);
            int64_t roffset = va_arg(args, int64_t);
            int64_t toffset = va_arg(args, int64_t);
            int64_t boffset = va_arg(args, int64_t);
            return _upipe_crop_set_rect(upipe,
                    loffset, roffset, toffset, boffset);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_crop_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_crop_clean_output(upipe);
    upipe_crop_clean_urefcount(upipe);
    upipe_crop_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_crop_mgr = {
    .refcount = NULL,
    .signature = UPIPE_CROP_SIGNATURE,

    .upipe_alloc = upipe_crop_alloc,
    .upipe_input = upipe_crop_input,
    .upipe_control = upipe_crop_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for crop pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_crop_mgr_alloc(void)
{
    return &upipe_crop_mgr;
}
