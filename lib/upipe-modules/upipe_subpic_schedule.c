/*
 * Copyright (C) 2016 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
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
 * @short Upipe subpic schedule module
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uclock.h>
#include <upipe/upipe.h>
#include <upipe/udict.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe-ts/uref_ts_flow.h>
#include <upipe-modules/upipe_subpic_schedule.h>

/** upipe_subpic_schedule structure */
struct upipe_subpic_schedule {
    /** real refcount management structure */
    struct urefcount urefcount_real;
    /** refcount management structure exported to the public structure */
    struct urefcount urefcount;

    /** pipe acting as output */
    struct upipe *output;
    /** flow definition packet */
    struct uref *flow_def;
    /** attributes / parameters from application */
    struct uref *flow_def_params;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** list of subpipes */
    struct uchain subs;

    /** frame duration */
    uint64_t frame_duration;

    /** manager to create output subpipes */
    struct upipe_mgr sub_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

/** upipe_subpic_schedule_sub structure */
struct upipe_subpic_schedule_sub {
    /** refcount management structure */
    struct urefcount urefcount;

    /** pipe acting as output */
    struct upipe *output;
    /** flow definition packet */
    struct uref *flow_def;
    /** attributes / parameters from application */
    struct uref *flow_def_params;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** if this stream is teletext */
    bool teletext;

    /** structure for double-linked lists */
    struct uchain uchain;

