/*
 * Copyright (C) 2019 Open Broadcast Systems Ltd
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

/*
 * Normative references:
 * SMPTE 2038-2008 Carriage of Ancillary Data Packets in an MPEG-2 Transport Stream
 */

#include <upipe-modules/upipe_vanc_decoder.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe.h>
#include <upipe/uref_block.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_pic.h>

#include <bitstream/smpte/291.h>

/** @internal @This is the private context of a vanc decoder pipe. */
struct upipe_vanc_decoder {
    /** pipe public structure */
    struct upipe upipe;
    /** refcounting structure */
    struct urefcount urefcount;
    /** reference to the output pipe */
    struct upipe *output;
    /** reference to the output flow format */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain requests;
    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;
    /** ubuf flow format */
    struct uref *flow_format;
};

UPIPE_HELPER_UPIPE(upipe_vanc_decoder, upipe, UPIPE_VANC_DECODER_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_vanc_decoder, urefcount, upipe_vanc_decoder_free);
UPIPE_HELPER_VOID(upipe_vanc_decoder);
UPIPE_HELPER_OUTPUT(upipe_vanc_decoder, output, flow_def, output_state,
                    requests);
UPIPE_HELPER_UBUF_MGR(upipe_vanc_decoder, ubuf_mgr, flow_format,
                      ubuf_mgr_request,
                      NULL,
                      upipe_vanc_decoder_register_output_request,
                      upipe_vanc_decoder_unregister_output_request);

/** @internal @This allocates an vanc decoder pipe.
 *
 * @param mgr reference to the vanc decoder pipe manager.
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args arguments
 * @return pointer to allocated pipe, or NULL in case of failure
 */
static struct upipe *upipe_vanc_decoder_alloc(struct upipe_mgr *mgr,
                                             struct uprobe *uprobe,
                                             uint32_t signature,
                                             va_list args)
{
    struct upipe *upipe =
        upipe_vanc_decoder_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_vanc_decoder_init_urefcount(upipe);
    upipe_vanc_decoder_init_output(upipe);
    upipe_vanc_decoder_init_ubuf_mgr(upipe);

    upipe_throw_ready(upipe);

    return upipe;
}

/** @internal @This frees an vanc decoder pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_vanc_decoder_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);
    upipe_vanc_decoder_clean_ubuf_mgr(upipe);
    upipe_vanc_decoder_clean_output(upipe);
    upipe_vanc_decoder_clean_urefcount(upipe);
    upipe_vanc_decoder_free_void(upipe);
}

/** @internal @This is called when there is new data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref carrying the data
 * @param upump_p reference to the pump that generated the buffer
 */
static void upipe_vanc_decoder_input(struct upipe *upipe,
                                    struct uref *uref,
                                    struct upump **upump_p)
{
    struct upipe_vanc_decoder *vancd = upipe_vanc_decoder_from_upipe(upipe);
    if (unlikely(!vancd->ubuf_mgr)) {
        uref_free(uref);
        return;
    }

    const uint8_t *r;
    int end = -1;
    if (unlikely(!ubase_check(uref_block_read(uref, 0, &end, &r)))) {
        upipe_err(upipe, "could not read uref");
        uref_free(uref);
        return;
    }

    if (end < 9) {
        upipe_dbg_va(upipe, "Packet too small (%d)", end);
        goto ret;
    }

    struct ubits s;
    ubits_init(&s, (uint8_t*)r, end, UBITS_READ);

