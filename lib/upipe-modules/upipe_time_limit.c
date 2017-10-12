/*
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
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
 * @short Upipe module blocking sources if they are too early
 */

/** @showvalue default time limit */
#define DURATION_DEFAULT (UCLOCK_FREQ)

#include <upipe/uclock.h>
#include <upipe/uref_clock.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe-modules/upipe_time_limit.h>

/** @internal @This is the private context of a time limit pipe. */
struct upipe_time_limit {
    /** upipe public structure */
    struct upipe upipe;
    /** refcount structure */
    struct urefcount urefcount;

    /** list of uref */
    struct uchain urefs;
    /** number of uref in @tt {urefs} list */
    unsigned int nb_urefs;
    /** maximum uref in @tt {urefs} list */
    unsigned int max_urefs;
    /** list of input blockers */
    struct uchain blockers;

    /** output pipe */
    struct upipe *output;
    /** output flow format */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** output request list */
    struct uchain request_list;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** upump */
    struct upump *upump;

    /** uclock */
    struct uclock *uclock;
    /** uclock request */
    struct urequest uclock_request;

    /** the time limit in @ref #UCLOCK_FREQ units */
    uint64_t time_limit;
};

/** @hidden */
static int upipe_time_limit_check(struct upipe *upipe,
                                  struct uref *flow_format);
/** @hidden */
static bool upipe_time_limit_process(struct upipe *upipe,
                                     struct uref *uref,
                                     struct upump **upump_p);

/** @hidden */
static void upipe_time_limit_wait(struct upipe *upipe);


UPIPE_HELPER_UPIPE(upipe_time_limit, upipe, UPIPE_TIME_LIMIT_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_time_limit, urefcount, upipe_time_limit_free);
UPIPE_HELPER_VOID(upipe_time_limit);
UPIPE_HELPER_INPUT(upipe_time_limit, urefs, nb_urefs, max_urefs, blockers,
                   upipe_time_limit_process);
UPIPE_HELPER_OUTPUT(upipe_time_limit, output, flow_def, output_state,
                    request_list);
UPIPE_HELPER_UPUMP_MGR(upipe_time_limit, upump_mgr);
UPIPE_HELPER_UPUMP(upipe_time_limit, upump, upump_mgr);
UPIPE_HELPER_UCLOCK(upipe_time_limit, uclock, uclock_request,
                    upipe_time_limit_check,
                    upipe_time_limit_register_output_request,
                    upipe_time_limit_unregister_output_request);

/** @internal @This allocates a time limit pipe.
 *
 * @param mgr pointer to upipe manager
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_time_limit_alloc(struct upipe_mgr *mgr,
                                            struct uprobe *uprobe,
                                            uint32_t signature,
                                            va_list args)
{
    struct upipe *upipe =
        upipe_time_limit_alloc_void(mgr, uprobe, signature, args);
    if (!upipe)
        return NULL;
    struct upipe_time_limit *upipe_time_limit =
        upipe_time_limit_from_upipe(upipe);

    upipe_time_limit_init_urefcount(upipe);
    upipe_time_limit_init_input(upipe);
    upipe_time_limit_init_output(upipe);
    upipe_time_limit_init_upump_mgr(upipe);
    upipe_time_limit_init_upump(upipe);
    upipe_time_limit_init_uclock(upipe);
    upipe_time_limit->time_limit = (uint64_t)-1;
    upipe_throw_ready(upipe);

    return upipe;
}

/** @internal @This is called when the file descriptor can be written again.
 * Unblock the sink and unqueue all queued buffers.
 *
 * @param upump description structure of the watcher
 */
static void upipe_time_limit_watcher(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    upipe_time_limit_set_upump(upipe, NULL);
    upipe_time_limit_output_input(upipe);
    upipe_time_limit_unblock_input(upipe);
    if (upipe_time_limit_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_time_limit_input. */
        upipe_release(upipe);
    }
}

/** @internal @This tries to output an uref.
 * If time limit size is reached start time limit watcher and ask to hold uref
 * and block input.
 *
 * @param upipe description structure of the pipe
 * @param uref uref to output
 * @param upump_p reference to the pump that generated the buffer
 * @return true if the uref has been outputted, false otherwise
 */
static bool upipe_time_limit_process(struct upipe *upipe, struct uref *uref,
                                     struct upump **upump_p)
{
    struct upipe_time_limit *upipe_time_limit =
        upipe_time_limit_from_upipe(upipe);
    uint64_t then;
    int type;
    uref_clock_get_date_sys(uref, &then, &type);
    if (unlikely(type == UREF_DATE_NONE)) {
        /* no date, output... */
        upipe_warn(upipe, "no date");
        upipe_time_limit_output(upipe, uref, upump_p);
        return true;
    }

    if (unlikely(upipe_time_limit->uclock == NULL)) {
        /* no clock, output... */
        upipe_warn(upipe, "no clock");
        upipe_time_limit_output(upipe, uref, upump_p);
        return true;
    }

    uint64_t now = uclock_now(upipe_time_limit->uclock);

    if (now + upipe_time_limit->time_limit >= then) {
        upipe_time_limit_output(upipe, uref, upump_p);
        return true;
    }

    /* too early, wait... */
    upipe_time_limit_wait_upump(upipe,
                                then - now - upipe_time_limit->time_limit,
                                upipe_time_limit_watcher);
    return false;
}

/** @internal @This is called when there is input data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref carrying the data
 * @param upump_p reference to the pump that generated the buffer
 */
