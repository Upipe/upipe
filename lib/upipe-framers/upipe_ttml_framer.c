/*
 * Copyright (C) 2026 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module building frames from chunks of a DVB TTML subtitle
 * stream
 *
 * Normative references:
 *  - ETSI EN 303 560 V1.1.1 (2018-05) (TTML subtitling systems)
 */

#include "upipe/uref.h"
#include "upipe/uref_flow.h"
#include "upipe/uref_block.h"
#include "upipe/uref_clock.h"
#include "upipe/ubuf.h"
#include "upipe/upipe.h"
#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_void.h"
#include "upipe/upipe_helper_sync.h"
#include "upipe/upipe_helper_output.h"
#include "upipe/upipe_helper_flow_def.h"
#include "upipe-framers/upipe_ttml_framer.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>

#include <bitstream/mpeg/psi.h>

/** size of the fixed part of the PES_data_field (EN 303 560 table 16) */
#define PES_DATA_FIELD_HEADER_SIZE 7
/** size of the trailing CRC_32 */
#define CRC_32_SIZE 4

/** a PES packet can not be longer than its 16 bit length field allows, and a
 * stream that never marks the end of one is not allowed to grow past it */
#define MAX_PES_SIZE (UINT16_MAX + 6)

/** @internal @This is the private context of a ttmlf pipe. */
struct upipe_ttmlf {
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
    /** input flow definition packet */
    struct uref *flow_def_input;
    /** attributes added to the input flow definition */
    struct uref *flow_def_attr;

    /** PES packet being reassembled */
    struct uref *next_uref;
    /** size of the PES packet reassembled so far */
    size_t next_uref_size;

    /** true if we have thrown the sync_acquired event (that means we found a
     * valid PES data field) */
    bool acquired;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ttmlf, upipe, UPIPE_TTMLF_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ttmlf, urefcount, upipe_ttmlf_free)
UPIPE_HELPER_VOID(upipe_ttmlf)
UPIPE_HELPER_SYNC(upipe_ttmlf, acquired)

UPIPE_HELPER_OUTPUT(upipe_ttmlf, output, flow_def, output_state, request_list)
UPIPE_HELPER_FLOW_DEF(upipe_ttmlf, flow_def_input, flow_def_attr)

/** @internal @This allocates a ttmlf pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ttmlf_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ttmlf_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ttmlf *upipe_ttmlf = upipe_ttmlf_from_upipe(upipe);
    upipe_ttmlf_init_urefcount(upipe);
    upipe_ttmlf_init_sync(upipe);
    upipe_ttmlf_init_output(upipe);
    upipe_ttmlf_init_flow_def(upipe);
    upipe_ttmlf->next_uref = NULL;
    upipe_ttmlf->next_uref_size = 0;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This drops the PES packet being reassembled.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ttmlf_flush(struct upipe *upipe)
{
    struct upipe_ttmlf *upipe_ttmlf = upipe_ttmlf_from_upipe(upipe);
    uref_free(upipe_ttmlf->next_uref);
    upipe_ttmlf->next_uref = NULL;
    upipe_ttmlf->next_uref_size = 0;
}

/** @internal @This computes the CRC of a PES_data_field.
 *
 * The CRC is the one of annex B of DVB BlueBook A038, so running it over the
 * whole field including the trailing CRC_32 leaves the registers at zero.  The
 * reassembled packet is a chain of one ubuf per transport stream packet, so it
 * is walked one segment at a time.
 *
 * @param uref PES data field
 * @param total size of the field including the CRC_32
 * @return true if the CRC is correct
 */
static bool upipe_ttmlf_check_crc(struct uref *uref, size_t total)
{
    uint32_t crc = 0xffffffff;
    size_t offset = 0;

    while (offset < total) {
        const uint8_t *buffer;
        int size = total - offset;
        if (unlikely(!ubase_check(uref_block_read(uref, offset, &size,
                                                  &buffer)) || size <= 0))
            return false;
        for (int i = 0; i < size; i++)
            crc = (crc << 8) ^ p_psi_crc_table[(crc >> 24) ^ buffer[i]];
        uref_block_unmap(uref, offset);
        offset += size;
    }
    return crc == 0;
}

