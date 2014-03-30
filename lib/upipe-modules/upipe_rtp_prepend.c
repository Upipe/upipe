/*
 * Copyright (C) 2013-2014 OpenHeadend S.A.R.L.
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
 * @short Upipe rtp module to prepend rtp header to uref blocks
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uclock.h>
#include <upipe/uref_clock.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
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
#include <upipe-modules/upipe_rtp_prepend.h>

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

#include <bitstream/ietf/rtp.h>
#include <bitstream/ietf/rtp3551.h>

#define EXPECTED_FLOW_DEF "block."
#define OUT_FLOW "block.rtp."

#define DEFAULT_RATE 90000 /* (90kHz, see rfc 2250 and 3551) */

/** upipe_rtp_prepend structure */ 
struct upipe_rtp_prepend {
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

    /** rtp sequence number */
    uint16_t seqnum;
    /** timestamp clockrate */
    uint32_t clockrate;
    /** rtp type */ 
    uint8_t type;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_rtp_prepend, upipe, UPIPE_RTP_PREPEND_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_rtp_prepend, urefcount, upipe_rtp_prepend_free)
UPIPE_HELPER_VOID(upipe_rtp_prepend);
UPIPE_HELPER_UBUF_MGR(upipe_rtp_prepend, ubuf_mgr, flow_def);
UPIPE_HELPER_OUTPUT(upipe_rtp_prepend, output, flow_def, flow_def_sent);

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_rtp_prepend_input(struct upipe *upipe, struct uref *uref,
                                    struct upump **upump_p)
{
    struct upipe_rtp_prepend *upipe_rtp_prepend = upipe_rtp_prepend_from_upipe(upipe);
    struct ubuf *header, *payload;
    uint8_t *buf = NULL;
    uint64_t cr = 0;
    uint32_t ts;
    lldiv_t div;
    int size = -1;

    /* check ubuf manager */
    if (unlikely(!ubase_check(upipe_rtp_prepend_check_ubuf_mgr(upipe)))) {
        uref_free(uref);
        return;
    }

    /* timestamp (synced to program clock ref, fallback to system clock ref) */
    if (unlikely(!ubase_check(uref_clock_get_cr_prog(uref, &cr)))) {
        uref_clock_get_cr_sys(uref, &cr);
    }
    div = lldiv(cr, UCLOCK_FREQ);
    ts = div.quot * upipe_rtp_prepend->clockrate
         + ((uint64_t)div.rem * upipe_rtp_prepend->clockrate)/UCLOCK_FREQ;
    
    /* alloc header */
    header = ubuf_block_alloc(upipe_rtp_prepend->ubuf_mgr, RTP_HEADER_SIZE);
    if (unlikely(!header)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        uref_free(uref);
        return;
    }

    /* write header */
    ubuf_block_write(header, 0, &size, &buf);
    memset(buf, 0, RTP_HEADER_SIZE);
    rtp_set_hdr(buf);
    rtp_set_type(buf, upipe_rtp_prepend->type);
    rtp_set_seqnum(buf, upipe_rtp_prepend->seqnum);
    rtp_set_timestamp(buf, ts);
    ubuf_block_unmap(header, 0);
    upipe_rtp_prepend->seqnum++;

    /* append payload (current ubuf) to header to form segmented ubuf */
    payload = uref_detach_ubuf(uref);
    if (unlikely(!ubase_check(ubuf_block_append(header, payload)))) {
        upipe_warn(upipe, "could not append payload to header");
        ubuf_free(header);
        ubuf_free(payload);
        uref_free(uref);
        return;
    }
    uref_attach_ubuf(uref, header);

