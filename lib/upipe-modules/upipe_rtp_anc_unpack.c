/*
 * Copyright (C) 2025 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module to decapsulate RTP Ancillary data (RFC8331)
 */


#include <stdlib.h>

#include <upipe/upipe.h>
#include <upipe/uref_block.h>
#include <upipe/uref_pic.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_input.h>
#include <upipe-modules/upipe_rtp_anc_unpack.h>

#include <bitstream/ietf/rtp.h>
#include <bitstream/ietf/rfc8331.h>

struct upipe_rtp_anc_unpack {
    /** refcount management structure */
    struct urefcount urefcount;

    /* output stuff */
    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
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

    /** public upipe structure */
    struct upipe upipe;
};

/** @hidden */
static bool upipe_rtp_anc_unpack_handle(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p);

UPIPE_HELPER_UPIPE(upipe_rtp_anc_unpack, upipe, UPIPE_RTP_ANC_UNPACK_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_rtp_anc_unpack, urefcount, upipe_rtp_anc_unpack_free)
UPIPE_HELPER_VOID(upipe_rtp_anc_unpack)
UPIPE_HELPER_OUTPUT(upipe_rtp_anc_unpack, output, flow_def, output_state,
                    request_list)
UPIPE_HELPER_INPUT(upipe_rtp_anc_unpack, urefs, nb_urefs, max_urefs, blockers,
        upipe_rtp_anc_unpack_handle)

static int upipe_rtp_anc_unpack_set_flow_def(struct upipe *upipe,
                                       struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    flow_def = uref_dup(flow_def);
    if (unlikely(!flow_def)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    uref_flow_set_def(flow_def, "block.vanc.rfc8331.pic.");
    upipe_rtp_anc_unpack_store_flow_def(upipe, flow_def);

    return UBASE_ERR_NONE;
}

static int upipe_rtp_anc_unpack_control(struct upipe *upipe, int command,
                                  va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_FLOW_FORMAT ||
                request->type == UREQUEST_UBUF_MGR)
                return upipe_throw_provide_request(upipe, request);
            return upipe_rtp_anc_unpack_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_FLOW_FORMAT ||
                request->type == UREQUEST_UBUF_MGR)
                return UBASE_ERR_NONE;
            return upipe_rtp_anc_unpack_free_output_proxy(upipe, request);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_rtp_anc_unpack_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
        case UPIPE_GET_FLOW_DEF:
            return upipe_rtp_anc_unpack_control_output(upipe, command, args);
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

static void upipe_rtp_anc_unpack_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_rtp_anc_unpack_clean_output(upipe);
    upipe_rtp_anc_unpack_clean_urefcount(upipe);
    upipe_rtp_anc_unpack_clean_input(upipe);
    upipe_rtp_anc_unpack_free_void(upipe);
}

static struct upipe *upipe_rtp_anc_unpack_alloc(struct upipe_mgr *mgr,
                                           struct uprobe *uprobe,
                                           uint32_t signature,
                                           va_list args)
{
    struct upipe *upipe =
        upipe_rtp_anc_unpack_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_rtp_anc_unpack_init_urefcount(upipe);
    upipe_rtp_anc_unpack_init_input(upipe);
    upipe_rtp_anc_unpack_init_output(upipe);

    upipe_throw_ready(upipe);

    return upipe;
}

static void upipe_rtp_anc_unpack_input(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p)
{
    if (!upipe_rtp_anc_unpack_check_input(upipe)) {
        upipe_rtp_anc_unpack_hold_input(upipe, uref);
        upipe_rtp_anc_unpack_block_input(upipe, upump_p);
    } else if (!upipe_rtp_anc_unpack_handle(upipe, uref, upump_p)) {
        upipe_rtp_anc_unpack_hold_input(upipe, uref);
        upipe_rtp_anc_unpack_block_input(upipe, upump_p);
        /* Increment upipe refcount to avoid disappearing before all unpackets
         * have been sent. */
        upipe_use(upipe);
    }
}

static bool upipe_rtp_anc_unpack_handle(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p)
{
    const uint8_t *buf;
    int size = -1;

    ubase_assert(uref_block_read(uref, 0, &size, &buf));

    if (size < RFC_8331_HEADER_LEN) {
        upipe_err_va(upipe, "Invalid packet (%d < %d)",
            size, RFC_8331_HEADER_LEN);
        goto error;
    }

    uint16_t ext_seq = rfc8331_get_extended_sequence_number(buf);
    uint16_t len = rfc8331_get_length(buf);
    uint8_t anc_count = rfc8331_get_anc_count(buf);
    uint8_t f = rfc8331_get_f(buf);

    switch (f) {
    case RFC_8331_F_PROGRESSIVE:
        uref_pic_set_progressive(uref, true);
        break;
    case RFC_8331_F_FIELD_1:
        uref_pic_set_tf(uref);
        break;
    case RFC_8331_F_FIELD_2:
        uref_pic_set_bf(uref);
        break;
    default:
        upipe_err(upipe, "Invalid field");
        goto error;
    }

    buf += RFC_8331_HEADER_LEN;
    size -= RFC_8331_HEADER_LEN;

    if (size < len) {
        upipe_err_va(upipe, "Invalid packet (%d < %d)", size, len);
        goto error;
    }

    (void)ext_seq;      /* we don't have lower bits seqnum */
    (void)anc_count;    /* next pipe will loop over data */

    ubase_assert(uref_block_unmap(uref, 0));

    uref_block_resize(uref, RFC_8331_HEADER_LEN, -1);

    upipe_rtp_anc_unpack_output(upipe, uref, upump_p);
    return true;

error:
    ubase_assert(uref_block_unmap(uref, 0));
    uref_free(uref);
    return true;
}

static struct upipe_mgr upipe_rtp_anc_unpack_mgr = {
    .refcount = NULL,
    .signature = UPIPE_RTP_ANC_UNPACK_SIGNATURE,

    .upipe_alloc = upipe_rtp_anc_unpack_alloc,
    .upipe_input = upipe_rtp_anc_unpack_input,
    .upipe_control = upipe_rtp_anc_unpack_control,

    .upipe_mgr_control = NULL,
};

struct upipe_mgr *upipe_rtp_anc_unpack_mgr_alloc(void)
{
    return &upipe_rtp_anc_unpack_mgr;
}
