/*
 * Copyright (C) 2014-2015 OpenHeadend S.A.R.L.
 *
 * Authors: Sebastien Gougelet
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
#include <upipe/upipe_helper_subpipe.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-modules/upipe_blit.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/** we only accept pictures */
#define EXPECTED_FLOW_DEF "pic."

/** @internal @This is the private context of a blit pipe */
struct upipe_blit {
    /** refcount management structure */
    struct urefcount urefcount;

    /** output pipe */
    struct upipe *output;
    /** output flow_definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** list of input subpipes */
    struct uchain subs;
    /** manager to create input subpipes */
    struct upipe_mgr sub_mgr;

    /** number of pixels in a macropixel of the output picture */
    uint8_t macropixel;
    /** highest horizontal subsampling of the output picture */
    uint8_t hsub;
    /** highest vertical subsampling of the output picture */
    uint8_t vsub;
    /** horizontal size of the output picture */
    uint64_t hsize;
    /** vertical size of the output picture */
    uint64_t vsize;
    /** sample aspect ratio of the output picture */
    struct urational sar;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_blit, upipe, UPIPE_BLIT_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_blit, urefcount, upipe_blit_free)
UPIPE_HELPER_VOID(upipe_blit)
UPIPE_HELPER_OUTPUT(upipe_blit, output, flow_def, output_state, request_list)

/** @internal @This is the private context of an input of a blit pipe. */
struct upipe_blit_sub {
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked lists */
    struct uchain uchain;

    /** offset from the left border */
    uint64_t loffset;
    /** offset from the right border */
    uint64_t roffset;
    /** offset from the top border */
    uint64_t toffset;
    /** offset from the bottom border */
    uint64_t boffset;

    /** last received ubuf */
    struct ubuf *ubuf;

    /** horizontal size */
    uint64_t hsize;
    /** vertical size */
    uint64_t vsize;
    /** horizontal position */
    uint64_t hposition;
    /** vertical position */
    uint64_t vposition;

    /** flow format urequests */
    struct uchain flow_format_requests;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_blit_sub, upipe, UPIPE_BLIT_SUB_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_blit_sub, urefcount, upipe_blit_sub_free)
UPIPE_HELPER_VOID(upipe_blit_sub)

UPIPE_HELPER_SUBPIPE(upipe_blit, upipe_blit_sub, sub, sub_mgr, subs, uchain)