    upipe_rtp_prepend_output(upipe, uref, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static enum ubase_err upipe_rtp_prepend_set_flow_def(struct upipe *upipe,
                                                     struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def))
    if (ubase_ncmp(def, EXPECTED_FLOW_DEF))
        return UBASE_ERR_INVALID;
    struct uref *flow_def_dup;
    if ((flow_def_dup = uref_dup(flow_def)) == NULL)
        return UBASE_ERR_ALLOC;
    if (!ubase_check(uref_flow_set_def_va(flow_def_dup, OUT_FLOW"%s",
                              def + strlen(EXPECTED_FLOW_DEF)))) {
        uref_free(flow_def_dup);
        return UBASE_ERR_ALLOC;
    }
    upipe_rtp_prepend_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This sets the rtp type and clock rate.
 *
 * @param upipe description structure of the pipe
 * @param type rtp payload type
 * @param clockrate rtp timestamp and clock rate (optional, set
 * according to rfc 3551 if null)
 * @return an error code
 */
static enum ubase_err _upipe_rtp_prepend_set_type(struct upipe *upipe,
                                                  uint8_t type,
                                                  uint32_t clockrate)
{
    struct upipe_rtp_prepend *upipe_rtp_prepend =
        upipe_rtp_prepend_from_upipe(upipe);
    type = 0x7f & type;
    if (!clockrate) {
        clockrate = rtp_3551_get_clock_rate(type);
    }
    if (unlikely(!clockrate)) {
        /* fallback to default rate in case of unspecified and unknown rate */
        clockrate = DEFAULT_RATE;
    }
    upipe_rtp_prepend->clockrate = clockrate;
    upipe_rtp_prepend->type = type;
    return UBASE_ERR_NONE;
}

/** @internal @This returns the configured RTP type.
 *
 * @param upipe description structure of the pipe
 * @param type_p rtp type
 * @param rate_p rtp timestamp clock rate
 * @return an error code
 */
static enum ubase_err _upipe_rtp_prepend_get_type(struct upipe *upipe,
                                                  uint8_t *type,
                                                  uint32_t *clockrate)
{
    struct upipe_rtp_prepend *upipe_rtp_prepend =
                       upipe_rtp_prepend_from_upipe(upipe);
    if (type) {
        *type = upipe_rtp_prepend->type;
    }
    if (clockrate) {
        *clockrate = upipe_rtp_prepend->clockrate;
    }
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a rtp_prepend pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static enum ubase_err upipe_rtp_prepend_control(struct upipe *upipe,
                                                int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UBUF_MGR:
            return upipe_rtp_prepend_attach_ubuf_mgr(upipe);

        case UPIPE_AMEND_FLOW_FORMAT: {
            struct uref *flow_format = va_arg(args, struct uref *);
            return upipe_throw_new_flow_format(upipe, flow_format, NULL);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_rtp_prepend_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_rtp_prepend_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_rtp_prepend_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_rtp_prepend_set_output(upipe, output);
        }

        case UPIPE_RTP_PREPEND_GET_TYPE: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_RTP_PREPEND_SIGNATURE);
            uint8_t *type_p = va_arg(args, uint8_t *);
            uint32_t *rate_p = va_arg(args, uint32_t *);
            return _upipe_rtp_prepend_get_type(upipe, type_p, rate_p);
        }
        case UPIPE_RTP_PREPEND_SET_TYPE: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_RTP_PREPEND_SIGNATURE);
            uint8_t type = (uint8_t) va_arg(args, int);
            uint32_t rate = va_arg(args, uint32_t);
            return _upipe_rtp_prepend_set_type(upipe, type, rate);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This allocates a rtp_prepend pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_rtp_prepend_alloc(struct upipe_mgr *mgr,
                                             struct uprobe *uprobe,
                                             uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_rtp_prepend_alloc_void(mgr, uprobe, signature,
                                                       args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_rtp_prepend *upipe_rtp_prepend =
        upipe_rtp_prepend_from_upipe(upipe);
    upipe_rtp_prepend_init_urefcount(upipe);
    upipe_rtp_prepend_init_ubuf_mgr(upipe);
    upipe_rtp_prepend_init_output(upipe);

    upipe_rtp_prepend->seqnum = 0; /* FIXME random init ?*/

    /* transport TS by default (FIXME) */
    _upipe_rtp_prepend_set_type(upipe, RTP_TYPE_TS, 0);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_rtp_prepend_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_rtp_prepend_clean_ubuf_mgr(upipe);
    upipe_rtp_prepend_clean_output(upipe);
    upipe_rtp_prepend_clean_urefcount(upipe);
    upipe_rtp_prepend_free_void(upipe);
}

static struct upipe_mgr upipe_rtp_prepend_mgr = {
    .refcount = NULL,
    .signature = UPIPE_RTP_PREPEND_SIGNATURE,

    .upipe_alloc = upipe_rtp_prepend_alloc,
    .upipe_input = upipe_rtp_prepend_input,
    .upipe_control = upipe_rtp_prepend_control
};

/** @This returns the management structure for rtp_prepend pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_rtp_prepend_mgr_alloc(void)
{
    return &upipe_rtp_prepend_mgr;
}