static void upipe_time_limit_input(struct upipe *upipe,
                                   struct uref *uref,
                                   struct upump **upump_p)
{
    if (!upipe_time_limit_check_input(upipe)) {
        upipe_time_limit_hold_input(upipe, uref);
        upipe_time_limit_block_input(upipe, upump_p);
    } else if (!upipe_time_limit_process(upipe, uref, upump_p)) {
        upipe_time_limit_hold_input(upipe, uref);
        upipe_time_limit_block_input(upipe, upump_p);
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    }
}

/** @internal @This flushes all currently held buffers, and unblocks the
 * sources.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_time_limit_flush(struct upipe *upipe)
{
    if (upipe_time_limit_flush_input(upipe)) {
        upipe_time_limit_set_upump(upipe, NULL);
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_time_limit_input. */
        upipe_release(upipe);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This sets the flow format of pipe.
 *
 * @param upipe description structure of the pipe
 * @param flow_def the flow format to set
 * @return an error code
 */
static int upipe_time_limit_set_flow_def(struct upipe *upipe,
                                         struct uref *flow_def)
{
    struct uref *flow_def_dup = uref_dup(flow_def);
    if (unlikely(flow_def_dup == NULL))
        return UBASE_ERR_ALLOC;

    upipe_time_limit_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This gets the time limit in octets/s.
 *
 * @param upipe description structure of the pipe
 * @param time_limit_p pointer filled with the time limit
 * @return an error code
 */
static int _upipe_time_limit_get_limit(struct upipe *upipe,
                                       uint64_t *time_limit_p)
{
    struct upipe_time_limit *upipe_time_limit =
        upipe_time_limit_from_upipe(upipe);
    *time_limit_p = upipe_time_limit->time_limit;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the time limit in octets/s.
 *
 * @param upipe description structure of the pipe
 * @param time_limit time limit to set
 * @return an error code
 */
static int _upipe_time_limit_set_limit(struct upipe *upipe,
                                       uint64_t time_limit)
{
    struct upipe_time_limit *upipe_time_limit =
        upipe_time_limit_from_upipe(upipe);
    upipe_dbg_va(upipe, "set time limit to %"PRIu64" ms",
                 time_limit * 1000 / UCLOCK_FREQ);
    upipe_time_limit->time_limit = time_limit;
    return UBASE_ERR_NONE;
}

/** @internal @This dispatches the control commands.
 *
 * @param upipe description structure of the pipe
 * @param command command to dispatch
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_time_limit_control(struct upipe *upipe,
                                     int command,
                                     va_list args)
{
    UBASE_HANDLED_RETURN(upipe_time_limit_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR:
            return upipe_time_limit_attach_upump_mgr(upipe);

        case UPIPE_ATTACH_UCLOCK:
            upipe_time_limit_require_uclock(upipe);
            return UBASE_ERR_NONE;

        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_time_limit_set_flow_def(upipe, flow_def);
        }
        case UPIPE_FLUSH:
            return upipe_time_limit_flush(upipe);

        case UPIPE_TIME_LIMIT_SET_LIMIT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TIME_LIMIT_SIGNATURE);
            uint64_t time_limit = va_arg(args, uint64_t);
            return _upipe_time_limit_set_limit(upipe, time_limit);
        }
        case UPIPE_TIME_LIMIT_GET_LIMIT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TIME_LIMIT_SIGNATURE);
            uint64_t *time_limit_p = va_arg(args, uint64_t *);
            return _upipe_time_limit_get_limit(upipe, time_limit_p);
        }
    }
    return UBASE_ERR_UNHANDLED;
}

/** @internal @This checks upump manager and uclock.
 *
 * @param upipe description structure of the pipe
 * @param flow_format flow format
 * @return an error code
 */
static int upipe_time_limit_check(struct upipe *upipe,
                                  struct uref *flow_format)
{
    struct upipe_time_limit *upipe_time_limit =
        upipe_time_limit_from_upipe(upipe);

    int ret = upipe_time_limit_check_upump_mgr(upipe);
    if (!ubase_check(ret))
        return ret;

    if (unlikely(upipe_time_limit->uclock == NULL))
        upipe_time_limit_require_uclock(upipe);
    return UBASE_ERR_NONE;
}

/** @internal @This dispatches commands and checks upump manager and uclock.
 *
 * @param upipe description structure of the pipe
 * @param command command to dispatch
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_time_limit_control(struct upipe *upipe,
                                    int command, va_list args)
{
    UBASE_RETURN(_upipe_time_limit_control(upipe, command, args));
    return upipe_time_limit_check(upipe, NULL);
}

/** @internal @This frees the time limit pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_time_limit_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);
    upipe_time_limit_clean_uclock(upipe);
    upipe_time_limit_clean_upump(upipe);
    upipe_time_limit_clean_upump_mgr(upipe);
    upipe_time_limit_clean_output(upipe);
    upipe_time_limit_clean_input(upipe);
    upipe_time_limit_clean_urefcount(upipe);
    upipe_time_limit_free_void(upipe);
}

/** @internal @This is the static time limit pipe manager. */
static struct upipe_mgr upipe_time_limit_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TIME_LIMIT_SIGNATURE,
    .upipe_command_str = upipe_time_limit_command_str,
    .upipe_alloc = upipe_time_limit_alloc,
    .upipe_input = upipe_time_limit_input,
    .upipe_control = upipe_time_limit_control,
};

/** @This returns the static time limit pipe manager. */
struct upipe_mgr *upipe_time_limit_mgr_alloc(void)
{
    return &upipe_time_limit_mgr;
}