/** @internal @This allocates an input subpipe of a blit pipe.
*
* @param mgr common management structure
* @param uprobe structure used to raise events
* @param signature signature of the pipe allocator
* @param args optional arguments
* @return pointer to upipe or NULL in case of allocation error
*/
static struct upipe *upipe_blit_sub_alloc(struct upipe_mgr *mgr,
                                          struct uprobe *uprobe,
                                          uint32_t signature, va_list args)
{
    struct upipe *upipe =
        upipe_blit_sub_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_blit_sub *sub = upipe_blit_sub_from_upipe(upipe);
    upipe_blit_sub_init_urefcount(upipe);
    upipe_blit_sub_init_sub(upipe);
    sub->loffset = sub->roffset = sub->toffset = sub->boffset = 0;
    sub->ubuf = NULL;
    sub->hsize = sub->vsize = sub->hposition = sub->vposition = UINT64_MAX;
    ulist_init(&sub->flow_format_requests);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This blits the subpicture into the input uref.
*
* @param upipe description structure of the pipe
* @param uref uref structure
*/
static void upipe_blit_sub_work(struct upipe *upipe, struct uref *uref)
{
    struct upipe_blit_sub *sub = upipe_blit_sub_from_upipe(upipe);
    if (unlikely(sub->ubuf == NULL))
        return;

    int err = uref_pic_blit(uref, sub->ubuf, sub->hposition, sub->vposition,
                            0, 0, sub->hsize, sub->vsize);
    if (unlikely(!ubase_check(err))) {
        upipe_warn(upipe, "unable to blit picture");
        upipe_throw_error(upipe, err);
    }
    ubuf_free(sub->ubuf);
    sub->ubuf = NULL;
}

/** @internal @This receives data.
*
* @param upipe description structure of the pipe
* @param uref uref structure
* @param upump_p reference to pump that generated the buffer
*/
static void upipe_blit_sub_input(struct upipe *upipe, struct uref *uref,
                                 struct upump **upump_p)
{
    struct upipe_blit_sub *sub = upipe_blit_sub_from_upipe(upipe);
    struct upipe_blit *upipe_blit = upipe_blit_from_sub_mgr(upipe->mgr);

    if (unlikely(sub->hsize == UINT64_MAX || sub->vsize == UINT64_MAX ||
                 sub->hposition == UINT64_MAX ||
                 sub->vposition == UINT64_MAX)) {
        upipe_warn(upipe, "dropping incompatible subpicture");
        upipe_throw_error(upipe, UBASE_ERR_INVALID);
        uref_free(uref);
        return;
    }

    uint64_t hsize, vsize;
    uint8_t macropixel;
    if (unlikely(!ubase_check(uref_pic_size(uref, &hsize, &vsize,
                                            &macropixel)) ||
                 hsize != sub->hsize || vsize != sub->vsize ||
                 macropixel != upipe_blit->macropixel)) {
        upipe_warn(upipe, "dropping incompatible subpicture");
        upipe_throw_error(upipe, UBASE_ERR_INVALID);
        uref_free(uref);
        return;
    }

    ubuf_free(sub->ubuf);
    sub->ubuf = uref_detach_ubuf(uref);
    uref_free(uref);
}

/** @internal @This provides a flow format suggestion.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_blit_sub_provide_flow_format(struct upipe *upipe)
{
    struct upipe_blit *upipe_blit = upipe_blit_from_sub_mgr(upipe->mgr);
    if (upipe_blit->flow_def == NULL)
        return UBASE_ERR_NONE;

    /* Compute size of destination rectangle */
    struct upipe_blit_sub *sub = upipe_blit_sub_from_upipe(upipe);
    uint8_t hround = upipe_blit->hsub * upipe_blit->macropixel;
    uint64_t loffset = sub->loffset;
    uint64_t roffset = sub->roffset;
    if (hround > 1) {
        loffset -= loffset % hround;
        roffset += hround - 1;
        roffset -= roffset % hround;
    }

    uint8_t vround = upipe_blit->vsub;
    uint64_t toffset = sub->toffset;
    uint64_t boffset = sub->boffset;
    if (vround > 1) {
        toffset -= toffset % vround;
        boffset += vround - 1;
        boffset -= boffset % vround;
    }

    uint64_t dest_hsize = upipe_blit->hsize - loffset - roffset;
    uint64_t dest_vsize = upipe_blit->vsize - toffset - boffset;
    struct urational dest_dar;
    dest_dar.num = dest_hsize * upipe_blit->sar.num;
    dest_dar.den = dest_vsize * upipe_blit->sar.den;
    urational_simplify(&dest_dar);
    upipe_dbg_va(upipe,
            "destination rectangle %"PRIu64"x%"PRIu64"@%"PRIu64":%"PRIu64" DAR %"PRId64":%"PRIu64,
            dest_hsize, dest_vsize, loffset, toffset,
            dest_dar.num, dest_dar.den);

    struct uchain *uchain;
    ulist_foreach (&sub->flow_format_requests, uchain) {
        struct urequest *proxy = urequest_from_uchain(uchain);
        struct urequest *urequest =
            urequest_get_opaque(proxy, struct urequest *);
        if (unlikely(urequest->uref == NULL)) {
            upipe_warn(upipe, "invalid flow format urequest");
            upipe_throw_error(upipe, UBASE_ERR_INVALID);
            continue;
        }

        /* Get original sizes */
        uint64_t src_hsize, src_vsize;
        if (unlikely(!ubase_check(uref_pic_flow_get_hsize(urequest->uref,
                                                          &src_hsize)) ||
                     !ubase_check(uref_pic_flow_get_vsize(urequest->uref,
                                                          &src_vsize)))) {
            upipe_warn(upipe, "invalid flow format urequest size");
            upipe_throw_error(upipe, UBASE_ERR_INVALID);
            continue;
        }
        struct urational src_sar;
        src_sar.num = src_sar.den = 1;
        uref_pic_flow_get_sar(urequest->uref, &src_sar);

        /* FIXME take into account overscan */
        struct urational src_dar;
        src_dar.num = src_hsize * src_sar.num;
        src_dar.den = src_vsize * src_sar.den;
        urational_simplify(&src_dar);

        struct uref *uref = uref_dup(urequest->uref);
        if (unlikely(uref == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            continue;
        }

        struct urational div = urational_divide(&dest_dar, &src_dar);
        if (div.num > div.den) {
            /* Destination rectangle larger than source picture */
            sub->hsize = dest_hsize * div.den / div.num;
            sub->hsize -= sub->hsize % hround;
            assert(sub->hsize <= dest_hsize);
            sub->vsize = dest_vsize;
            loffset += (dest_hsize - sub->hsize) / 2;
            loffset -= loffset % hround;
        } else if (div.num < div.den) {
            /* Destination rectangle smaller than source picture */
            sub->hsize = dest_hsize;
            sub->vsize = dest_vsize * div.num / div.den;
            sub->vsize -= sub->vsize % vround;
            assert(sub->vsize <= dest_vsize);
            toffset += (dest_vsize - sub->vsize) / 2;
            toffset -= toffset % hround;
        } else {
            sub->hsize = dest_hsize;
            sub->vsize = dest_vsize;
        }
        sub->hposition = loffset;
        sub->vposition = toffset;

        uref_pic_flow_set_hsize(uref, sub->hsize);
        uref_pic_flow_set_vsize(uref, sub->vsize);
        uref_pic_flow_clear_format(uref);
        uref_pic_flow_copy_format(uref, upipe_blit->flow_def);
        uref_pic_flow_delete_sar(uref);
        uref_pic_flow_delete_overscan(uref);
        uref_pic_flow_delete_dar(uref);

        /* Compare with previous request and bail out if identical */
        if (proxy->uref != NULL &&
            !udict_cmp(proxy->uref->udict, uref->udict)) {
            uref_free(uref);
            continue;
        }

        uref_free(proxy->uref);
        proxy->uref = uref_dup(uref);
        if (proxy->uref == NULL)
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);

        upipe_dbg_va(upipe, "providing flow format for urequest %p", urequest);
        int err = urequest_provide_flow_format(urequest, uref);
        if (!ubase_check(err))
            upipe_throw_error(upipe, err);
    }

    return UBASE_ERR_NONE;
}