    while (end >= 9) {
        if (ubits_get(&s, 6))
            goto ret;

        bool c_not_y = ubits_get(&s, 1);

        unsigned line = ubits_get(&s, 11);
        unsigned offset = ubits_get(&s, 12);
        uint16_t did = ubits_get(&s, 10);
        uint16_t sdid = ubits_get(&s, 10);
        uint16_t dc = ubits_get(&s, 10);

        size_t bits_left = (6+1+11+12+3*10) + 10 * ((dc & 0xff) + 1 /* checksum */);
        if (((bits_left + 7) / 8) > end) {
            upipe_dbg_va(upipe, "Invalid DC %u, packet size %d", dc & 0xff, end);
            goto ret;
        }

        end -= (bits_left + 7) / 8;

        if (line == 0) {
            upipe_dbg(upipe, "Invalid line number 0");
            goto ret;
        }

        struct uref *pic = uref_dup(uref);
        if (unlikely(!pic)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            goto ret;
        }

        struct ubuf *ubuf_pic = ubuf_pic_alloc(vancd->ubuf_mgr,
                S291_HEADER_SIZE + (dc & 0xff) + 1, 1);
        if (unlikely(!ubuf_pic)) {
            uref_free(pic);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            goto ret;
        }

        uref_attach_ubuf(pic, ubuf_pic);

        uint8_t *vanc_buf;
        if (unlikely(!ubase_check(uref_pic_plane_write(pic, "x10",
                            0, 0, -1, -1, &vanc_buf)))) {
            uref_free(pic);
        }
        uint16_t *data = (uint16_t*)vanc_buf;

        data[0] = S291_ADF1;
        data[1] = S291_ADF2;
        data[2] = S291_ADF3;
        data[3] = did;
        data[4] = sdid;
        data[5] = dc;

        for (int i = 0; i < (dc & 0xff) + 1; i++) {
            data[S291_HEADER_SIZE+i] = ubits_get(&s, 10);
        }

        while (s.available) {
            if (!ubits_get(&s, 1)) {
                upipe_dbg(upipe, "Invalid byte align, skipping");
                uref_pic_plane_unmap(pic, "x10", 0, 0, -1, -1);
                uref_free(pic);
                continue;
            }
        }

        if (!s291_check_cs(data)) {
            upipe_dbg(upipe, "Invalid checksum, skipping");
            uref_pic_plane_unmap(pic, "x10", 0, 0, -1, -1);
            uref_free(pic);
            continue;
        }

        upipe_dbg_va(upipe, "y=%d line %d off %d | DID 0x%.2x SDID 0x%.2x DC 0x%.2x",
                c_not_y, line, offset,
                s291_get_did(data), s291_get_sdid(data), s291_get_dc(data));

        if (c_not_y)
            uref_pic_set_c_not_y(pic);

        uref_pic_set_hposition(pic, offset);
        uref_pic_set_vposition(pic, line - 1);

        uref_pic_plane_unmap(pic, "x10", 0, 0, -1, -1);
        upipe_vanc_decoder_output(upipe, pic, upump_p);
    }

ret:
    uref_block_unmap(uref, 0);
    uref_free(uref);
}

/** @internal @This sets the output flow format.
 *
 * @param upipe description structure of the pipe
 * @param flow_def the flow format to set
 * @return an error code
 */
static int upipe_vanc_decoder_set_flow_def(struct upipe *upipe,
                                          struct uref *flow_def)
{
    UBASE_RETURN(uref_flow_match_def(flow_def, "block.vanc.pic."));

    flow_def = uref_sibling_alloc(flow_def);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    uref_flow_set_def(flow_def, "pic.");
    UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def, 1))
    UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 2, "x10"))
    upipe_vanc_decoder_store_flow_def(upipe, uref_dup(flow_def));
    upipe_vanc_decoder_require_ubuf_mgr(upipe, flow_def);

    return UBASE_ERR_NONE;
}

/** @internal @This dispatches commands.
 *
 * @param upipe description structure of the pipe
 * @param cmd command to dispatch
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_vanc_decoder_control(struct upipe *upipe,
                                     int cmd,
                                     va_list args)
{
    UBASE_HANDLED_RETURN(upipe_vanc_decoder_control_output(upipe, cmd, args));

    switch (cmd) {
    case UPIPE_SET_FLOW_DEF: {
        struct uref *flow_def = va_arg(args, struct uref *);
        return upipe_vanc_decoder_set_flow_def(upipe, flow_def);
    }
    }
    return UBASE_ERR_UNHANDLED;
}

/** @internal @This is the static vanc decoder pipe manager. */
static struct upipe_mgr upipe_vanc_decoder_mgr = {
    .signature = UPIPE_VANC_DECODER_SIGNATURE,
    .refcount = NULL,
    .upipe_alloc = upipe_vanc_decoder_alloc,
    .upipe_input = upipe_vanc_decoder_input,
    .upipe_control = upipe_vanc_decoder_control,
};

/** @This returns the static vanc decoder pipe manager.
 *
 * @return a reference to the static vanc decoder pipe manager
 */
struct upipe_mgr *upipe_vancd_mgr_alloc(void)
{
    return &upipe_vanc_decoder_mgr;
}
