/*
 * Copyright (C) 2012-2014 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
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
 * @short Upipe module - generates auxiliary blocks from k.systime
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uclock.h>
#include <upipe/uref_clock.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_input.h>
#include <upipe-modules/upipe_genaux.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <assert.h>

/** @hidden */
static bool upipe_genaux_handle(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p);
/** @hidden */
static int upipe_genaux_check(struct upipe *upipe, struct uref *flow_format);

/** upipe_genaux structure */ 
struct upipe_genaux {
    /** refcount management structure */
    struct urefcount urefcount;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** output pipe */
    struct upipe *output;
    /** flow_definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** temporary uref storage (used during urequest) */
    struct uchain urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers (used during urequest) */
    struct uchain blockers;

    /** get attr */
    int (*getattr) (struct uref *, uint64_t *);

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_genaux, upipe, UPIPE_GENAUX_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_genaux, urefcount, upipe_genaux_free);
UPIPE_HELPER_VOID(upipe_genaux);
UPIPE_HELPER_OUTPUT(upipe_genaux, output, flow_def, output_state, request_list);
UPIPE_HELPER_UBUF_MGR(upipe_genaux, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_genaux_check,
                      upipe_genaux_register_output_request,
                      upipe_genaux_unregister_output_request)
UPIPE_HELPER_INPUT(upipe_genaux, urefs, nb_urefs, max_urefs, blockers, upipe_genaux_handle)

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 * @return false if the input must be blocked
 */
static bool upipe_genaux_handle(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p)
{
    struct upipe_genaux *upipe_genaux = upipe_genaux_from_upipe(upipe);
    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        upipe_genaux_store_flow_def(upipe, NULL);
        upipe_genaux_require_ubuf_mgr(upipe, uref);
        return true;
    }

    if (upipe_genaux->flow_def == NULL)
        return false;

    uint64_t systime = 0;
    int size;
    struct ubuf *dst;
    uint8_t *aux;

    if (!ubase_check(upipe_genaux->getattr(uref, &systime))) {
        uref_free(uref);
        return true;
    }

    size = sizeof(uint64_t);
    dst = ubuf_block_alloc(upipe_genaux->ubuf_mgr, size);
    if (unlikely(dst == NULL)) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return true;
    }
    ubuf_block_write(dst, 0, &size, &aux);
    upipe_genaux_hton64(aux, systime);
    ubuf_block_unmap(dst, 0);
    uref_attach_ubuf(uref, dst);
    upipe_genaux_output(upipe, uref, upump_p);
    return true;
}

/** @internal @This inputs data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_genaux_input(struct upipe *upipe, struct uref *uref,
                               struct upump **upump_p)
{
    if (!upipe_genaux_check_input(upipe)) {
        upipe_genaux_hold_input(upipe, uref);
        upipe_genaux_block_input(upipe, upump_p);
    } else if (!upipe_genaux_handle(upipe, uref, upump_p)) {
        upipe_genaux_hold_input(upipe, uref);
        upipe_genaux_block_input(upipe, upump_p);
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    }
}

/** @internal @This checks if the input may start.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_genaux_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_genaux *upipe_genaux = upipe_genaux_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_genaux_store_flow_def(upipe, flow_format);

    if (upipe_genaux->flow_def == NULL)
        return UBASE_ERR_NONE;

    bool was_buffered = !upipe_genaux_check_input(upipe);
    upipe_genaux_output_input(upipe);
    upipe_genaux_unblock_input(upipe);
    if (was_buffered && upipe_genaux_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_genaux_input. */
        upipe_release(upipe);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_genaux_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    struct uref *flow_def_dup;
    if ((flow_def_dup = uref_dup(flow_def)) == NULL)
        return UBASE_ERR_ALLOC;
    if (unlikely(!ubase_check(uref_flow_set_def(flow_def_dup, "block.aux."))))
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
    upipe_input(upipe, flow_def_dup, NULL);
    return UBASE_ERR_NONE;
}

/** @This sets the get callback to fetch the u64 opaque with.
 *
 * @param upipe description structure of the pipe
 * @param get callback
 * @return an error code
 */
static inline int _upipe_genaux_set_getattr(struct upipe *upipe,
                            int (*get)(struct uref*, uint64_t*))
{
    struct upipe_genaux *upipe_genaux = upipe_genaux_from_upipe(upipe);
    if (unlikely(!get)) {
        return UBASE_ERR_INVALID;
    }

    upipe_genaux->getattr = get;
    return UBASE_ERR_NONE;
}

/** @This gets the get callback to fetch the u64 opaque with.
 *
 * @param upipe description structure of the pipe
 * @param get callback pointer
 * @return an error code
 */
static inline int _upipe_genaux_get_getattr(struct upipe *upipe,
                            int (**get)(struct uref*, uint64_t*))
{
    struct upipe_genaux *upipe_genaux = upipe_genaux_from_upipe(upipe);
    if (unlikely(!get)) {
        return UBASE_ERR_INVALID;
    }

    *get = upipe_genaux->getattr;
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a genaux pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_genaux_control(struct upipe *upipe,
                                int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return upipe_throw_provide_request(upipe, request);
            return upipe_genaux_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return UBASE_ERR_NONE;
            return upipe_genaux_free_output_proxy(upipe, request);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_genaux_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_genaux_control_output(upipe, command, args);
        case UPIPE_GENAUX_SET_GETATTR: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_GENAUX_SIGNATURE)
            return _upipe_genaux_set_getattr(upipe,
                   va_arg(args, int (*)(struct uref*, uint64_t*)));
        }
        case UPIPE_GENAUX_GET_GETATTR: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_GENAUX_SIGNATURE)
            return _upipe_genaux_get_getattr(upipe,
                   va_arg(args, int (**)(struct uref*, uint64_t*)));
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This allocates a genaux pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_genaux_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_genaux_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_genaux *upipe_genaux = upipe_genaux_from_upipe(upipe);
    upipe_genaux_init_urefcount(upipe);
    upipe_genaux_init_ubuf_mgr(upipe);
    upipe_genaux_init_output(upipe);
    upipe_genaux_init_input(upipe);
    upipe_genaux->getattr = uref_clock_get_cr_sys;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_genaux_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_genaux_clean_input(upipe);
    upipe_genaux_clean_ubuf_mgr(upipe);
    upipe_genaux_clean_output(upipe);
    upipe_genaux_clean_urefcount(upipe);
    upipe_genaux_free_void(upipe);
}

static struct upipe_mgr upipe_genaux_mgr = {
    .refcount = NULL,
    .signature = UPIPE_GENAUX_SIGNATURE,

    .upipe_alloc = upipe_genaux_alloc,
    .upipe_input = upipe_genaux_input,
    .upipe_control = upipe_genaux_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for genaux pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_genaux_mgr_alloc(void)
{
    return &upipe_genaux_mgr;
}