/** @internal @This allocates a provider for flow format suggestions.
 *
 * @param upipe description structure of the pipe
 * @param request description structure of the request
 * @return an error code
 */
static int upipe_blit_sub_alloc_flow_format_provider(struct upipe *upipe,
                                                     struct urequest *request)
{
    struct urequest *proxy = malloc(sizeof(struct urequest));
    UBASE_ALLOC_RETURN(proxy);
    urequest_set_opaque(proxy, request);
    urequest_init(proxy, request->type, NULL, NULL,
                  (urequest_free_func)free);

    struct upipe_blit_sub *sub = upipe_blit_sub_from_upipe(upipe);
    ulist_add(&sub->flow_format_requests, urequest_to_uchain(proxy));
    return upipe_blit_sub_provide_flow_format(upipe);
}

/** @internal @This frees a provider for flow format suggestions.
 *
 * @param upipe description structure of the pipe
 * @param request description structure of the request
 * @return an error code
 */
static int upipe_blit_sub_free_flow_format_provider(struct upipe *upipe,
                                                    struct urequest *request)
{
    struct upipe_blit_sub *sub = upipe_blit_sub_from_upipe(upipe);
    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach (&sub->flow_format_requests, uchain, uchain_tmp) {
        struct urequest *proxy = urequest_from_uchain(uchain);
        if (urequest_get_opaque(proxy, struct urequest *) == request) {
            ulist_delete(urequest_to_uchain(proxy));
            urequest_clean(proxy);
            urequest_free(proxy);
            return UBASE_ERR_NONE;
        }
    }
    return UBASE_ERR_INVALID;
}

/** @internal @This sets the input flow definition.
*
* @param upipe description structure of the pipe
* @param flow_def flow definition packet
* @return an error code
*/
static int upipe_blit_sub_set_flow_def(struct upipe *upipe,
                                       struct uref *flow_def)
{
    struct upipe_blit_sub *sub = upipe_blit_sub_from_upipe(upipe);
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))

    ubuf_free(sub->ubuf);
    sub->ubuf = NULL;

    return UBASE_ERR_NONE;
}

/** @internal @This gets the offsets (from the respective borders of the frame)
 * of the rectangle onto which the input of the subpipe will be blitted.
 *
 * @param upipe description structure of the pipe
 * @param loffset_p filled in with the offset from the left border
 * @param roffset_p filled in with the offset from the right border
 * @param toffset_p filled in with the offset from the top border
 * @param boffset_p filled in with the offset from the bottom border
 * @return an error code
 */
