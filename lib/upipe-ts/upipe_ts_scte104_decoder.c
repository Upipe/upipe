/*
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * This event is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This event is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this event; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe module decoding the operation tables of SCTE 104 streams
 * Normative references:
 *  - SCTE 104 2012 (Automation to Compression Communications API)
 *  - SCTE 35 2013 (Digital Program Insertion Cueing Message for Cable)
 */

#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe-ts/upipe_ts_scte104_decoder.h>
#include <upipe-ts/uref_ts_flow.h>
#include <upipe-ts/uref_ts_scte104_flow.h>
#include <upipe-ts/uref_ts_scte35.h>
#include <upipe-ts/uref_ts_scte35_desc.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>

#include <bitstream/scte/35.h>
#include <bitstream/scte/104.h>

#include "upipe_ts_scte_common.h"

/** we only accept TS packets */
#define EXPECTED_FLOW_DEF "block.scte104."
/** we output SCTE104 metadata */
#define OUTPUT_FLOW_DEF "void.scte35."

/** @hidden */
static int upipe_ts_scte104d_check(struct upipe *upipe,
                                   struct uref *flow_format);

/** @internal @This is an op handler function type. */
typedef int (*scte104_handler_func)(struct upipe *upipe,
                                    struct uref *input,
                                    const uint8_t *op,
                                    struct uchain *urefs);

/** @internal @This enumerates the operation usage. */
enum scte104_op_usage {
    /** normal */
    SCTE104_OP_USAGE_NORMAL,
    /** supplemental */
    SCTE104_OP_USAGE_SUPPLEMENTAL,
};

/** @internal @This is an op id handler. */
struct scte104_handler {
    /** handled op id */
    uint16_t opid;
    /** is it supplemental */
    enum scte104_op_usage usage;
    /** handler */
    scte104_handler_func handle;
};

#define SCTE104_HANDLER(Opid, Usage, Handler)   \
{                                               \
    .opid = SCTE104_OPID_##Opid,                \
    .usage = SCTE104_OP_USAGE_##Usage,          \
    .handle = upipe_ts_scte104d_##Handler,      \
}

#define SCTE104_HANDLER_NORMAL(Opid, Handler)   \
    SCTE104_HANDLER(Opid, NORMAL, Handler)
#define SCTE104_HANDLER_SUPPLEMENTAL(Opid, Handler)   \
    SCTE104_HANDLER(Opid, SUPPLEMENTAL, Handler)

/** @internal @This is the private context of a ts_scte104d pipe. */
struct upipe_ts_scte104d {
    /** real refcount management structure */
    struct urefcount urefcount_real;
    /** refcount management structure */
    struct urefcount urefcount;

    /** input flow def */
    struct uref *flow_def;
    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** array of received messages */
    struct uref *messages[UINT8_MAX + 1];

    /** list of output subpipes */
    struct uchain subs;

    /** manager to create output subpipes */
    struct upipe_mgr sub_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_scte104d, upipe, UPIPE_TS_SCTE104D_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_scte104d, urefcount, upipe_ts_scte104d_no_input)
UPIPE_HELPER_VOID(upipe_ts_scte104d)
UPIPE_HELPER_UBUF_MGR(upipe_ts_scte104d, ubuf_mgr, flow_format,
                      ubuf_mgr_request, upipe_ts_scte104d_check,
                      upipe_throw_provide_request, NULL)

UBASE_FROM_TO(upipe_ts_scte104d, urefcount, urefcount_real, urefcount_real)

/** @hidden */
static void upipe_ts_scte104d_free(struct urefcount *urefcount_real);

/** @internal @This is the private context of an output of a ts_scte104d pipe. */
struct upipe_ts_scte104d_sub {
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked lists */
    struct uchain uchain;

    /** DPI PID index to match */
    uint64_t dpi_pid_index;

    /** pipe acting as output */
    struct upipe *output;
    /** flow definition packet on this output */
    struct uref *flow_def;
    /** attributes / parameters from application */
    struct uref *flow_def_params;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_scte104d_sub, upipe,
                   UPIPE_TS_SCTE104D_OUTPUT_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_scte104d_sub, urefcount,
                       upipe_ts_scte104d_sub_free)
