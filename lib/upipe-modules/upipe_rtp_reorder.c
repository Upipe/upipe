/*
 * Copyright (C) 2015-2016 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
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
 * @short Upipe module to buffer and reorder rtp packets from multiple sources
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/upipe.h>
#include <upipe/uref_attr.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/uclock.h>
#include <upipe/upipe.h>
#include <upipe/ulist.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe-modules/upipe_rtp_reorder.h>

#include <bitstream/ietf/rtp.h>

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

/** @hidden */
static bool upipe_rtpr_sub_output(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p);

/** upipe_rtpr structure */
struct upipe_rtpr {
    /** real refcount management structure */
    struct urefcount urefcount_real;
    /** refcount management structure exported to the public structure */
    struct urefcount urefcount;

    /** uclock structure, if not NULL we are in live mode */
    struct uclock *uclock;
    /** uclock request */
    struct urequest uclock_request;

    /** list of input subpipes */
    struct uchain inputs;

    /** output pipe */
    struct upipe *output;
    /** input flow definition packet */
    struct uref *flow_def_input;
    /** flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    struct upump *upump;
    /** reorder timer */
    struct upump *upump2;

    /** manager to create subs */
    struct upipe_mgr sub_mgr;

    struct uchain queue;

    uint64_t last_sent_seqnum;
    uint64_t num_consecutive_late;

    /** delay to set */
    uint64_t delay;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_rtpr, upipe, UPIPE_RTPR_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_rtpr, urefcount, upipe_rtpr_no_input);
UPIPE_HELPER_VOID(upipe_rtpr);
UPIPE_HELPER_UPUMP_MGR(upipe_rtpr, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_rtpr, upump, upump_mgr)
UPIPE_HELPER_OUTPUT(upipe_rtpr, output, flow_def, output_state, request_list);
UPIPE_HELPER_UCLOCK(upipe_rtpr, uclock, uclock_request, NULL, upipe_throw_provide_request, NULL)

UBASE_FROM_TO(upipe_rtpr, urefcount, urefcount_real, urefcount_real)

/** @hidden */
static void upipe_rtpr_free(struct urefcount *urefcount_real);

/** @internal @This is the private context of an output of an rtpr
 * pipe. */
struct upipe_rtpr_sub {
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked lists */
    struct uchain uchain;

    /** flow_definition packet */
    struct uref *flow_def;

    /** temporary uref storage */
    struct uchain urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers */
    struct uchain blockers;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_rtpr_sub, upipe,
                   UPIPE_RTPR_INPUT_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_rtpr_sub, urefcount,
                       upipe_rtpr_sub_free)
UPIPE_HELPER_INPUT(upipe_rtpr_sub, urefs, nb_urefs, max_urefs, blockers, upipe_rtpr_sub_output)
UPIPE_HELPER_VOID(upipe_rtpr_sub);
UPIPE_HELPER_SUBPIPE(upipe_rtpr, upipe_rtpr_sub, input,
                     sub_mgr, inputs, uchain)

static inline bool seq_num_lt(uint16_t s1, uint16_t s2)
 {
    /* a 'less-than' on 16-bit sequence numbers */
    int diff = s2 - s1;
    if (diff > 0)
        return (diff < 0x8000);
    else if (diff < 0)
        return (diff < -0x8000);
    else
        return 0;
}

static int upipe_rtpr_sub_get_flow_def(struct upipe *upipe,
                                              struct uref **p)
{
    struct upipe_rtpr_sub *upipe_rtpr_sub =
        upipe_rtpr_sub_from_upipe(upipe);
    assert(upipe_rtpr_sub != NULL);
    *p = upipe_rtpr_sub->flow_def;
    return UBASE_ERR_NONE;
}

static int upipe_rtpr_sub_set_flow_def(struct upipe *upipe,
                                              struct uref *flow_def)
{
    struct upipe_rtpr *upipe_rtpr =
        upipe_rtpr_from_sub_mgr(upipe->mgr);
    struct upipe_rtpr_sub *upipe_rtpr_sub =
        upipe_rtpr_sub_from_upipe(upipe);

    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    flow_def = uref_dup(flow_def);
    UBASE_ALLOC_RETURN(flow_def)
    upipe_rtpr_sub->flow_def = flow_def;
    if (!upipe_rtpr->flow_def) {
        flow_def = uref_dup(flow_def);
        UBASE_ALLOC_RETURN(flow_def)
        upipe_rtpr->flow_def = flow_def;
    }
    return UBASE_ERR_NONE;
}