static int _upipe_blit_sub_get_rect(struct upipe *upipe,
        uint64_t *loffset_p, uint64_t *roffset_p,
        uint64_t *toffset_p, uint64_t *boffset_p)
{
    struct upipe_blit_sub *sub = upipe_blit_sub_from_upipe(upipe);
    *loffset_p = sub->loffset;
    *roffset_p = sub->roffset;
    *toffset_p = sub->toffset;
    *boffset_p = sub->boffset;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the offsets (from the respective borders of the frame)
 * of the rectangle onto which the input of the subpipe will be blitted.
 *
 * @param upipe description structure of the pipe
 * @param loffset offset from the left border
 * @param roffset offset from the right border
 * @param toffset offset from the top border
 * @param boffset offset from the bottom border
 * @return an error code
 */
static int _upipe_blit_sub_set_rect(struct upipe *upipe,
        uint64_t loffset, uint64_t roffset, uint64_t toffset, uint64_t boffset)
{
    struct upipe_blit_sub *sub = upipe_blit_sub_from_upipe(upipe);
    sub->loffset = loffset;
    sub->roffset = roffset;
    sub->toffset = toffset;
    sub->boffset = boffset;
    upipe_blit_sub_provide_flow_format(upipe);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a subpipe of a blit
* pipe.
*
* @param upipe description structure of the pipe
* @param command type of command to process
* @param args arguments of the command
* @return false in case of error
*/
static int upipe_blit_sub_control(struct upipe *upipe,
                                  int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct upipe_blit *upipe_blit = upipe_blit_from_sub_mgr(upipe->mgr);
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR)
                return upipe_throw_provide_request(upipe, request);
            if (request->type == UREQUEST_FLOW_FORMAT)
                return upipe_blit_sub_alloc_flow_format_provider(upipe, request);
            return upipe_blit_alloc_output_proxy(
                    upipe_blit_to_upipe(upipe_blit), request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct upipe_blit *upipe_blit = upipe_blit_from_sub_mgr(upipe->mgr);
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR)
                return UBASE_ERR_NONE;
            if (request->type == UREQUEST_FLOW_FORMAT)
                return upipe_blit_sub_free_flow_format_provider(upipe, request);
            return upipe_blit_free_output_proxy(
                    upipe_blit_to_upipe(upipe_blit), request);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_blit_sub_set_flow_def(upipe, flow_def);
        }
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_blit_sub_get_super(upipe, p);
        }

        case UPIPE_BLIT_SUB_GET_RECT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_BLIT_SUB_SIGNATURE);
            uint64_t *loffset_p = va_arg(args, uint64_t *);
            uint64_t *roffset_p = va_arg(args, uint64_t *);
            uint64_t *toffset_p = va_arg(args, uint64_t *);
            uint64_t *boffset_p = va_arg(args, uint64_t *);
            return _upipe_blit_sub_get_rect(upipe,
                    loffset_p, roffset_p, toffset_p, boffset_p);
        }
        case UPIPE_BLIT_SUB_SET_RECT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_BLIT_SUB_SIGNATURE);
            uint64_t loffset = va_arg(args, uint64_t);
            uint64_t roffset = va_arg(args, uint64_t);
            uint64_t toffset = va_arg(args, uint64_t);
            uint64_t boffset = va_arg(args, uint64_t);
            return _upipe_blit_sub_set_rect(upipe,
                    loffset, roffset, toffset, boffset);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees an input subpipe.
*
* @param upipe description structure of the pipe
*/
static void upipe_blit_sub_free(struct upipe *upipe)
{
    struct upipe_blit_sub *sub = upipe_blit_sub_from_upipe(upipe);
    upipe_throw_dead(upipe);
    ubuf_free(sub->ubuf);
    upipe_blit_sub_clean_sub(upipe);
    upipe_blit_sub_clean_urefcount(upipe);
    upipe_blit_sub_free_void(upipe);
}


/** @internal @This initializes the input manager for a blit pipe.
*
* @param upipe description structure of the pipe
*/
static void upipe_blit_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_blit *upipe_blit = upipe_blit_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_blit->sub_mgr;
    sub_mgr->refcount = upipe_blit_to_urefcount(upipe_blit);
    sub_mgr->signature = UPIPE_BLIT_SUB_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_blit_sub_alloc;
    sub_mgr->upipe_input = upipe_blit_sub_input;
    sub_mgr->upipe_control = upipe_blit_sub_control;
    sub_mgr->upipe_mgr_control = NULL;
}