UPIPE_HELPER_FLOW(upipe_ts_scte104d_sub, NULL)
UPIPE_HELPER_OUTPUT(upipe_ts_scte104d_sub, output, flow_def, output_state, request_list)

UPIPE_HELPER_SUBPIPE(upipe_ts_scte104d, upipe_ts_scte104d_sub, sub,
                     sub_mgr, subs, uchain)

/** @internal @This allocates an output subpipe of a ts_scte104d pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_scte104d_sub_alloc(struct upipe_mgr *mgr,
                                                 struct uprobe *uprobe,
                                                 uint32_t signature,
                                                 va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_ts_scte104d_sub_alloc_flow(mgr, uprobe,
                                                           signature,
                                                           args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_scte104d_sub *upipe_ts_scte104d_sub =
        upipe_ts_scte104d_sub_from_upipe(upipe);
    upipe_ts_scte104d_sub_init_urefcount(upipe);
    upipe_ts_scte104d_sub_init_output(upipe);
    upipe_ts_scte104d_sub_init_sub(upipe);
    upipe_ts_scte104d_sub->flow_def_params = flow_def;
    upipe_ts_scte104d_sub->dpi_pid_index = 0;
    uref_ts_scte104_flow_get_dpi_pid_index(flow_def,
            &upipe_ts_scte104d_sub->dpi_pid_index);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This builds the subpipe flow definition.
 *
 * @param upipe description structure of the subpipe
 */
static void upipe_ts_scte104d_sub_build_flow_def(struct upipe *upipe)
{
    struct upipe_ts_scte104d_sub *sub = upipe_ts_scte104d_sub_from_upipe(upipe);
    struct upipe_ts_scte104d *scte104d =
        upipe_ts_scte104d_from_sub_mgr(upipe->mgr);
    if (scte104d->flow_def == NULL)
        return;

    struct uref *flow_def = uref_dup(sub->flow_def_params);
    if (unlikely(!flow_def)) {
        upipe_throw_error(upipe, UBASE_ERR_ALLOC);
        return;
    }

    UBASE_ERROR(upipe, uref_flow_set_def(flow_def, OUTPUT_FLOW_DEF))
    /* We need to keep input latency. */
    uint64_t latency;
    if (likely(ubase_check(uref_clock_get_latency(scte104d->flow_def, &latency)))) {
        UBASE_ERROR(upipe, uref_clock_set_latency(flow_def, latency))
    }

    upipe_ts_scte104d_sub_store_flow_def(upipe, flow_def);

    /* Force sending out flow definition */
    struct uref *uref = uref_sibling_alloc(flow_def);
    upipe_ts_scte104d_sub_output(upipe, uref, NULL);
}

