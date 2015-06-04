/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
 *
 * Authors: Sebastien Gougelet
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
#include <upipe/uref_clock.h>
#include <upipe/ubuf_pic.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe/upipe_helper_subpipe.h>

#include <upipe/upipe_helper_output.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <time.h>
#include <upipe-modules/upipe_blit.h>
/** only accept pics */
#define EXPECTED_FLOW_DEF "pic."

/** @file
 * @short Upipe module blit
 */

/** upipe_blit structure to do blit */ 
struct upipe_blit {
    /** refcount management structure */
    struct urefcount urefcount;

    /** output pipe */
    struct upipe *output;
    /** flow_definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** public upipe structure */
    struct upipe upipe;

    struct uchain subs;
    struct upipe_mgr sub_mgr;
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
    
    /** received uref */
    struct uref *uref;

    /** input flow definition packet */
    struct uref *flow_def;

    /** public upipe structure */
    struct upipe upipe;
    /** position of the picture in the main picture */
    int H;
    int V;
};

UPIPE_HELPER_UPIPE(upipe_blit_sub, upipe, UPIPE_BLIT_INPUT_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_blit_sub, urefcount, upipe_blit_sub_dead)
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
    struct upipe *upipe = upipe_blit_sub_alloc_void(mgr,
                                     uprobe, signature, args);
    if (unlikely(upipe == NULL)) {
        printf("sub_alloc failed\n");
        return NULL;
    }

    struct upipe_blit_sub *upipe_blit_sub =
        upipe_blit_sub_from_upipe(upipe);
    upipe_blit_sub_init_urefcount(upipe);
    upipe_blit_sub_init_sub(upipe);
    upipe_blit_sub->uref = NULL;
    upipe_blit_sub->flow_def = NULL;
    upipe_blit_sub->H=0;
    upipe_blit_sub->V=0;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This make the blit.