    /** buffered urefs */
    struct uchain urefs;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_subpic_schedule, upipe, UPIPE_SUBPIC_SCHEDULE_SIGNATURE);
UPIPE_HELPER_OUTPUT(upipe_subpic_schedule, output, flow_def, output_state, request_list)
UPIPE_HELPER_UREFCOUNT(upipe_subpic_schedule, urefcount, upipe_subpic_schedule_no_input)
UPIPE_HELPER_VOID(upipe_subpic_schedule);

UPIPE_HELPER_UPIPE(upipe_subpic_schedule_sub, upipe, UPIPE_SUBPIC_SCHEDULE_SUB_SIGNATURE);
UPIPE_HELPER_OUTPUT(upipe_subpic_schedule_sub, output, flow_def, output_state, request_list)
UPIPE_HELPER_UREFCOUNT(upipe_subpic_schedule_sub, urefcount, upipe_subpic_schedule_sub_free)
UPIPE_HELPER_VOID(upipe_subpic_schedule_sub);

UPIPE_HELPER_SUBPIPE(upipe_subpic_schedule, upipe_subpic_schedule_sub, sub,
                     sub_mgr, subs, uchain)

UBASE_FROM_TO(upipe_subpic_schedule, urefcount, urefcount_real, urefcount_real)

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_subpic_schedule_sub_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);
    struct upipe_subpic_schedule_sub *upipe_subpic_schedule_sub = upipe_subpic_schedule_sub_from_upipe(upipe);
    for (;;) {
        struct uchain *uchain = ulist_pop(&upipe_subpic_schedule_sub->urefs);
        if (!uchain)
            break;
        uref_free(uref_from_uchain(uchain));
    }
    upipe_subpic_schedule_sub_clean_urefcount(upipe);
    upipe_subpic_schedule_sub_clean_output(upipe);
    upipe_subpic_schedule_sub_clean_sub(upipe);
    upipe_subpic_schedule_sub_free_void(upipe);
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_subpic_schedule_sub_control(struct upipe *upipe, int command, va_list args)
{
    struct upipe_subpic_schedule_sub *upipe_subpic_schedule_sub = upipe_subpic_schedule_sub_from_upipe(upipe);

    UBASE_HANDLED_RETURN(
        upipe_subpic_schedule_sub_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *uref = va_arg(args, struct uref *);
            upipe_subpic_schedule_sub->teletext =
                ubase_check(uref_ts_flow_get_telx_type(uref, NULL, 0));
            upipe_subpic_schedule_sub_store_flow_def(upipe, uref_dup(uref));
            return UBASE_ERR_NONE;
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This schedules sub pictures
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_subpic_schedule_sub_input(struct upipe *upipe, struct uref *uref, struct upump **upump_p)
{
    struct upipe_subpic_schedule_sub *upipe_subpic_schedule_sub = upipe_subpic_schedule_sub_from_upipe(upipe);

    uint64_t pts;
    if (!ubase_check(uref_clock_get_pts_prog(uref, &pts))) {
        upipe_err(upipe, "Undated sub picture");
        uref_free(uref);
        return;
    }

    ulist_add(&upipe_subpic_schedule_sub->urefs, &uref->uchain);
}

/** @internal @This allocates a subpic schedule_sub pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_subpic_schedule_sub_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_subpic_schedule_sub_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_subpic_schedule_sub *upipe_subpic_schedule_sub = upipe_subpic_schedule_sub_from_upipe(upipe);
    ulist_init(&upipe_subpic_schedule_sub->urefs);

    upipe_subpic_schedule_sub_init_urefcount(upipe);
    upipe_subpic_schedule_sub_init_output(upipe);
    upipe_subpic_schedule_sub_init_sub(upipe);
    upipe_throw_ready(upipe);
    return upipe;
}


/** @internal @This initializes the output manager for an subpic_schedule sub pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_subpic_schedule_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_subpic_schedule *upipe_subpic_schedule =
        upipe_subpic_schedule_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_subpic_schedule->sub_mgr;
    sub_mgr->refcount = upipe_subpic_schedule_to_urefcount_real(upipe_subpic_schedule);
    sub_mgr->signature = UPIPE_SUBPIC_SCHEDULE_SUB_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_subpic_schedule_sub_alloc;
    sub_mgr->upipe_input = upipe_subpic_schedule_sub_input;
    sub_mgr->upipe_control = upipe_subpic_schedule_sub_control;
    sub_mgr->upipe_mgr_control = NULL;
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_subpic_schedule_free(struct urefcount *urefcount_real)
{
    struct upipe_subpic_schedule *upipe_subpic_schedule =
        upipe_subpic_schedule_from_urefcount_real(urefcount_real);
    struct upipe *upipe = upipe_subpic_schedule_to_upipe(upipe_subpic_schedule);

    upipe_throw_dead(upipe);
    upipe_subpic_schedule_clean_sub_subs(upipe);
    upipe_subpic_schedule_clean_urefcount(upipe);
    upipe_subpic_schedule_clean_output(upipe);
    upipe_subpic_schedule_free_void(upipe);
}

/** @internal @This allocates a subpic schedule pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_subpic_schedule_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_subpic_schedule_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_subpic_schedule *upipe_subpic_schedule = upipe_subpic_schedule_from_upipe(upipe);
    upipe_subpic_schedule_init_sub_subs(upipe);
    upipe_subpic_schedule_init_urefcount(upipe);
    urefcount_init(upipe_subpic_schedule_to_urefcount_real(upipe_subpic_schedule),
            upipe_subpic_schedule_free);
    upipe_subpic_schedule_init_output(upipe);
    upipe_subpic_schedule_init_sub_mgr(upipe);
    upipe_throw_ready(&upipe_subpic_schedule->upipe);
    return &upipe_subpic_schedule->upipe;
}

/** @internal @This selects sub pictures to be output
 */
static void upipe_subpic_schedule_sub_handle_subpic(struct upipe *upipe,
        uint64_t date)
{
    struct upipe_subpic_schedule_sub *upipe_subpic_schedule_sub =
        upipe_subpic_schedule_sub_from_upipe(upipe);

    struct upipe_subpic_schedule *upipe_subpic_schedule = upipe_subpic_schedule_from_sub_mgr(upipe->mgr);

    const bool teletext = upipe_subpic_schedule_sub->teletext;

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach(&upipe_subpic_schedule_sub->urefs, uchain, uchain_tmp) {
        struct uref *uref = uref_from_uchain(uchain);

        uint64_t date_uref = 0;
        uref_clock_get_pts_prog(uref, &date_uref);

        if (date_uref > date) /* The next subpicture is in advance */
            break;

        if (teletext && uchain->next != &upipe_subpic_schedule_sub->urefs) {
            /* For teletext, the next subpicture replaces the previous one.
             * There can not be multiple active subpictures. */
            struct uref *uref_next = uref_from_uchain(uchain->next);

            uint64_t date_uref_next = 0;
            uref_clock_get_pts_prog(uref_next, &date_uref_next);

            if (date_uref_next <= date) {
                ulist_delete(uchain);
                uref_free(uref);
                upipe_verbose_va(upipe, "subpicture replaced");
                continue;
            }
        }

        uint64_t duration;
        if (unlikely(!ubase_check(uref_clock_get_duration(uref, &duration))))
            duration = 0;
        if (duration == 0)
            duration = upipe_subpic_schedule->frame_duration;

        uint64_t pts_end = date_uref + duration;

        if (pts_end < date) {
            ulist_delete(uchain);
            uref_free(uref);
            upipe_verbose_va(upipe, "subpicture elapsed");
            continue;
        }

        if (uref->ubuf)
            upipe_subpic_schedule_sub_output(upipe, uref_dup(uref), NULL);
    }
}

/** @internal @This schedules sub pictures
 */
static void upipe_subpic_schedule_handle_subpics(struct upipe *upipe, uint64_t date)
{
    struct upipe_subpic_schedule *upipe_subpic_schedule = upipe_subpic_schedule_from_upipe(upipe);

    /* interate through input subpipes */
    struct uchain *uchain;
    ulist_foreach(&upipe_subpic_schedule->subs, uchain) {
        struct upipe_subpic_schedule_sub *upipe_subpic_schedule_sub =
            upipe_subpic_schedule_sub_from_uchain(uchain);
        struct upipe *sub = &upipe_subpic_schedule_sub->upipe;

        upipe_subpic_schedule_sub_handle_subpic(sub, date);
    }
}

/** @internal @This schedules sub pictures
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_subpic_schedule_input(struct upipe *upipe, struct uref *uref, struct upump **upump_p)
{
    uint64_t date;
    if (!ubase_check(uref_clock_get_pts_prog(uref, &date))) {
        upipe_warn(upipe, "undated uref");
        uref_free(uref);
        return;
    }

    upipe_subpic_schedule_handle_subpics(upipe, date);

    upipe_subpic_schedule_output(upipe, uref, upump_p);
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_subpic_schedule_control(struct upipe *upipe, int command, va_list args)
{
    struct upipe_subpic_schedule *upipe_subpic_schedule = upipe_subpic_schedule_from_upipe(upipe);

    UBASE_HANDLED_RETURN(
        upipe_subpic_schedule_control_output(upipe, command, args));
    UBASE_HANDLED_RETURN(
        upipe_subpic_schedule_control_subs(upipe, command, args));

    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *uref = va_arg(args, struct uref *);
            struct urational fps;
            if (!ubase_check(uref_pic_flow_get_fps(uref, &fps))) {
                upipe_subpic_schedule->frame_duration = 0;
            } else {
                upipe_subpic_schedule->frame_duration =
                    fps.den * UCLOCK_FREQ / fps.num;
            }
            upipe_subpic_schedule_store_flow_def(upipe, uref_dup(uref));
            return UBASE_ERR_NONE;
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_subpic_schedule_no_input(struct upipe *upipe)
{
    struct upipe_subpic_schedule *upipe_subpic_schedule =
        upipe_subpic_schedule_from_upipe(upipe);
    upipe_subpic_schedule_throw_sub_subs(upipe, UPROBE_SOURCE_END);
    urefcount_release(upipe_subpic_schedule_to_urefcount_real(upipe_subpic_schedule));
}

/** upipe_subpic_schedule */
static struct upipe_mgr upipe_subpic_schedule_mgr = {
    .refcount = NULL,
    .signature = UPIPE_SUBPIC_SCHEDULE_SIGNATURE,

    .upipe_alloc = upipe_subpic_schedule_alloc,
    .upipe_input = upipe_subpic_schedule_input,
    .upipe_control = upipe_subpic_schedule_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for subpic schedule pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_subpic_schedule_mgr_alloc(void)
{
    return &upipe_subpic_schedule_mgr;
}
