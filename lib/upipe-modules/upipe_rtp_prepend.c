/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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

#define EXPECTED_FLOW "block."
#define OUT_FLOW "block.rtp."

#define DEFAULT_FREQ 90000 /* (90kHz, see rfc 2250 and 3551) */

/** upipe_rtp_prepend structure */ 
struct upipe_rtp_prepend {
    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** output pipe */
    struct upipe *output;
    /** flow_definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /* rtp sequence number */
    uint16_t seqnum;
    /* timestamp freq */
    unsigned int freq;
    uint8_t type;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_rtp_prepend, upipe);
UPIPE_HELPER_UBUF_MGR(upipe_rtp_prepend, ubuf_mgr);
UPIPE_HELPER_OUTPUT(upipe_rtp_prepend, output, flow_def, flow_def_sent);

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void _upipe_rtp_prepend_input(struct upipe *upipe, struct uref *uref,
                                struct upump *upump)
{
    struct upipe_rtp_prepend *upipe_rtp_prepend = upipe_rtp_prepend_from_upipe(upipe);
    struct ubuf *header, *payload;
    uint8_t *buf = NULL;
    uint64_t dts = 0;
    uint32_t ts;
    lldiv_t div;
    int size = -1;

    /* timestamp (synced to dts, fallback to systime) */
    if (unlikely(!uref_clock_get_dts(uref, &dts))) {
        uref_clock_get_systime(uref, &dts);
    }
    div = lldiv(dts, UCLOCK_FREQ);
    ts = div.quot * upipe_rtp_prepend->freq
         + ((uint64_t)div.rem * upipe_rtp_prepend->freq)/UCLOCK_FREQ;
    
    /* alloc header */
    header = ubuf_block_alloc(upipe_rtp_prepend->ubuf_mgr, RTP_HEADER_SIZE);
    if (unlikely(!header)) {
        upipe_throw_aerror(upipe);
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
    if (unlikely(!ubuf_block_append(header, payload))) {
        upipe_warn(upipe, "could not append payload to header");
        ubuf_free(header);
        ubuf_free(payload);
        uref_free(uref);
        return;
    }
    uref_attach_ubuf(uref, header);

    upipe_rtp_prepend_output(upipe, uref, upump);
}

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_rtp_prepend_input(struct upipe *upipe, struct uref *uref,
                               struct upump *upump)
{
    struct upipe_rtp_prepend *upipe_rtp_prepend = upipe_rtp_prepend_from_upipe(upipe);
    const char *def;
    char *out_def = NULL;

    if (unlikely(uref_flow_get_def(uref, &def))) {
        if (unlikely(ubase_ncmp(def, EXPECTED_FLOW))) {
            upipe_throw_flow_def_error(upipe, uref);
            uref_free(uref);
            return;
        }
        upipe_dbg_va(upipe, "flow definition %s", def);
        asprintf(&out_def, OUT_FLOW"%s", def+strlen(EXPECTED_FLOW));
        if (unlikely(!out_def)) {
            upipe_throw_aerror(upipe);
            uref_free(uref);
            return;
        }
        uref_flow_set_def(uref, out_def);
        free(out_def);
        upipe_rtp_prepend_store_flow_def(upipe, uref);
        return;
    }

    if (unlikely(uref_flow_get_end(uref))) {
        uref_free(uref);
        upipe_throw_need_input(upipe);
        return;
    }

    if (unlikely(upipe_rtp_prepend->flow_def == NULL)) {
        upipe_throw_flow_def_error(upipe, uref);
        uref_free(uref);
        return;
    }

    /* check ubuf manager */
    if (unlikely(!upipe_rtp_prepend->ubuf_mgr)) {
        upipe_throw_need_ubuf_mgr(upipe, upipe_rtp_prepend->flow_def);
        if (unlikely(!upipe_rtp_prepend->ubuf_mgr)) {
            upipe_warn(upipe, "ubuf_mgr not set !");
            uref_free(uref);
            return;
        }
    }

    if (unlikely(!uref->ubuf)) {
        upipe_warn(upipe, "dropping empty uref");
        uref_free(uref);
        return;
    }

    _upipe_rtp_prepend_input(upipe, uref, upump);
}

/** @internal @This processes control commands on a rtp_prepend pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_rtp_prepend_control(struct upipe *upipe,
                                 enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_GET_UBUF_MGR: {
            struct ubuf_mgr **p = va_arg(args, struct ubuf_mgr **);
            return upipe_rtp_prepend_get_ubuf_mgr(upipe, p);
        }
        case UPIPE_SET_UBUF_MGR: {
            struct ubuf_mgr *ubuf_mgr = va_arg(args, struct ubuf_mgr *);
            return upipe_rtp_prepend_set_ubuf_mgr(upipe, ubuf_mgr);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_rtp_prepend_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_rtp_prepend_set_output(upipe, output);
        }

        default:
            return false;
    }
}

/** @internal @This allocates a rtp_prepend pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_rtp_prepend_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe)
{
    struct upipe_rtp_prepend *upipe_rtp_prepend = malloc(sizeof(struct upipe_rtp_prepend));
    if (unlikely(upipe_rtp_prepend == NULL))
        return NULL;
    struct upipe *upipe = upipe_rtp_prepend_to_upipe(upipe_rtp_prepend);
    upipe_init(upipe, mgr, uprobe);
    upipe_rtp_prepend_init_ubuf_mgr(upipe);
    upipe_rtp_prepend_init_output(upipe);

    upipe_rtp_prepend->seqnum = 0; /* FIXME random init ?*/
    upipe_rtp_prepend->type = RTP_TYPE_TS; /* transport TS by default */
    upipe_rtp_prepend->freq = DEFAULT_FREQ;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_rtp_prepend_free(struct upipe *upipe)
{
    struct upipe_rtp_prepend *upipe_rtp_prepend = upipe_rtp_prepend_from_upipe(upipe);
    upipe_dbg_va(upipe, "releasing pipe %p", upipe);
    upipe_throw_dead(upipe);

    upipe_rtp_prepend_clean_ubuf_mgr(upipe);
    upipe_rtp_prepend_clean_output(upipe);

    upipe_clean(upipe);
    free(upipe_rtp_prepend);
}

static struct upipe_mgr upipe_rtp_prepend_mgr = {
    .signature = UPIPE_RTP_PREPEND_SIGNATURE,

    .upipe_alloc = upipe_rtp_prepend_alloc,
    .upipe_input = upipe_rtp_prepend_input,
    .upipe_control = upipe_rtp_prepend_control,
    .upipe_free = upipe_rtp_prepend_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for rtp_prepend pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_rtp_prepend_mgr_alloc(void)
{
    return &upipe_rtp_prepend_mgr;
}
