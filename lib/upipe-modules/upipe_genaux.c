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

/** upipe_genaux structure */ 
struct upipe_genaux {
    /** refcount management structure */
    struct urefcount urefcount;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** output pipe */
    struct upipe *output;
    /** flow_definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /** get attr */
    int (*getattr) (struct uref *, uint64_t *);

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_genaux, upipe, UPIPE_GENAUX_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_genaux, urefcount, upipe_genaux_free)
UPIPE_HELPER_VOID(upipe_genaux);
UPIPE_HELPER_UBUF_MGR(upipe_genaux, ubuf_mgr, flow_def);
UPIPE_HELPER_OUTPUT(upipe_genaux, output, flow_def, flow_def_sent);

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_genaux_input(struct upipe *upipe, struct uref *uref,
                               struct upump **upump_p)
{
    struct upipe_genaux *upipe_genaux = upipe_genaux_from_upipe(upipe);
    uint64_t systime = 0;
    int size;
    struct ubuf *dst;
    uint8_t *aux;

    if (!ubase_check(upipe_genaux_check_ubuf_mgr(upipe)) ||
        !ubase_check(upipe_genaux->getattr(uref, &systime))) {
        uref_free(uref);
        return;
    }

    size = sizeof(uint64_t);
    dst = ubuf_block_alloc(upipe_genaux->ubuf_mgr, size);
    if (unlikely(dst == NULL)) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    ubuf_block_write(dst, 0, &size, &aux);
    upipe_genaux_hton64(aux, systime);
    ubuf_block_unmap(dst, 0);
    uref_attach_ubuf(uref, dst);
    upipe_genaux_output(upipe, uref, upump_p);
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
    upipe_genaux_store_flow_def(upipe, flow_def_dup);
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
        case UPIPE_ATTACH_UBUF_MGR:
            return upipe_genaux_attach_ubuf_mgr(upipe);

        case UPIPE_AMEND_FLOW_FORMAT:
            return UBASE_ERR_NONE;
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_genaux_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_genaux_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_genaux_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_genaux_set_output(upipe, output);
        }

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
    upipe_dbg_va(upipe, "releasing pipe %p", upipe);
    upipe_throw_dead(upipe);

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
