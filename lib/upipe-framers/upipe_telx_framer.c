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
 * @short Upipe module building frames from chunks of a DVB teletext stream
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
#include <upipe-framers/upipe_telx_framer.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

/** maximum retention time */
#define MAX_DELAY_TELX (UCLOCK_FREQ / 25)

/** @internal @This is the private context of an telxf pipe. */
struct upipe_telxf {
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

    /** currently detected frame rate */
    struct urational fps;
    /** currently detected octet rate */
    uint64_t octetrate;

    /** next uref to be processed */
    struct uref *next_uref;
    /** size of the next uref */
    size_t next_uref_size;

    /** pseudo-packet containing date information for the next picture */
    struct uref au_uref_s;
    /** true if we have thrown the sync_acquired event (that means we found a
     * sequence header) */
    bool acquired;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_telxf, upipe, UPIPE_TELXF_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_telxf, urefcount, upipe_telxf_free)
UPIPE_HELPER_VOID(upipe_telxf)
UPIPE_HELPER_SYNC(upipe_telxf, acquired)

UPIPE_HELPER_OUTPUT(upipe_telxf, output, flow_def, output_state, request_list)
UPIPE_HELPER_FLOW_DEF(upipe_telxf, flow_def_input, flow_def_attr)

/** @internal @This allocates an telxf pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_telxf_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_telxf_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_telxf *upipe_telxf = upipe_telxf_from_upipe(upipe);
    upipe_telxf_init_urefcount(upipe);
    upipe_telxf_init_sync(upipe);
    upipe_telxf_init_output(upipe);
    upipe_telxf_init_flow_def(upipe);
    /* There is no known implementation of teletext in non-25 Hz systems. */
    upipe_telxf->fps.num = 25;
    upipe_telxf->fps.den = 1;
    upipe_telxf->octetrate = 0;
    upipe_telxf->next_uref = NULL;
    upipe_telxf->next_uref_size = 0;
    uref_init(&upipe_telxf->au_uref_s);
    uref_clock_set_date_sys(&upipe_telxf->au_uref_s, UINT64_MAX,
                            UREF_DATE_NONE);
    uref_clock_set_date_prog(&upipe_telxf->au_uref_s, UINT64_MAX,
                             UREF_DATE_NONE);
    uref_clock_set_date_orig(&upipe_telxf->au_uref_s, UINT64_MAX,
                             UREF_DATE_NONE);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This works on a telx frame and outputs it.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_telxf_work(struct upipe *upipe, struct upump **upump_p)
{
    struct upipe_telxf *upipe_telxf = upipe_telxf_from_upipe(upipe);
    uint64_t octetrate =
        (upipe_telxf->next_uref_size * upipe_telxf->fps.num +
         upipe_telxf->fps.den - 1) / upipe_telxf->fps.den;
    if (octetrate != upipe_telxf->octetrate) {
        upipe_telxf->octetrate = octetrate;

        struct uref *flow_def = upipe_telxf_alloc_flow_def_attr(upipe);
        if (unlikely(flow_def == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            uref_free(upipe_telxf->next_uref);
            goto upipe_telxf_work_err;
        }

        UBASE_FATAL(upipe, uref_flow_set_complete(flow_def))
        UBASE_FATAL(upipe, uref_pic_flow_set_fps(flow_def, upipe_telxf->fps))
        UBASE_FATAL(upipe, uref_block_flow_set_octetrate(flow_def,
                                upipe_telxf->octetrate))

        flow_def = upipe_telxf_store_flow_def_attr(upipe, flow_def);
        if (unlikely(flow_def == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            uref_free(upipe_telxf->next_uref);
            goto upipe_telxf_work_err;
        }
        upipe_telxf_store_flow_def(upipe, flow_def);
    }

    upipe_telxf_sync_acquired(upipe);

    uint64_t duration = UCLOCK_FREQ * upipe_telxf->fps.den /
                        upipe_telxf->fps.num;

    /* We work on encoded data so in the DTS domain. Rebase on DTS. */
    uint64_t date;
    struct urational drift_rate;
    drift_rate.den = 0;
    uref_clock_get_rate(upipe_telxf->next_uref, &drift_rate);
#define SET_DATE(dv)                                                        \
    if (ubase_check(uref_clock_get_dts_##dv(upipe_telxf->next_uref, &date)))\
        uref_clock_set_dts_##dv(&upipe_telxf->au_uref_s, date);             \
    if (ubase_check(uref_clock_get_dts_##dv(&upipe_telxf->au_uref_s,        \
                                            &date))) {                      \
        uref_clock_set_dts_##dv(upipe_telxf->next_uref, date);              \
        uref_clock_set_dts_##dv(&upipe_telxf->au_uref_s, date + duration);  \
    } else if (ubase_check(uref_clock_get_cr_##dv(upipe_telxf->next_uref,   \
                                                  &date))) {                \
        /* EN 300 472 Annex A */                                            \
        uref_clock_set_dts_##dv(upipe_telxf->next_uref,                     \
                                date + MAX_DELAY_TELX);                     \
        uref_clock_set_dts_##dv(&upipe_telxf->au_uref_s, date +             \
                                MAX_DELAY_TELX + duration);                 \
    }
    SET_DATE(sys)
    SET_DATE(prog)
    SET_DATE(orig)
#undef SET_DATE

    uref_clock_set_dts_pts_delay(upipe_telxf->next_uref, 0);
    if (drift_rate.den)
        uref_clock_set_rate(upipe_telxf->next_uref, drift_rate);

    upipe_telxf_output(upipe, upipe_telxf->next_uref, upump_p);
upipe_telxf_work_err:
    upipe_telxf->next_uref = NULL;
    upipe_telxf->next_uref_size = 0;
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_telxf_input(struct upipe *upipe, struct uref *uref,
                              struct upump **upump_p)
{
    struct upipe_telxf *upipe_telxf = upipe_telxf_from_upipe(upipe);
    size_t uref_size;
    if (!ubase_check(uref_block_size(uref, &uref_size)) || !uref_size) {
        uref_free(uref);
        return;
    }
    bool end = ubase_check(uref_block_get_end(uref));

    if (ubase_check(uref_block_get_start(uref))) {
        if (upipe_telxf->next_uref != NULL)
            upipe_telxf_work(upipe, upump_p);

        upipe_telxf->next_uref = uref;
        upipe_telxf->next_uref_size = uref_size;
    } else if (likely(upipe_telxf->next_uref != NULL)) {
        struct ubuf *ubuf = uref_detach_ubuf(uref);
        uref_free(uref);
        if (unlikely(!ubase_check(uref_block_append(upipe_telxf->next_uref,
                                                    ubuf)))) {
            ubuf_free(ubuf);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }
        upipe_telxf->next_uref_size += uref_size;
    } else {
        uref_free(uref);
        return;
    }

    if (end)
        upipe_telxf_work(upipe, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_telxf_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    const char *def;
    if (unlikely(!ubase_check(uref_flow_get_def(flow_def, &def)) ||
                 (ubase_ncmp(def, "block.dvb_teletext.") &&
                  strcmp(def, "block."))))
        return UBASE_ERR_INVALID;
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    flow_def = upipe_telxf_store_flow_def_input(upipe, flow_def_dup);
    if (flow_def != NULL)
        upipe_telxf_store_flow_def(upipe, flow_def);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a telxf pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_telxf_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_telxf_control_output(upipe, command, args));

    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_telxf_set_flow_def(upipe, flow_def);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_telxf_free(struct upipe *upipe)
{
    struct upipe_telxf *upipe_telxf = upipe_telxf_from_upipe(upipe);
    upipe_throw_dead(upipe);

    uref_free(upipe_telxf->next_uref);
    upipe_telxf_clean_output(upipe);
    upipe_telxf_clean_flow_def(upipe);
    upipe_telxf_clean_sync(upipe);

    upipe_telxf_clean_urefcount(upipe);
    upipe_telxf_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_telxf_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TELXF_SIGNATURE,

    .upipe_alloc = upipe_telxf_alloc,
    .upipe_input = upipe_telxf_input,
    .upipe_control = upipe_telxf_control,

    .upipe_mgr_control = NULL

};

/** @This returns the management structure for all telxf pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_telxf_mgr_alloc(void)
{
    return &upipe_telxf_mgr;
}