/** @internal @This allocates an output subpipe of an rtpr pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_rtpr_sub_alloc(struct upipe_mgr *mgr,
                                                 struct uprobe *uprobe,
                                                 uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_rtpr_sub_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL)) {
        return NULL;
    }

    struct upipe_rtpr_sub *upipe_rtpr_sub =
                            upipe_rtpr_sub_from_upipe(upipe);

    upipe_rtpr_sub_init_urefcount(upipe);
    upipe_rtpr_sub_init_input(upipe);
    upipe_rtpr_sub_init_sub(upipe);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This processes control commands on an input subpipe of an
 *  rtpr pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_rtpr_sub_control(struct upipe *upipe,
                                         int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_control_provide_request(upipe, command, args));
    UBASE_HANDLED_RETURN(upipe_rtpr_sub_control_super(upipe, command, args));

    switch (command) {
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_rtpr_sub_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_rtpr_sub_set_flow_def(upipe, flow_def);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

static void upipe_rtpr_timer(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_rtpr *rtpr = upipe_rtpr_from_upipe(upipe);
    uint64_t now = uclock_now(rtpr->uclock);
    uint64_t date_sys;
    int type;
    struct uchain *uchain, *uchain_tmp;
    struct uref *uref;

    ulist_delete_foreach(&rtpr->queue, uchain, uchain_tmp) {
        uref = uref_from_uchain(uchain);
        uref_clock_get_date_sys(uref, &date_sys, &type);
        uint64_t seqnum = 0;
        uref_attr_get_priv(uref, &seqnum);

        if (now >= date_sys || date_sys == UINT64_MAX) {
            ulist_delete(uchain);
            upipe_rtpr_output(upipe, uref, NULL);
            rtpr->last_sent_seqnum = seqnum;
        }
        else {
            break;
        }
    }
}

static void upipe_rtpr_list_add(struct upipe *upipe, struct uref *uref)
{
    struct upipe_rtpr *rtpr = upipe_rtpr_from_upipe(upipe);
    int dup = 0, ooo = 0;
    struct uchain *uchain, *uchain_tmp;

    uint8_t rtp_buffer[RTP_HEADER_SIZE];
    const uint8_t *rtp_header = uref_block_peek(uref, 0, RTP_HEADER_SIZE,
                                                rtp_buffer);

    if (unlikely(rtp_header == NULL)) {
        upipe_warn(upipe, "invalid buffer received");
        uref_free(uref);
        return;
    }
    uint16_t new_seqnum = rtp_get_seqnum(rtp_header);
    uref_attr_set_priv(uref, new_seqnum);
    uref_block_peek_unmap(uref, 0, rtp_buffer, rtp_header);

    /* Drop late packets */
    if (rtpr->last_sent_seqnum != UINT64_MAX &&
        (seq_num_lt(new_seqnum, rtpr->last_sent_seqnum) || new_seqnum == rtpr->last_sent_seqnum)) {
        uref_free(uref);
        rtpr->num_consecutive_late++;

        /* Assume new stream if too many consecutive late packets */
        if (rtpr->num_consecutive_late > 200)
            rtpr->last_sent_seqnum = UINT64_MAX;
        
        return;
    }

    rtpr->num_consecutive_late = 0;

    /* Remove date_sys for any late packets */
    ulist_delete_foreach_reverse(&rtpr->queue, uchain, uchain_tmp) {
        struct uref *cur_uref = uref_from_uchain(uchain);
        uint64_t seqnum = 0;
        uref_attr_get_priv(cur_uref, &seqnum);

        if (seq_num_lt(new_seqnum, seqnum)) {
            if (ulist_is_first(&rtpr->queue, uchain)) {
                uref_clock_delete_date_sys(uref);
                ulist_insert(uchain->prev, uchain, uref_to_uchain(uref));
                ooo = 1;
                break;
            }
            else {
                struct uref *prev_uref = uref_from_uchain(uchain->prev);
                uint64_t prev_seqnum = 0;
                uref_attr_get_priv(prev_uref, &prev_seqnum);
                if (!seq_num_lt(new_seqnum, prev_seqnum) && !(new_seqnum == prev_seqnum)) {
                    uref_clock_delete_date_sys(uref);
                    ulist_insert(uchain->prev, uchain, uref_to_uchain(uref));
                    ooo = 1;
                    break;
                }
            }
        }
        /* Duplicate packet */
        else if (new_seqnum == seqnum) {
            dup = 1;
            uref_free(uref);
            break;
        }
        else
            break;
    }


    /* Add to end if normal packet */
    if (!dup && !ooo) {
        ulist_add(&rtpr->queue, uref_to_uchain(uref));
    }
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static bool upipe_rtpr_sub_output(struct upipe *upipe, struct uref *uref,
                                         struct upump **upump_p)
{
  struct upipe_rtpr *upipe_rtpr =
        upipe_rtpr_from_sub_mgr(upipe->mgr);
    struct upipe_rtpr_sub *upipe_rtpr_sub =
                              upipe_rtpr_sub_from_upipe(upipe);

