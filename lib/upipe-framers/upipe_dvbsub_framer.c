/*
 * Copyright (C) 2014-2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe module building frames from chunks of a DVB subtitles stream
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/uclock.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_sync.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe-framers/upipe_dvbsub_framer.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#include <bitstream/dvb/sub.h>

/** T-STD TB octet rate for DVB subtitles */
#define TB_RATE_DVBSUB 24000
/** T-STD TB octet rate for DVB subtitles with display definition segment */
#define TB_RATE_DVBSUB_DISP 50000
/** DVB subtitles buffer size (ETSI EN 300 743 5.) */
#define BS_DVBSUB 24576
/** DVB subtitles buffer size with display definition segment
 * (ETSI EN 300 743 5.) */
#define BS_DVBSUB_DISP 102400

/** @internal @This is the private context of an dvbsubf pipe. */
struct upipe_dvbsubf {
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
    /** attributes in the sequence header */
    struct uref *flow_def_attr;

    /** presence of a display definition segment */
    bool display_def;

    /** next uref to be processed */
    struct uref *next_uref;

    /** true if we have thrown the sync_acquired event (that means we found a
     * sequence header) */
    bool acquired;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_dvbsubf, upipe, UPIPE_DVBSUBF_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_dvbsubf, urefcount, upipe_dvbsubf_free)
UPIPE_HELPER_VOID(upipe_dvbsubf)
UPIPE_HELPER_SYNC(upipe_dvbsubf, acquired)

UPIPE_HELPER_OUTPUT(upipe_dvbsubf, output, flow_def, output_state, request_list)
UPIPE_HELPER_FLOW_DEF(upipe_dvbsubf, flow_def_input, flow_def_attr)

