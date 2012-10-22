/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
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

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/uprobe.h>
#include <upipe/ulog.h>
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
#include <upipe/upipe_helper_linear_ubuf_mgr.h>
#include <upipe/upipe_helper_linear_output.h>
#include <upipe-modules/upipe_genaux.h>


/** upipe_genaux structure */ 
struct upipe_genaux {
    /** input flow */
    struct uref *input_flow;
    /** output flow */
    struct uref *output_flow;
    /** true if the flow definition has already been sent */
    bool output_flow_sent;

    /** output pipe */
    struct upipe *output;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;

    /** true if we have thrown the ready event */
    bool ready;

    /** refcount management structure */
    urefcount refcount;
    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_genaux, upipe);
UPIPE_HELPER_LINEAR_OUTPUT(upipe_genaux, output, output_flow, output_flow_sent);
UPIPE_HELPER_LINEAR_UBUF_MGR(upipe_genaux, ubuf_mgr);

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @return false if the buffer couldn't be accepted
 */
static bool _upipe_genaux_input(struct upipe *upipe, struct uref *uref)
{
    struct upipe_genaux *upipe_genaux = upipe_genaux_from_upipe(upipe);
    uint64_t systime = 0;
    int size;
    struct ubuf *dst;
    uint8_t *aux;

    if (!uref_clock_get_systime(uref, &systime)) {
        uref_free(uref);
        return false;
    }

    size = sizeof(uint64_t);
    dst = ubuf_block_alloc(upipe_genaux->ubuf_mgr, size);
    if (unlikely(dst == NULL)) {
        ulog_aerror(upipe->ulog);
        upipe_throw_aerror(upipe);
        return false;
    }
    ubuf_block_write(dst, 0, &size, &aux);
    upipe_genaux_hton64(aux, systime);
    ubuf_block_unmap(dst, 0, size);
    ubuf_free(uref_detach_ubuf(uref));
    uref_attach_ubuf(uref, dst);
    upipe_genaux_output(upipe, uref);
    return true;
}

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @return false if the buffer couldn't be accepted
 */
static bool upipe_genaux_input(struct upipe *upipe, struct uref *uref)
{
    // FIXME: check flow definition (only accept flows with planar y8)
    struct upipe_genaux *upipe_genaux = upipe_genaux_from_upipe(upipe);
    const char *flow, *def = NULL, *inflow = NULL; // hush gcc !

    if (unlikely(!uref_flow_get_name(uref, &flow))) {
       ulog_warning(upipe->ulog, "received a buffer outside of a flow");
       uref_free(uref);
       return false;
    }

    if (unlikely(uref_flow_get_def(uref, &def))) {
        if (upipe_genaux->input_flow) {
            ulog_warning(upipe->ulog, "received flow definition without delete first");
            uref_free(upipe_genaux->input_flow);
            upipe_genaux->input_flow = NULL;
        }

        upipe_genaux->input_flow = uref;
        //FIXME: allocate new flow
        upipe_genaux_set_flow_def(upipe, uref_dup(uref));

        ulog_debug(upipe->ulog, "flow definition for %s: %s", flow, def);
        return true;
    }

    if (unlikely(upipe_genaux->input_flow == NULL)) {
        ulog_warning(upipe->ulog, "pipe has no registered input flow");
        uref_free(uref);
        return false;
    }

    uref_flow_get_name(upipe_genaux->input_flow, &inflow);
    if (unlikely(strcmp(inflow, flow))) {
        ulog_warning(upipe->ulog, "received a buffer not matching the current flow");
        uref_free(uref);
        return false;
    }


    if (unlikely(uref_flow_get_delete(uref))) {
        uref_free(upipe_genaux->input_flow);
        upipe_genaux->input_flow = NULL;
        uref_free(uref);
        return true;
    }

    if (unlikely(uref->ubuf == NULL)) {
        uref_free(uref);
        return true;
    }

    return _upipe_genaux_input(upipe, uref);
}