*
* @param hoset horizontal position of the picture to copy
* @param voset vertical position of the picture to copy
* @param signature signature of the pipe allocator
* @param uref uref structure
* @param urefsub second uref structure containing the picture to copy
*/
static void copy(int hoset, int voset, struct uref *uref, struct uref *urefsub)
{
    int x, y;
    size_t h, v, h2, v2;
    uint8_t macropixel, macropixel2;
    const char* cp = NULL;
    uint8_t *buf = NULL,  *aout = NULL, *out=NULL;
    const uint8_t *buf2 = NULL, *asub = NULL;
    uref_pic_size(uref, &h, &v, &macropixel);
    uref_pic_size(urefsub, &h2, &v2, &macropixel2);
    int sh1 = (int)h, sh2 = (int)h2, sv1 = (int)v, sv2 = (int)v2;
    if ((sh2 + hoset) <= sh1 && (sv2 + voset) <= sv1 &&
                uref_pic_flow_compare_format(uref, urefsub)) {

        bool transpref = false;
        while (ubase_check(uref_pic_plane_iterate(uref, &cp)) && cp != NULL) {
            if (!strcmp(cp,"a8")) {
                transpref = true;
            }
        }
        bool transpsub = false;
        while (ubase_check(uref_pic_plane_iterate(uref, &cp)) && cp != NULL) {
            if (!strcmp(cp,"a8")) {
                transpsub = true;
            }
        } 
        if (!transpref && !transpsub) {
            while (ubase_check(uref_pic_plane_iterate(urefsub, &cp)) && 
                    cp != NULL) {
                size_t stride, stride2;
                uint8_t hsub, vsub, hsub2, vsub2;
                uint8_t macropixel_size, macropixel_size2;
                uref_pic_plane_size(urefsub, cp, &stride2, &hsub2, &vsub2,
                                        &macropixel_size2);
                uref_pic_plane_size(uref, cp, &stride, &hsub, &vsub,
                                        &macropixel_size);
                int hoctets = h2 * macropixel_size2 / hsub2 / macropixel2;
                uref_pic_plane_read(urefsub, cp, 0, 0, -1, -1, &buf2);
                uref_pic_plane_write(uref, cp, hoset, voset, sh2, sv2, &buf);
                for (y = 0; y < v2 / vsub2; y++) {
                    for (x = 0; x < hoctets; x++) {
                        buf[x] = buf2[x];
                    }
                    buf += stride;
                    buf2 += stride2;
                }
                uref_pic_plane_unmap(urefsub, cp, 0, 0, -1, -1);
                uref_pic_plane_unmap(uref, cp, hoset, voset, sh2, sv2);
                }
        } else {
            size_t stridea, stridea2;
            uint8_t hsuba, vsuba, hsuba2, vsuba2;
            uint8_t macropixel_sizea, macropixel_sizea2;
            uref_pic_plane_size(uref, "a8", &stridea, &hsuba, &vsuba,
                                        &macropixel_sizea);
            uref_pic_plane_size(urefsub, "a8", &stridea2, &hsuba2, &vsuba2,
                                        &macropixel_sizea2);
            uref_pic_plane_write(uref, "a8", hoset, voset, sh2, sv2, &aout);
            int hoctetsa = h2 * macropixel_sizea2 / hsuba2 / macropixel2;
            for (y = 0; y < v2 / vsuba2; y++) {
                for (x = 0; x < hoctetsa; x++) {
                    aout[x] = 0;
                }
                aout += stridea;
            }
            uref_pic_plane_unmap(uref, "a8", hoset, voset, sh2, sv2);
            uref_pic_plane_read(urefsub, "a8", 0, 0, -1, -1, &asub);            

            while (ubase_check(uref_pic_plane_iterate(urefsub, &cp)) && 
                    cp != NULL) {
                size_t stride, stride2;
                int r, b, ha, compteur = 0;
                int c = strcmp(cp, "a8");
                if (c){
                    uint8_t hsub, vsub, hsub2, vsub2;
                    uint8_t macropixel_size, macropixel_size2;                
                    uref_pic_plane_size(urefsub, cp, &stride2, &hsub2, &vsub2,
                                        &macropixel_size2);
                    uref_pic_plane_size(uref, cp, &stride, &hsub, &vsub,
                                        &macropixel_size);
                    int hoctets = h2 * macropixel_size2 / hsub2 / macropixel2;            
                    uref_pic_plane_read(urefsub, cp, 0, 0, -1, -1, &buf2);
                    uref_pic_plane_write(uref, cp, hoset, voset, sh2, sv2, &out);
                    uref_pic_plane_size(uref, "a8", &stridea, &hsuba, &vsuba,
                                            &macropixel_sizea);
                    uref_pic_plane_size(urefsub, "a8", &stridea2, &hsuba2, &vsuba2,
                                            &macropixel_sizea2);

                    int hoctetsa = h2 * macropixel_sizea2 / hsuba2 / macropixel2;
                    for (y = 0; y < v2 / vsub2; y++) {
                        for (x = 0; x < hoctets; x++) {
                            b = 255-asub[x+ha];
                            r = ((b)*buf2[x]+(1-b)*out[x]);
                            out[x] = r;
                            ha = ha + hoctetsa / hoctets;
                        }
                        ha = 0;
                        buf2 += stride2;
                        out += stride;
                        asub += vsub2 * stridea2;
                        compteur += 1;
                    }
                    asub -= compteur * vsub2 * stridea2;
                    uref_pic_plane_unmap(urefsub, cp, 0, 0, -1, -1);
                    uref_pic_plane_unmap(uref, cp, hoset, voset, sh2, sv2);
                }
            }
            uref_pic_plane_unmap(urefsub, "a8", 0, 0, -1, -1);
        }      
    }
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
    struct upipe_blit_sub *upipe_blit_sub =
                                upipe_blit_sub_from_upipe(upipe);

    if (unlikely(!uref->ubuf)) {
        upipe_warn_va(upipe, "received empty packet");
        uref_free(uref);
        return;
    }

    if (upipe_blit_sub->uref != NULL) {
        uref_free(upipe_blit_sub->uref);
    }
    upipe_blit_sub->uref = uref;
}

/** @internal @This sets the input flow definition.
*
* @param upipe description structure of the pipe
* @param flow_def flow definition packet
* @return an error code
*/
static enum ubase_err upipe_blit_sub_set_flow_def(struct upipe *upipe,
                                                       struct uref *flow_def)
{
    struct upipe_blit_sub *upipe_blit_sub =
           upipe_blit_sub_from_upipe(upipe);