/** @internal @This processes control commands on an output subpipe of a
 * ts_scte104d pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_scte104d_sub_control(struct upipe *upipe,
                                         int command, va_list args)
{
    UBASE_HANDLED_RETURN(
        upipe_ts_scte104d_sub_control_super(upipe, command, args));
    switch (command) {
        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_ts_scte104d_sub_control_output(upipe, command, args);

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_scte104d_sub_free(struct upipe *upipe)
{
    struct upipe_ts_scte104d_sub *sub = upipe_ts_scte104d_sub_from_upipe(upipe);
    upipe_throw_dead(upipe);

    uref_free(sub->flow_def_params);
    upipe_ts_scte104d_sub_clean_output(upipe);
    upipe_ts_scte104d_sub_clean_sub(upipe);
    upipe_ts_scte104d_sub_clean_urefcount(upipe);
    upipe_ts_scte104d_sub_free_flow(upipe);
}

/** @internal @This initializes the output manager for a ts_scte104d pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_scte104d_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_ts_scte104d *upipe_ts_scte104d =
        upipe_ts_scte104d_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_ts_scte104d->sub_mgr;
    sub_mgr->refcount =
        upipe_ts_scte104d_to_urefcount_real(upipe_ts_scte104d);
    sub_mgr->signature = UPIPE_TS_SCTE104D_OUTPUT_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_ts_scte104d_sub_alloc;
    sub_mgr->upipe_control = upipe_ts_scte104d_sub_control;
}

/** @internal @This allocates a ts_scte104d pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_scte104d_alloc(struct upipe_mgr *mgr,
                                             struct uprobe *uprobe,
                                             uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_scte104d_alloc_void(mgr, uprobe, signature,
                                                       args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_scte104d *upipe_ts_scte104d =
        upipe_ts_scte104d_from_upipe(upipe);
    upipe_ts_scte104d_init_urefcount(upipe);
    urefcount_init(upipe_ts_scte104d_to_urefcount_real(upipe_ts_scte104d),
                   upipe_ts_scte104d_free);
    upipe_ts_scte104d_init_ubuf_mgr(upipe);
    upipe_ts_scte104d_init_sub_subs(upipe);
    upipe_ts_scte104d_init_sub_mgr(upipe);
    upipe_ts_scte104d->flow_def = NULL;

    for (int i = 0; i <= UINT8_MAX; i++)
        upipe_ts_scte104d->messages[i] = NULL;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This finds an output of a ts_scte104d pipe.
 *
 * @param upipe description structure of the pipe
 * @param dpi_pid_index DPI PID index to find
 * @return pointer to upipe or NULL if not found
 */
static struct upipe *upipe_ts_scte104d_find_sub(struct upipe *upipe,
                                                uint64_t dpi_pid_index)
{
    struct upipe_ts_scte104d *upipe_ts_scte104d =
        upipe_ts_scte104d_from_upipe(upipe);
    struct uchain *uchain;
    ulist_foreach (&upipe_ts_scte104d->subs, uchain) {
        struct upipe_ts_scte104d_sub *sub =
            upipe_ts_scte104d_sub_from_uchain(uchain);
        if (sub->dpi_pid_index == dpi_pid_index)
            return upipe_ts_scte104d_sub_to_upipe(sub);
    }
    return NULL;
}

/** @internal @This parses a single operation message.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_scte104d_handle_single(struct upipe *upipe,
                                            struct uref *uref,
                                            struct upump **upump_p)
{
    int size = -1;
    const uint8_t *msg;
    if (unlikely(!ubase_check(uref_block_read(uref, 0, &size, &msg)))) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        uref_free(uref);
        return;
    }

    if (!scte104s_validate(msg, size)) {
        upipe_warn(upipe, "invalid single operation message");
        goto upipe_ts_scte104d_handle_single_err;
    }

    if (scte104s_get_protocol(msg) != 0) {
        upipe_warn(upipe, "invalid single operation message version");
        goto upipe_ts_scte104d_handle_single_err;
    }

    /* not implemented */
    uint16_t opid = scte104_get_opid(msg);
    switch (opid) {
        default:
            upipe_dbg_va(upipe, "%u not implemented", opid);
            break;
    }

upipe_ts_scte104d_handle_single_err:
    uref_block_unmap(uref, 0);
    uref_free(uref);
}

/** @internal @This parses a splice request data.
 *
 * @param upipe description structure of the pipe
 * @param uref input buffer
 * @param op SCTE104 operation
 * @param urefs filled with the event
 * @return an error code
 */
static int upipe_ts_scte104d_splice(struct upipe *upipe,
                                    struct uref *uref,
                                    const uint8_t *op,
                                    struct uchain *urefs)
{
    const uint8_t *data = scte104o_get_data(op);
    uint8_t type = scte104srd_get_insert_type(data);
    uint32_t event_id = scte104srd_get_event_id(data);
    uint16_t unique_program_id = scte104srd_get_unique_program_id(data);
    uint16_t pre_roll_time = scte104srd_get_pre_roll_time(data);
    uint16_t break_duration = scte104srd_get_break_duration(data);
    bool auto_return = scte104srd_get_auto_return(data);