    uint64_t date_sys;
    int type;

    uref_clock_get_date_sys(uref, &date_sys, &type);
    date_sys += upipe_rtpr->delay;
    uref_clock_set_date_sys(uref, date_sys, type);

    upipe_rtpr_list_add(&upipe_rtpr->upipe, uref);

    return true;
}

/** @internal @This handles output data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 */
static void upipe_rtpr_sub_input(struct upipe *upipe, struct uref *uref,
                                        struct upump **upump_p)
{
    struct upipe_rtpr *upipe_rtpr =
        upipe_rtpr_from_sub_mgr(upipe->mgr);

     upipe_rtpr_sub_output(upipe, uref, upump_p);
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_rtpr_sub_free(struct upipe *upipe)
{
    struct upipe_rtpr_sub *upipe_rtpr_sub =
                              upipe_rtpr_sub_from_upipe(upipe);

    upipe_throw_dead(upipe);

    upipe_rtpr_sub_clean_input(upipe);
    upipe_rtpr_sub_clean_sub(upipe);
    upipe_rtpr_sub_clean_urefcount(upipe);
}

/** @internal @This initializes the output manager for an rtpr sub pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_rtpr_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_rtpr *upipe_rtpr =
                              upipe_rtpr_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_rtpr->sub_mgr;
    sub_mgr->refcount = upipe_rtpr_to_urefcount_real(upipe_rtpr);
    sub_mgr->signature = UPIPE_RTPR_INPUT_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_rtpr_sub_alloc;
    sub_mgr->upipe_input = upipe_rtpr_sub_input;
    sub_mgr->upipe_control = upipe_rtpr_sub_control;
    sub_mgr->upipe_mgr_control = NULL;
}

static void upipe_rtpr_clean_queue(struct upipe *upipe)
{
    struct upipe_rtpr *rtpr = upipe_rtpr_from_upipe(upipe);
    struct uchain *uchain, *uchain_tmp;
    struct uref *uref;

    ulist_delete_foreach(&rtpr->queue, uchain, uchain_tmp) {
        uref = uref_from_uchain(uchain);
        ulist_delete(uchain);
        uref_free(uref);
    }
}

/** @internal @This allocates a rtpr pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_rtpr_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_rtpr_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_rtpr *upipe_rtpr = upipe_rtpr_from_upipe(upipe);
    upipe_rtpr_init_urefcount(upipe);

    urefcount_init(upipe_rtpr_to_urefcount_real(upipe_rtpr),
                   upipe_rtpr_free);

    upipe_rtpr_init_upump_mgr(upipe);
    upipe_rtpr_init_upump(upipe);
    upipe_rtpr_init_uclock(upipe);
    upipe_rtpr_init_output(upipe);
    upipe_rtpr_init_sub_mgr(upipe);
    upipe_rtpr_init_sub_inputs(upipe);

    upipe_rtpr->flow_def_input = NULL;

    ulist_init(&upipe_rtpr->queue);

    upipe_rtpr->last_sent_seqnum = UINT64_MAX;
    upipe_rtpr->num_consecutive_late = 0;
    upipe_rtpr->delay = UCLOCK_FREQ/10;

    upipe_rtpr_check_upump_mgr(upipe);

    upipe_rtpr->upump2 = upump_alloc_timer(upipe_rtpr->upump_mgr,
                                            upipe_rtpr_timer, upipe, upipe->refcount,
                                            UCLOCK_FREQ/300, UCLOCK_FREQ/300);

    upump_start(upipe_rtpr->upump2);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_rtpr_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL))
        return UBASE_ERR_ALLOC;
    struct upipe_rtpr *upipe_rtpr = upipe_rtpr_from_upipe(upipe);
    uref_free(upipe_rtpr->flow_def_input);
    upipe_rtpr->flow_def_input = flow_def_dup;
    upipe_rtpr->flow_def = flow_def_dup;
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current delay being set into urefs.
 *
 * @param upipe description structure of the pipe
 * @param delay_p filled with the current delay
 * @return an error code
 */
static int _upipe_rtpr_get_delay(struct upipe *upipe, uint64_t *delay_p)
{
    struct upipe_rtpr *upipe_rtpr = upipe_rtpr_from_upipe(upipe);
    *delay_p = upipe_rtpr->delay;
    return UBASE_ERR_NONE;
}

/** @This sets the delay to set into urefs.
 *
 * @param upipe description structure of the pipe
 * @param delay delay to set
 * @return an error code
 */
static int _upipe_rtpr_set_delay(struct upipe *upipe, uint64_t delay)
{
    struct upipe_rtpr *upipe_rtpr = upipe_rtpr_from_upipe(upipe);
    upipe_rtpr->delay = delay;
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a rtpr pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_rtpr_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_rtpr_control_output(upipe, command, args));
    UBASE_HANDLED_RETURN(upipe_rtpr_control_inputs(upipe, command, args));

    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR: {
            upipe_rtpr_set_upump(upipe, NULL);
            return upipe_rtpr_attach_upump_mgr(upipe);
        }
        case UPIPE_ATTACH_UCLOCK: {
            upipe_rtpr_set_upump(upipe, NULL);
            upipe_rtpr_require_uclock(upipe);
            return UBASE_ERR_NONE;
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_rtpr_set_flow_def(upipe, flow_def);
        }
        case UPIPE_RTPR_GET_DELAY: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_RTPR_SIGNATURE)
            uint64_t *delay_p = va_arg(args, uint64_t *);
            return _upipe_rtpr_get_delay(upipe, delay_p);
        }
        case UPIPE_RTPR_SET_DELAY: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_RTPR_SIGNATURE)
            uint64_t delay = va_arg(args, uint64_t);
            return _upipe_rtpr_set_delay(upipe, delay);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_rtpr_no_input(struct upipe *upipe)
{
    struct upipe_rtpr *upipe_rtpr =
                              upipe_rtpr_from_upipe(upipe);
    urefcount_release(upipe_rtpr_to_urefcount_real(upipe_rtpr));
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_rtpr_free(struct urefcount *urefcount_real)
{
    struct upipe_rtpr *upipe_rtpr =
           upipe_rtpr_from_urefcount_real(urefcount_real);
    struct upipe *upipe = upipe_rtpr_to_upipe(upipe_rtpr);
    
    upipe_dbg_va(upipe, "releasing pipe %p", upipe);
    upipe_throw_dead(upipe);

    upump_stop(upipe_rtpr->upump2);
    upump_free(upipe_rtpr->upump2);
    upipe_rtpr_clean_queue(upipe);

    upipe_rtpr_clean_uclock(upipe);
    upipe_rtpr_clean_sub_inputs(upipe);
    if (upipe_rtpr->flow_def != NULL)
        uref_free(upipe_rtpr->flow_def);
    urefcount_clean(urefcount_real);

    upipe_rtpr_clean_upump(upipe);
    upipe_rtpr_clean_upump_mgr(upipe);

    upipe_rtpr_clean_output(upipe);
    upipe_rtpr_clean_urefcount(upipe);
    upipe_rtpr_free_void(upipe);
}

static struct upipe_mgr upipe_rtpr_mgr = {
    .refcount = NULL,
    .signature = UPIPE_RTPR_SIGNATURE,

    .upipe_alloc = upipe_rtpr_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_rtpr_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for rtpr pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_rtpr_mgr_alloc(void)
{
    return &upipe_rtpr_mgr;
}