/** @internal @This allocates an dvbsubf pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_dvbsubf_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_dvbsubf_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_dvbsubf *upipe_dvbsubf = upipe_dvbsubf_from_upipe(upipe);
    upipe_dvbsubf_init_urefcount(upipe);
    upipe_dvbsubf_init_sync(upipe);
    upipe_dvbsubf_init_output(upipe);
    upipe_dvbsubf_init_flow_def(upipe);
    upipe_dvbsubf->display_def = false;
    upipe_dvbsubf->next_uref = NULL;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This works on a dvbsub frame and outputs it.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_dvbsubf_work(struct upipe *upipe, struct upump **upump_p)
{
    struct upipe_dvbsubf *upipe_dvbsubf = upipe_dvbsubf_from_upipe(upipe);
    bool display_def = false;
    uint8_t buffer;

    if (unlikely(!ubase_check(uref_block_extract(upipe_dvbsubf->next_uref,
                                                 0, 1, &buffer)) ||
                 buffer != DVBSUB_DATA_IDENTIFIER)) {
        upipe_warn(upipe, "invalid DVB subtitle");
        uref_free(upipe_dvbsubf->next_uref);
        goto upipe_dvbsubf_work_err;
    }

    size_t offset = DVBSUB_HEADER_SIZE;
    while (ubase_check(uref_block_extract(upipe_dvbsubf->next_uref,
                                          offset, 1, &buffer)) &&
           buffer == DVBSUBS_SYNC) {
        uint8_t dvbsubs_buffer[DVBSUBS_HEADER_SIZE];
        const uint8_t *dvbsubs = uref_block_peek(upipe_dvbsubf->next_uref,
                                                 offset, DVBSUBS_HEADER_SIZE,
                                                 dvbsubs_buffer);
        if (unlikely(dvbsubs == NULL))
            break;
        uint8_t type = dvbsubs_get_type(dvbsubs);
        uint16_t length = dvbsubs_get_length(dvbsubs);
        UBASE_FATAL(upipe, uref_block_peek_unmap(upipe_dvbsubf->next_uref,
                                                 offset, dvbsubs_buffer,
                                                 dvbsubs))

        if (type == DVBSUBS_DISPLAY_DEFINITION) {
            display_def = true;
            break;
        }
        offset += length + DVBSUBS_HEADER_SIZE;
    }

    if (display_def != upipe_dvbsubf->display_def ||
        upipe_dvbsubf->flow_def == NULL) {
        upipe_dvbsubf->display_def = display_def;

        struct uref *flow_def = upipe_dvbsubf_alloc_flow_def_attr(upipe);
        if (unlikely(flow_def == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            uref_free(upipe_dvbsubf->next_uref);
            goto upipe_dvbsubf_work_err;
        }

        UBASE_FATAL(upipe, uref_flow_set_complete(flow_def))
        UBASE_FATAL(upipe, uref_block_flow_set_octetrate(flow_def,
                                display_def ? TB_RATE_DVBSUB_DISP :
                                              TB_RATE_DVBSUB))
        UBASE_FATAL(upipe, uref_block_flow_set_buffer_size(flow_def,
                                display_def ? BS_DVBSUB_DISP :
                                              BS_DVBSUB))

        flow_def = upipe_dvbsubf_store_flow_def_attr(upipe, flow_def);
        if (unlikely(flow_def == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            uref_free(upipe_dvbsubf->next_uref);
            goto upipe_dvbsubf_work_err;
        }
        upipe_dvbsubf_store_flow_def(upipe, flow_def);
    }

    upipe_dvbsubf_sync_acquired(upipe);

    uref_clock_set_dts_pts_delay(upipe_dvbsubf->next_uref, 0);

    upipe_dvbsubf_output(upipe, upipe_dvbsubf->next_uref, upump_p);
upipe_dvbsubf_work_err:
    upipe_dvbsubf->next_uref = NULL;
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_dvbsubf_input(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p)
{
    struct upipe_dvbsubf *upipe_dvbsubf = upipe_dvbsubf_from_upipe(upipe);
    bool end = ubase_check(uref_block_get_end(uref));

    if (ubase_check(uref_block_get_start(uref))) {
        if (upipe_dvbsubf->next_uref != NULL)
            upipe_dvbsubf_work(upipe, upump_p);

        upipe_dvbsubf->next_uref = uref;
    } else if (likely(upipe_dvbsubf->next_uref != NULL)) {
        struct ubuf *ubuf = uref_detach_ubuf(uref);
        uref_free(uref);
        if (unlikely(!ubase_check(uref_block_append(upipe_dvbsubf->next_uref,
                                                    ubuf)))) {
            ubuf_free(ubuf);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }
    } else {
        uref_free(uref);
        return;
    }

    if (end)
        upipe_dvbsubf_work(upipe, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_dvbsubf_set_flow_def(struct upipe *upipe,
                                      struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    const char *def;
    if (unlikely(!ubase_check(uref_flow_get_def(flow_def, &def)) ||
                 (ubase_ncmp(def, "block.dvb_subtitle.") &&
                  strcmp(def, "block."))))
        return UBASE_ERR_INVALID;
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    flow_def = upipe_dvbsubf_store_flow_def_input(upipe, flow_def_dup);
    if (flow_def != NULL)
        upipe_dvbsubf_store_flow_def(upipe, flow_def);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a dvbsubf pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_dvbsubf_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_dvbsubf_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_dvbsubf_set_flow_def(upipe, flow_def);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_dvbsubf_free(struct upipe *upipe)
{
    struct upipe_dvbsubf *upipe_dvbsubf = upipe_dvbsubf_from_upipe(upipe);
    upipe_throw_dead(upipe);

    uref_free(upipe_dvbsubf->next_uref);
    upipe_dvbsubf_clean_output(upipe);
    upipe_dvbsubf_clean_flow_def(upipe);
    upipe_dvbsubf_clean_sync(upipe);

    upipe_dvbsubf_clean_urefcount(upipe);
    upipe_dvbsubf_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_dvbsubf_mgr = {
    .refcount = NULL,
    .signature = UPIPE_DVBSUBF_SIGNATURE,

    .upipe_alloc = upipe_dvbsubf_alloc,
    .upipe_input = upipe_dvbsubf_input,
    .upipe_control = upipe_dvbsubf_control,

    .upipe_mgr_control = NULL

};

/** @This returns the management structure for all dvbsubf pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_dvbsubf_mgr_alloc(void)
{
    return &upipe_dvbsubf_mgr;
}