    struct uref *event = uref_fork(uref, NULL);
    if (unlikely(event == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    uref_ts_scte35_set_command_type(uref, SCTE35_INSERT_COMMAND);
    uref_ts_scte35_set_event_id(event, event_id);
    if (type == SCTE104SRD_CANCEL)
        uref_ts_scte35_set_cancel(event);
    else {
        uref_ts_scte35_set_unique_program_id(event, unique_program_id);
        if (type == SCTE104SRD_START_NORMAL ||
            type == SCTE104SRD_START_IMMEDIATE)
            uref_ts_scte35_set_out_of_network(event);
        if ((type == SCTE104SRD_START_NORMAL ||
             type == SCTE104SRD_END_NORMAL) && pre_roll_time) {
            int64_t delay = (int64_t)pre_roll_time * UCLOCK_FREQ / 1000;
            uref_clock_add_date_orig(event, delay);
            uref_clock_add_date_prog(event, delay);
            uref_clock_add_date_sys(event, delay);
        }
        if ((type == SCTE104SRD_START_NORMAL ||
             type == SCTE104SRD_START_IMMEDIATE) && break_duration) {
            uref_clock_set_duration(event,
                                    (uint64_t)break_duration * UCLOCK_FREQ / 10);
            if (auto_return)
                uref_ts_scte35_set_auto_return(event);
        }
    }
    ulist_add(urefs, uref_to_uchain(event));
    return UBASE_ERR_NONE;
}

/** @internal @This parses a splice null request data.
 *
 * @param upipe description structure of the pipe
 * @param uref input buffer
 * @param op SCTE104 operation
 * @param urefs filled with the event
 * @return an error code
 */
static int upipe_ts_scte104d_splice_null(struct upipe *upipe,
                                         struct uref *uref,
                                         const uint8_t *op,
                                         struct uchain *urefs)
{
    struct uref *event = uref_fork(uref, NULL);
    if (unlikely(event == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    uref_ts_scte35_set_command_type(event, SCTE35_NULL_COMMAND);
    ulist_add(urefs, uref_to_uchain(event));
    return UBASE_ERR_NONE;
}

/** @internal @This parses a time signal request data.
 *
 * @param upipe description structure of the pipe
 * @param uref input buffer
 * @param op SCTE104 operation
 * @param urefs filled with the event
 * @return an error code
 */
static int upipe_ts_scte104d_time_signal(struct upipe *upipe,
                                         struct uref *uref,
                                         const uint8_t *op,
                                         struct uchain *urefs)
{
    const uint8_t *data = scte104o_get_data(op);
    uint16_t data_length = scte104o_get_data_length(op);
    if (data_length < SCTE104TSRD_HEADER_SIZE) {
        upipe_warn(upipe, "drop invalid tsrd");
        return UBASE_ERR_INVALID;
    }
    uint16_t pre_roll_time = scte104tsrd_get_pre_roll_time(data);

    struct uref *event = uref_fork(uref, NULL);
    if (unlikely(event == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    uref_ts_scte35_set_command_type(event, SCTE35_TIME_SIGNAL_COMMAND);
    int64_t delay = (int64_t)pre_roll_time * UCLOCK_FREQ / 1000;
    uref_clock_add_date_orig(event, delay);
    uref_clock_add_date_prog(event, delay);
    uref_clock_add_date_sys(event, delay);

    ulist_add(urefs, uref_to_uchain(event));
    return UBASE_ERR_NONE;
}

/** @internal @This parses an insert descriptor request data.
 *
 * @param upipe description structure of the pipe
 * @param uref event which own this descriptor
 * @param op SCTE104 operation
 * @param urefs filled with the descriptors
 * @return an error code
 */
static int upipe_ts_scte104d_insert_descriptor(struct upipe *upipe,
                                               struct uref *event,
                                               const uint8_t *op,
                                               struct uchain *urefs)
{
    const uint8_t *data = scte104o_get_data(op);
    uint16_t data_length = scte104o_get_data_length(op);
    if (data_length < SCTE104IDRD_HEADER_SIZE) {
        upipe_warn(upipe, "drop invalid idrd");
        return UBASE_ERR_INVALID;
    }
    data_length -= SCTE104IDRD_HEADER_SIZE;

    uint8_t count = scte104idrd_get_count(data);
    const uint8_t *image = scte104idrd_get_image(data);
    for (uint8_t i = 0; i < count; i++) {
        if (data_length < DESC_HEADER_SIZE) {
            upipe_warn(upipe, "drop invalid idrd descriptor(s)");
            return UBASE_ERR_INVALID;
        }
        uint16_t size = desc_get_length(image) + DESC_HEADER_SIZE;
        if (data_length < size) {
            upipe_warn(upipe, "drop invalid idrd descriptor(s)");
            return UBASE_ERR_INVALID;
        }
        data_length -= size;

        UBASE_FATAL(upipe, uref_ts_flow_add_descriptor(event, image, size));
        image += size;

        struct uref *desc = upipe_ts_scte_extract_desc(upipe, event, image);
        if (desc)
            ulist_add(urefs, uref_to_uchain(desc));
    }
    return UBASE_ERR_NONE;
}

/** @internal @This parses an insert segmentation descriptor request data.
 *
 * @param upipe description structure of the pipe
 * @param uref event which own this descriptor
 * @param op SCTE104 operation
 * @param urefs filled with the descriptors
 * @return an error code
 */
static int
upipe_ts_scte104d_insert_segmentation_descriptor(struct upipe *upipe,
                                                 struct uref *event,
                                                 const uint8_t *op,
                                                 struct uchain *urefs)
{
    struct uref *desc = uref_dup_inner(event);
    if (unlikely(desc == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    uint16_t data_length = scte104o_get_data_length(op);
    if (data_length < SCTE104ISDRD_HEADER_SIZE) {
        upipe_warn(upipe, "drop invalid isdrd");
        return UBASE_ERR_INVALID;
    }
    const uint8_t *data = scte104o_get_data(op);
    uint32_t event_id = scte104isdrd_get_event_id(data);
    uint8_t cancel_indicator = scte104isdrd_get_cancel_indicator(data);
    uint16_t duration = scte104isdrd_get_duration(data);
    uint8_t upid_type = scte104isdrd_get_upid_type(data);
    uint8_t upid_length = scte104isdrd_get_upid_length(data);
    if (data_length < SCTE104ISDRD_HEADER_SIZE + upid_length) {
        upipe_warn(upipe, "drop invalid isdrd");
        return UBASE_ERR_INVALID;
    }
    uint8_t *upid = scte104isdrd_get_upid(data);
    uint8_t type_id = scte104isdrd_get_type_id(data);
    uint8_t num = scte104isdrd_get_num(data);
    uint8_t expected = scte104isdrd_get_expected(data);
    uint8_t duration_extension_frames =
        scte104isdrd_get_duration_extension_frames(data);
    uint8_t delivery_not_restricted =
        scte104isdrd_get_delivery_not_restricted(data);
    uint8_t web_delivery_allowed = scte104isdrd_get_web_delivery_allowed(data);
    uint8_t no_regional_blackout = scte104isdrd_get_no_regional_blackout(data);
    uint8_t archive_allowed = scte104isdrd_get_archive_allowed(data);
    uint8_t device_restrictions = scte104isdrd_get_device_restrictions(data);
    uint8_t insert_sub_info = 0;
    uint8_t sub_num = 0;
    uint8_t sub_expected = 0;
    if (data_length >= SCTE104ISDRD_HEADER_SIZE + upid_length + 3) {
        insert_sub_info = scte104isdrd_get_insert_sub_info(data);
        sub_num = scte104isdrd_get_sub_num(data);
        sub_expected = scte104isdrd_get_sub_expected(data);
    }

    uref_ts_scte35_desc_set_tag(desc, SCTE35_SPLICE_DESC_TAG_SEG);
    uref_ts_scte35_desc_set_identifier(desc, SCTE35_SPLICE_DESC_IDENTIFIER);
    uref_ts_scte35_desc_seg_set_event_id(desc, event_id);
    if (cancel_indicator)
        uref_ts_scte35_desc_seg_set_cancel(desc);
    uref_clock_set_duration(desc, (uint64_t)duration * UCLOCK_FREQ);
    if (upid_type || upid_length) {
        uref_ts_scte35_desc_seg_set_upid_type(desc, upid_type);
        uref_ts_scte35_desc_seg_set_upid_type_name(
            desc, scte35_seg_desc_upid_type_to_str(upid_type));
        uref_ts_scte35_desc_seg_set_upid_length(desc, upid_length);
        uref_ts_scte35_desc_seg_set_upid(desc, upid, upid_length);
    }
    uref_ts_scte35_desc_seg_set_type_id(desc, type_id);
    uref_ts_scte35_desc_seg_set_type_id_name(
        desc, scte35_seg_desc_type_id_to_str(type_id));
    uref_ts_scte35_desc_seg_set_num(desc, num);
    uref_ts_scte35_desc_seg_set_expected(desc, expected);
    if (delivery_not_restricted)
        uref_ts_scte35_desc_seg_set_delivery_not_restricted(desc);
    if (web_delivery_allowed)
        uref_ts_scte35_desc_seg_set_web(desc);
    if (no_regional_blackout)
        uref_ts_scte35_desc_seg_set_no_regional_blackout(desc);
    if (archive_allowed)
        uref_ts_scte35_desc_seg_set_archive(desc);
    if (!delivery_not_restricted)
        uref_ts_scte35_desc_seg_set_device(desc, device_restrictions);
    if (insert_sub_info) {
        uref_ts_scte35_desc_seg_set_sub_num(desc, sub_num);
        uref_ts_scte35_desc_seg_set_sub_expected(desc, sub_expected);
    }
    if (duration_extension_frames)
        uref_ts_scte35_desc_seg_set_duration_extension_frames(
            desc, duration_extension_frames);

    ulist_add(urefs, uref_to_uchain(desc));
    return UBASE_ERR_NONE;
}

/** @internal @This is the list of the implemented handlers. */
static const struct scte104_handler *
upipe_ts_scte104d_get_handler(struct upipe *upipe, uint16_t opid)
{
    static const struct scte104_handler handlers[] = {
        SCTE104_HANDLER_NORMAL(SPLICE, splice),
        SCTE104_HANDLER_NORMAL(SPLICE_NULL, splice_null),
        SCTE104_HANDLER_NORMAL(TIME_SIGNAL, time_signal),
        SCTE104_HANDLER_SUPPLEMENTAL(INSERT_DESCRIPTOR, insert_descriptor),
        SCTE104_HANDLER_SUPPLEMENTAL(INSERT_SEGMENTATION_DESCRIPTOR,
                                     insert_segmentation_descriptor),
    };
    for (unsigned i = 0; i < UBASE_ARRAY_SIZE(handlers); i++)
        if (handlers[i].opid == opid)
            return &handlers[i];
    return NULL;
}

/** @internal @This output a multiple operation message.
 *
 * @param upipe description structure of the pipe
 * @param event uref structure describing the event
 * @param urefs list of uref structure describing an event
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_scte104d_output(struct upipe *output,
                                     struct uchain *urefs,
                                     struct upump **upump_p)
{
    struct uchain *uchain = ulist_pop(urefs);
    if (!uchain)
        return;

    struct uref *out = uref_from_uchain(uchain);
    uref_block_set_start(out);
    while ((uchain = ulist_pop(urefs))) {
        struct uref *next = uref_from_uchain(uchain);
        upipe_ts_scte104d_sub_output(output, out, upump_p);
        out = next;
    }
    uref_block_set_end(out);
    upipe_ts_scte104d_sub_output(output, out, upump_p);
}

/** @internal @This parses a multiple operation message.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_scte104d_handle_multiple(struct upipe *upipe,
                                              struct uref *uref,
                                              struct upump **upump_p)
{
    struct upipe_ts_scte104d *upipe_ts_scte104d =
        upipe_ts_scte104d_from_upipe(upipe);
    struct uchain *uchain;
    struct uchain urefs;
    ulist_init(&urefs);

    int size = -1;
    const uint8_t *msg;
    if (unlikely(!ubase_check(uref_block_read(uref, 0, &size, &msg)))) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        uref_free(uref);
        return;
    }

    if (!scte104m_validate(msg, size)) {
        upipe_warn(upipe, "invalid multiple operation message");
        goto upipe_ts_scte104d_handle_multiple_err;
    }

    if (scte104m_get_protocol(msg) != 0) {
        upipe_warn(upipe, "invalid multiple operation message version");
        goto upipe_ts_scte104d_handle_multiple_err;
    }

    if (scte104m_get_scte35_protocol(msg) != 0) {
        upipe_warn(upipe, "invalid multiple operation message SCTE-35 version");
        goto upipe_ts_scte104d_handle_multiple_err;
    }

    uint8_t nb = scte104m_get_message_number(msg);

    if (upipe_ts_scte104d->messages[nb] != NULL &&
        ubase_check(uref_block_equal(upipe_ts_scte104d->messages[nb], uref)))
        goto upipe_ts_scte104d_handle_multiple_ok;

    uint16_t dpi_pid_index = scte104m_get_dpi_pid_index(msg);
    struct upipe *output = upipe_ts_scte104d_find_sub(upipe, dpi_pid_index);
    if (output == NULL)
        goto upipe_ts_scte104d_handle_multiple_ok;

    const uint8_t *timestamp = scte104m_get_timestamp(msg);
    if (scte104t_get_type(timestamp) != SCTE104T_TYPE_NONE)
        upipe_warn_va(upipe, "unsupported timestamp type %"PRIu8,
                      scte104t_get_type(timestamp));

    uint8_t num_ops = scte104m_get_num_ops(msg);
    struct uref *event = NULL;
    for (uint8_t j = 0; j < num_ops; j++) {
        const uint8_t *op = scte104m_get_op(msg, j);
        uint16_t opid = scte104o_get_opid(op);
        const struct scte104_handler *handler =
            upipe_ts_scte104d_get_handler(upipe, opid);
        if (!handler) {
            upipe_warn_va(upipe, "unimplemented opid %u", opid);
            upipe_ts_scte104d_output(output, &urefs, upump_p);
            event = NULL;
            continue;
        }

        if (handler->usage != SCTE104_OP_USAGE_SUPPLEMENTAL) {
            upipe_ts_scte104d_output(output, &urefs, upump_p);
            event = NULL;
        }
        else if (!event) {
            upipe_warn_va(upipe, "orphan supplemental opid %u", opid);
            continue;
        }

        int ret = handler->handle(upipe, event ?: uref, op, &urefs);
        if (unlikely(!ubase_check(ret)))
            goto upipe_ts_scte104d_handle_multiple_err;

        if (handler->usage != SCTE104_OP_USAGE_SUPPLEMENTAL) {
            uchain = ulist_peek(&urefs);
            if (uchain)
                event = uref_from_uchain(uchain);
        }
    }
    upipe_ts_scte104d_output(output, &urefs, upump_p);

upipe_ts_scte104d_handle_multiple_ok:
    uref_block_unmap(uref, 0);
    uref_free(upipe_ts_scte104d->messages[nb]);
    upipe_ts_scte104d->messages[nb] = uref;
    return;

upipe_ts_scte104d_handle_multiple_err:
    while ((uchain = ulist_pop(&urefs)))
        uref_free(uref_from_uchain(uchain));
    uref_block_unmap(uref, 0);
    uref_free(uref);
}

/** @internal @This parses a new operation message.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_scte104d_input(struct upipe *upipe, struct uref *uref,
                                    struct upump **upump_p)
{
    struct upipe_ts_scte104d *upipe_ts_scte104d =
        upipe_ts_scte104d_from_upipe(upipe);

    uint8_t header_buffer[SCTE104_HEADER_SIZE];
    const uint8_t *header = uref_block_peek(uref, 0, SCTE104_HEADER_SIZE,
                                            header_buffer);
    if (unlikely(header == NULL)) {
        upipe_warn(upipe, "fail to get header");
        uref_free(uref);
        return;
    }

    uint16_t opid = scte104_get_opid(header);
    uint16_t size = scte104_get_size(header);
    uref_block_peek_unmap(uref, 0, header_buffer, header);

    size_t uref_size;
    if (unlikely(!ubase_check(uref_block_size(uref, &uref_size)))) {
        upipe_warn_va(upipe, "fail to get size");
        uref_free(uref);
        upipe_throw_error(upipe, UBASE_ERR_INVALID);
        return;
    }

    if (unlikely(size > uref_size)) {
        upipe_err_va(upipe, "message too long (%"PRIu16" %zu)",
                     size, uref_size);
        uref_free(uref);
        return;
    }

    if (unlikely(!ubase_check(uref_block_merge(uref,
                        upipe_ts_scte104d->ubuf_mgr, 0, -1)))) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        uref_free(uref);
        return;
    }

    if (opid == SCTE104_OPID_MULTIPLE)
        upipe_ts_scte104d_handle_multiple(upipe, uref, upump_p);
    else
        upipe_ts_scte104d_handle_single(upipe, uref, upump_p);
}

/** @internal @This receives an ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_ts_scte104d_check(struct upipe *upipe,
                                   struct uref *flow_format)
{
    if (flow_format == NULL)
        return UBASE_ERR_NONE;

    struct upipe_ts_scte104d *upipe_ts_scte104d =
        upipe_ts_scte104d_from_upipe(upipe);
    uref_free(upipe_ts_scte104d->flow_def);
    upipe_ts_scte104d->flow_def = flow_format;

    struct uchain *uchain;
    ulist_foreach (&upipe_ts_scte104d->subs, uchain) {
        struct upipe_ts_scte104d_sub *sub =
            upipe_ts_scte104d_sub_from_uchain(uchain);
        upipe_ts_scte104d_sub_build_flow_def(upipe_ts_scte104d_sub_to_upipe(sub));
    }
    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_scte104d_set_flow_def(struct upipe *upipe,
                                          struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    upipe_ts_scte104d_demand_ubuf_mgr(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_scte104d_control(struct upipe *upipe,
                                     int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_ts_scte104d_control_subs(upipe, command, args));

    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            /* We do not pass through the requests ; which output would
             * we use ? */
            return upipe_throw_provide_request(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST:
            return UBASE_ERR_NONE;
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_scte104d_set_flow_def(upipe, flow_def);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param urefcount_real pointer to urefcount_real structure
 */
static void upipe_ts_scte104d_free(struct urefcount *urefcount_real)
{
    struct upipe_ts_scte104d *upipe_ts_scte104d =
        upipe_ts_scte104d_from_urefcount_real(urefcount_real);
    struct upipe *upipe = upipe_ts_scte104d_to_upipe(upipe_ts_scte104d);
    upipe_throw_dead(upipe);

    for (int i = 0; i <= UINT8_MAX; i++)
        uref_free(upipe_ts_scte104d->messages[i]);

    uref_free(upipe_ts_scte104d->flow_def);
    upipe_ts_scte104d_clean_sub_subs(upipe);
    urefcount_clean(urefcount_real);
    upipe_ts_scte104d_clean_ubuf_mgr(upipe);
    upipe_ts_scte104d_clean_urefcount(upipe);
    upipe_ts_scte104d_free_void(upipe);
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_scte104d_no_input(struct upipe *upipe)
{
    struct upipe_ts_scte104d *upipe_ts_scte104d =
        upipe_ts_scte104d_from_upipe(upipe);
    upipe_ts_scte104d_throw_sub_subs(upipe, UPROBE_SOURCE_END);
    urefcount_release(upipe_ts_scte104d_to_urefcount_real(upipe_ts_scte104d));
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_scte104d_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_SCTE104D_SIGNATURE,

    .upipe_alloc = upipe_ts_scte104d_alloc,
    .upipe_input = upipe_ts_scte104d_input,
    .upipe_control = upipe_ts_scte104d_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_scte104d pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_scte104d_mgr_alloc(void)
{
    return &upipe_ts_scte104d_mgr;
}