/** @internal @This allocates a blit pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_blit_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_blit_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_blit_init_urefcount(upipe);
    upipe_blit_init_output(upipe);
    upipe_blit_init_sub_mgr(upipe);
    upipe_blit_init_sub_subs(upipe);

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
static void upipe_blit_input(struct upipe *upipe, struct uref *uref, 
                             struct upump **upump_p)
{
    struct upipe_blit *upipe_blit = upipe_blit_from_upipe(upipe);
    if (unlikely(uref->ubuf == NULL)) {
        upipe_warn(upipe, "invalid uref received");
        uref_free(uref);
        return;
    }

    /* Check if we can write on the planes */
    bool writable = true;
    const char *chroma = NULL;
    while (ubase_check(uref_pic_plane_iterate(uref, &chroma)) &&
           chroma != NULL) {
        if (!ubase_check(uref_pic_plane_write(uref, chroma, 0, 0, -1, -1,
                                              NULL)) ||
            !ubase_check(uref_pic_plane_unmap(uref, chroma, 0, 0, -1, -1))) {
            writable = false;
            break;
        }
    }

    if (!writable) {
        struct ubuf *ubuf = ubuf_pic_copy(uref->ubuf->mgr,
                                            uref->ubuf,0, 0, -1, -1);
        if (unlikely(ubuf == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            uref_free(uref);
            return;
        }
        uref_attach_ubuf(uref, ubuf);
    }

    struct uchain *uchain;
    ulist_foreach (&upipe_blit->subs, uchain) {
        struct upipe_blit_sub *sub = upipe_blit_sub_from_uchain(uchain);
        upipe_blit_sub_work(upipe_blit_sub_to_upipe(sub), uref);
    }

    upipe_blit_output(upipe, uref, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_blit_set_flow_def(struct upipe *upipe, struct uref *flow_def)
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
    uref_pic_flow_get_sar(flow_def, &sar);

    struct upipe_blit *upipe_blit = upipe_blit_from_upipe(upipe);
    bool flow_format_change;
    if (upipe_blit->flow_def == NULL)
        flow_format_change = true;
    else
        flow_format_change =
            uref_pic_flow_compare_format(upipe_blit->flow_def, flow_def) &&
            !uref_pic_flow_cmp_hsize(upipe_blit->flow_def, flow_def) &&
            !uref_pic_flow_cmp_vsize(upipe_blit->flow_def, flow_def);

    flow_def = uref_dup(flow_def);
    if (unlikely(flow_def == NULL))
        return UBASE_ERR_ALLOC;
    upipe_blit_store_flow_def(upipe, flow_def);

    if (!flow_format_change)
        return UBASE_ERR_NONE;

    upipe_blit->macropixel = macropixel;
    upipe_blit->hsub = hsub;
    upipe_blit->vsub = vsub;
    upipe_blit->hsize = hsize;
    upipe_blit->vsize = vsize;
    upipe_blit->sar = sar;

    struct uchain *uchain;
    ulist_foreach (&upipe_blit->subs, uchain) {
        struct upipe_blit_sub *sub = upipe_blit_sub_from_uchain(uchain);
        upipe_blit_sub_provide_flow_format(upipe_blit_sub_to_upipe(sub));
    }
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a file source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_blit_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_blit_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_blit_free_output_proxy(upipe, request);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_blit_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow = va_arg(args, struct uref *);
            return upipe_blit_set_flow_def(upipe, flow);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_blit_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_blit_set_output(upipe, output);
        }
        case UPIPE_GET_SUB_MGR: {
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);
            return upipe_blit_get_sub_mgr(upipe, p);
        }
        case UPIPE_ITERATE_SUB: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_blit_iterate_sub(upipe, p);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_blit_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_blit_clean_sub_subs(upipe);
    upipe_blit_clean_output(upipe);
    upipe_blit_clean_urefcount(upipe);
    upipe_blit_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_blit_mgr = {
    .refcount = NULL,
    .signature = UPIPE_BLIT_SIGNATURE,

    .upipe_alloc = upipe_blit_alloc,
    .upipe_input = upipe_blit_input,
    .upipe_control = upipe_blit_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for blit pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_blit_mgr_alloc(void)
{
    return &upipe_blit_mgr;
}