/** @internal @This works on a reassembled PES data field and outputs it.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ttmlf_work(struct upipe *upipe, struct upump **upump_p)
{
    struct upipe_ttmlf *upipe_ttmlf = upipe_ttmlf_from_upipe(upipe);
    struct uref *uref = upipe_ttmlf->next_uref;
    size_t size = upipe_ttmlf->next_uref_size;
    upipe_ttmlf->next_uref = NULL;
    upipe_ttmlf->next_uref_size = 0;

    if (unlikely(size < PES_DATA_FIELD_HEADER_SIZE + CRC_32_SIZE)) {
        upipe_warn(upipe, "invalid PES data field");
        upipe_ttmlf_sync_lost(upipe);
        uref_free(uref);
        return;
    }

    if (unlikely(!upipe_ttmlf_check_crc(uref, size))) {
        /* EN 303 560 5.2.4.2: on a corrupt data field the current document
         * stays active until it expires. */
        upipe_warn(upipe, "invalid CRC, dropping PES data field");
        upipe_ttmlf_sync_lost(upipe);
        uref_free(uref);
        return;
    }

    if (unlikely(upipe_ttmlf->flow_def == NULL)) {
        struct uref *flow_def = upipe_ttmlf_alloc_flow_def_attr(upipe);
        if (unlikely(flow_def == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            uref_free(uref);
            return;
        }

        UBASE_FATAL(upipe, uref_flow_set_complete(flow_def))

        flow_def = upipe_ttmlf_store_flow_def_attr(upipe, flow_def);
        if (unlikely(flow_def == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            uref_free(uref);
            return;
        }
        upipe_ttmlf_store_flow_def(upipe, flow_def);
    }

    upipe_ttmlf_sync_acquired(upipe);

    /* the PES header of a TTML subtitle stream carries a PTS and no DTS
     * (EN 303 560 5.2.2.1) */
    uref_clock_set_dts_pts_delay(uref, 0);

    upipe_ttmlf_output(upipe, uref, upump_p);
}

/** @internal @This receives data.
 *
 * The PES decapsulator hands over the payload of one transport stream packet
 * at a time, marking the first chunk of a PES packet with the block start flag
 * and its last chunk with the block end flag, and dating the first chunk.  A
 * PES_data_field is only complete, and only has a checkable CRC, once all of
 * them have been put back together.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ttmlf_input(struct upipe *upipe, struct uref *uref,
                              struct upump **upump_p)
{
    struct upipe_ttmlf *upipe_ttmlf = upipe_ttmlf_from_upipe(upipe);

    if (unlikely(ubase_check(uref_flow_get_discontinuity(uref)))) {
        if (upipe_ttmlf->next_uref != NULL) {
            upipe_warn(upipe, "discontinuity, dropping PES packet");
            upipe_ttmlf_flush(upipe);
            upipe_ttmlf_sync_lost(upipe);
        }
    }

    size_t size;
    if (unlikely(!ubase_check(uref_block_size(uref, &size)))) {
        upipe_warn(upipe, "invalid PES chunk");
        uref_free(uref);
        return;
    }

    /* read the end flag now: the chunk is freed once appended */
    bool end = ubase_check(uref_block_get_end(uref));

    if (ubase_check(uref_block_get_start(uref))) {
        if (unlikely(upipe_ttmlf->next_uref != NULL)) {
            /* No end flag, which a PES packet of a declared length always
             * gets: hand over what there is and let the CRC decide. */
            upipe_verbose(upipe, "PES packet with no end");
            upipe_ttmlf_work(upipe, upump_p);
        }
        upipe_ttmlf->next_uref = uref;
        upipe_ttmlf->next_uref_size = size;
    } else if (likely(upipe_ttmlf->next_uref != NULL)) {
        if (unlikely(upipe_ttmlf->next_uref_size + size > MAX_PES_SIZE)) {
            upipe_warn(upipe, "oversized PES packet");
            upipe_ttmlf_flush(upipe);
            upipe_ttmlf_sync_lost(upipe);
            uref_free(uref);
            return;
        }
        struct ubuf *ubuf = uref_detach_ubuf(uref);
        uref_free(uref);
        if (unlikely(!ubase_check(uref_block_append(upipe_ttmlf->next_uref,
                                                    ubuf)))) {
            ubuf_free(ubuf);
            upipe_ttmlf_flush(upipe);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }
        upipe_ttmlf->next_uref_size += size;
    } else {
        /* the beginning of this PES packet was not seen */
        uref_free(uref);
        return;
    }

    if (end)
        upipe_ttmlf_work(upipe, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ttmlf_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    const char *def;
    if (unlikely(!ubase_check(uref_flow_get_def(flow_def, &def)) ||
                 (ubase_ncmp(def, UPIPE_TTMLF_EXPECTED_FLOW_DEF) &&
                  strcmp(def, "block."))))
        return UBASE_ERR_INVALID;
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    flow_def = upipe_ttmlf_store_flow_def_input(upipe, flow_def_dup);
    if (flow_def != NULL)
        upipe_ttmlf_store_flow_def(upipe, flow_def);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a ttmlf pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ttmlf_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_ttmlf_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ttmlf_set_flow_def(upipe, flow_def);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ttmlf_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_ttmlf_flush(upipe);
    upipe_ttmlf_clean_output(upipe);
    upipe_ttmlf_clean_flow_def(upipe);
    upipe_ttmlf_clean_sync(upipe);

    upipe_ttmlf_clean_urefcount(upipe);
    upipe_ttmlf_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ttmlf_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TTMLF_SIGNATURE,

    .upipe_alloc = upipe_ttmlf_alloc,
    .upipe_input = upipe_ttmlf_input,
    .upipe_control = upipe_ttmlf_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ttmlf pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ttmlf_mgr_alloc(void)
{
    return &upipe_ttmlf_mgr;
}