    if (flow_def == NULL) {
        return UBASE_ERR_INVALID;
    }

    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))

    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    if (unlikely(upipe_blit_sub->flow_def)) {
        uref_free(upipe_blit_sub->flow_def);
    }

    upipe_blit_sub->flow_def = flow_def_dup;
    
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
static int _upipe_blit_sub_control(struct upipe *upipe,
                                       int command, va_list args)
{
    struct upipe_blit_sub *sub = upipe_blit_sub_from_upipe(upipe);
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_blit_sub_set_flow_def(upipe, flow_def);
        }
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_blit_sub_get_super(upipe, p);
        }
        case UPIPE_SUB_SET_POSITIONH: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_BLIT_INPUT_SIGNATURE);
            sub->H = va_arg(args, int);
            return UBASE_ERR_NONE;
        }
        case UPIPE_SUB_SET_POSITIONV: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_BLIT_INPUT_SIGNATURE);
            sub->V = va_arg(args, int);
            return UBASE_ERR_NONE;
        }
        case UPIPE_SUB_GET_POSITIONH: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_BLIT_INPUT_SIGNATURE);
            *va_arg(args, int *) = sub->H;
            return UBASE_ERR_NONE;
        }
        case UPIPE_SUB_GET_POSITIONV: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_BLIT_INPUT_SIGNATURE);
            *va_arg(args, int *) = sub->V;
            return UBASE_ERR_NONE;
        }
        case UPIPE_SUB_SET_POSITION: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_BLIT_INPUT_SIGNATURE);
            sub->H = va_arg(args, int);
            sub->V = va_arg(args, int);
            return UBASE_ERR_NONE;
        }
        case UPIPE_SUB_GET_POSITION: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_BLIT_INPUT_SIGNATURE);
            *va_arg(args, int *) = sub->H;
            *va_arg(args, int *) = sub->V;
            return UBASE_ERR_NONE;
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
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
 
    struct uchain *uchain_sub;
    uint8_t *buf = NULL;
 
    if (unlikely(!ubase_check(uref_pic_plane_write(uref, "b8g8r8a8", 0, 0, 
                                                -1, -1, &buf)))) {
        struct ubuf *ubuf = ubuf_pic_copy(uref->ubuf->mgr,
                                            uref->ubuf,0, 0, -1, -1);
        if (unlikely(!ubuf)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            uref_free(uref);
        return;

        }
        uref_attach_ubuf(uref, ubuf);
        if (unlikely(!ubase_check(uref_pic_plane_write(uref, "b8g8r8a8", 0, 0, 
                                             -1, -1, &buf)))) {
            upipe_warn_va(upipe, "could not map ref packet");
            uref_free(uref);
            return;
        }

    }

    uref_pic_plane_unmap(uref, "b8g8r8a8", 0, 0, -1, -1);
    ulist_foreach(&upipe_blit->subs, uchain_sub){
     struct upipe_blit_sub *sub = upipe_blit_sub_from_uchain(uchain_sub);
     if (likely(sub->uref != NULL)) {
        copy(sub->H, sub->V, uref, sub->uref);
        } else {
            printf("sub->uref=NULL\n");     
        }
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

    UBASE_RETURN(uref_flow_match_def(flow_def, "pic."))
    flow_def = uref_dup(flow_def);

    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    upipe_blit_store_flow_def(upipe, flow_def);
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
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_blit_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow = va_arg(args, struct uref *);
            return upipe_blit_set_flow_def(upipe, flow);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This marks an input subpipe as dead.
*
* @param upipe description structure of the pipe
*/
static void upipe_blit_sub_dead(struct upipe *upipe)
{
    struct upipe_blit_sub *upipe_blit_sub =
                                upipe_blit_sub_from_upipe(upipe);

    if (likely(upipe_blit_sub->flow_def)) {
        uref_free(upipe_blit_sub->flow_def);
    }

    upipe_throw_dead(upipe);
    uref_free(upipe_blit_sub->uref);
    uref_free(upipe_blit_sub->flow_def);
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
    sub_mgr->signature = UPIPE_BLIT_INPUT_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_blit_sub_alloc;
    sub_mgr->upipe_input = upipe_blit_sub_input;
    sub_mgr->upipe_control = _upipe_blit_sub_control;
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
    struct upipe *upipe = upipe_blit_alloc_void(mgr, uprobe, signature,
                                                 args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_blit_init_sub_mgr(upipe);
    upipe_blit_init_sub_subs(upipe);
    upipe_blit_init_urefcount(upipe);
    upipe_blit_init_output(upipe);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_blit_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);
    struct upipe_blit *upipe_blit = upipe_blit_from_upipe(upipe);

    upipe_mgr_release(&upipe_blit->sub_mgr);
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