/** @internal @This processes control commands on a file source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool _upipe_genaux_control(struct upipe *upipe, enum upipe_command command,
                               va_list args)
{
    if (likely(command == UPIPE_INPUT)) {
        struct uref *uref = va_arg(args, struct uref *);
        assert(uref != NULL);
        return upipe_genaux_input(upipe, uref);
    }
    switch (command) {
        // generic linear stuff
        case UPIPE_LINEAR_GET_UBUF_MGR: {
            struct ubuf_mgr **p = va_arg(args, struct ubuf_mgr **);
            return upipe_genaux_get_ubuf_mgr(upipe, p);
        }
        case UPIPE_LINEAR_SET_UBUF_MGR: {
            struct ubuf_mgr *ubuf_mgr = va_arg(args, struct ubuf_mgr *);
            return upipe_genaux_set_ubuf_mgr(upipe, ubuf_mgr);
        }
        case UPIPE_LINEAR_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_genaux_get_output(upipe, p);
        }
        case UPIPE_LINEAR_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_genaux_set_output(upipe, output);
        }
        default:
            return false;
    }
}

/** @internal @This processes control commands on a file source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_genaux_control(struct upipe *upipe, enum upipe_command command,
                               va_list args)
{
    struct upipe_genaux *upipe_genaux = upipe_genaux_from_upipe(upipe);
    int ret = _upipe_genaux_control(upipe, command, args);
   
    // FIXME - check something before setting ready !
    if (!upipe_genaux->ready) {
        upipe_genaux->ready = true;
        upipe_throw_ready(upipe);
    }
    return ret;
}

/** @internal @This allocates a genaux pipe.
 *
 * @param mgr common management structure
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_genaux_alloc(struct upipe_mgr *mgr)
{
    struct upipe_genaux *upipe_genaux = malloc(sizeof(struct upipe_genaux));
    if (unlikely(upipe_genaux == NULL)) return NULL;
    struct upipe *upipe = upipe_genaux_to_upipe(upipe_genaux);
    upipe->mgr = mgr; /* do not increment refcount as mgr is static */
    upipe->signature = UPIPE_GENAUX_SIGNATURE;
    urefcount_init(&upipe_genaux->refcount);
    upipe_genaux_init_ubuf_mgr(upipe);
    upipe_genaux_init_output(upipe);
    upipe_genaux->input_flow = NULL;

    upipe_genaux->ready = false;
    return upipe;
}

/** @This increments the reference count of a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_genaux_use(struct upipe *upipe)
{
    struct upipe_genaux *upipe_genaux = upipe_genaux_from_upipe(upipe);
    urefcount_use(&upipe_genaux->refcount);
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_genaux_release(struct upipe *upipe)
{
    struct upipe_genaux *upipe_genaux = upipe_genaux_from_upipe(upipe);
    if (unlikely(urefcount_release(&upipe_genaux->refcount))) {
        ulog_debug(upipe->ulog, "releasing pipe %p", upipe);
        upipe_genaux_clean_output(upipe);
        upipe_genaux_clean_ubuf_mgr(upipe);
        if (upipe_genaux->input_flow) {
            uref_free(upipe_genaux->input_flow);
        }
        upipe_clean(upipe);
        urefcount_clean(&upipe_genaux->refcount);
        free(upipe_genaux);
    }
}

static struct upipe_mgr upipe_genaux_mgr = {
    .upipe_alloc = upipe_genaux_alloc,
    .upipe_control = upipe_genaux_control,
    .upipe_release = upipe_genaux_release,
    .upipe_use = upipe_genaux_use,

    .upipe_mgr_use = NULL,
    .upipe_mgr_release = NULL
};

/** @This returns the management structure for genaux pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_genaux_mgr_alloc(void)
{
    return &upipe_genaux_mgr;
}
