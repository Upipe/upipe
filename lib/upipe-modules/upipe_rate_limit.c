/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
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
 * @short Upipe rate limit module
 */

/** @showvalue default rate limit window */
#define DURATION_DEFAULT (UCLOCK_FREQ)

#include <upipe/uclock.h>
#include <upipe/uref_attr.h>
#include <upipe/uref_block.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe-modules/upipe_rate_limit.h>

UREF_ATTR_UNSIGNED(rate_limit, size, "rate_limit.size", rate limit block size);
UREF_ATTR_UNSIGNED(rate_limit, date, "rate_limit.date", rate limit block date);

/** @internal @This is the private context of a rate limit pipe. */
struct upipe_rate_limit {
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
    /** the rate limit in octect per second */
    uint64_t rate_limit;
    /** current window size */
    uint64_t size;
    /** list of output block */
    struct uchain sent_blocks;
    /** window duration */
    uint64_t duration;
};

/** @hidden */
static int upipe_rate_limit_check(struct upipe *upipe,
                                  struct uref *flow_format);
/** @hidden */
static bool upipe_rate_limit_process(struct upipe *upipe,
                                     struct uref *uref,
                                     struct upump **upump_p);

/** @hidden */
static void upipe_rate_limit_wait(struct upipe *upipe);


UPIPE_HELPER_UPIPE(upipe_rate_limit, upipe, UPIPE_RATE_LIMIT_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_rate_limit, urefcount, upipe_rate_limit_free);
UPIPE_HELPER_VOID(upipe_rate_limit);
UPIPE_HELPER_INPUT(upipe_rate_limit, urefs, nb_urefs, max_urefs, blockers,
                   upipe_rate_limit_process);
UPIPE_HELPER_OUTPUT(upipe_rate_limit, output, flow_def, output_state,
                    request_list);
UPIPE_HELPER_UPUMP_MGR(upipe_rate_limit, upump_mgr);
UPIPE_HELPER_UPUMP(upipe_rate_limit, upump, upump_mgr);
UPIPE_HELPER_UCLOCK(upipe_rate_limit, uclock, uclock_request,
                    upipe_rate_limit_check,
                    upipe_rate_limit_register_output_request,
                    upipe_rate_limit_unregister_output_request);

/** @internal @This allocates a rate limit pipe.
 *
 * @param mgr pointer to upipe manager
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_rate_limit_alloc(struct upipe_mgr *mgr,
                                            struct uprobe *uprobe,
                                            uint32_t signature,
                                            va_list args)
{
    struct upipe *upipe =
        upipe_rate_limit_alloc_void(mgr, uprobe, signature, args);
    if (!upipe)
        return NULL;
    struct upipe_rate_limit *upipe_rate_limit =
        upipe_rate_limit_from_upipe(upipe);

    upipe_rate_limit_init_urefcount(upipe);
    upipe_rate_limit_init_input(upipe);
    upipe_rate_limit_init_output(upipe);
    upipe_rate_limit_init_upump_mgr(upipe);
    upipe_rate_limit_init_upump(upipe);
    upipe_rate_limit_init_uclock(upipe);
    upipe_rate_limit->rate_limit = (uint64_t)-1;
    upipe_rate_limit->size = 0;
    upipe_rate_limit->duration = DURATION_DEFAULT;
    ulist_init(&upipe_rate_limit->sent_blocks);
    upipe_throw_ready(upipe);

    return upipe;
}

/** @internal @This frees the rate limit pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_rate_limit_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);
    upipe_rate_limit_clean_uclock(upipe);
    upipe_rate_limit_clean_upump(upipe);
    upipe_rate_limit_clean_upump_mgr(upipe);
    upipe_rate_limit_clean_output(upipe);
    upipe_rate_limit_clean_input(upipe);
    upipe_rate_limit_clean_urefcount(upipe);
    upipe_rate_limit_free_void(upipe);
}

static void upipe_rate_limit_flush(struct upipe *upipe)
{
    struct upipe_rate_limit *upipe_rate_limit =
        upipe_rate_limit_from_upipe(upipe);

    uint64_t now = 0;
    if (upipe_rate_limit->uclock)
       now = uclock_now(upipe_rate_limit->uclock);

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach(&upipe_rate_limit->sent_blocks, uchain, uchain_tmp) {
        struct uref *uref = uref_from_uchain(uchain);

        uint64_t date;
        if (ubase_check(uref_rate_limit_get_date(uref, &date)) &&
            upipe_rate_limit->duration + date >= now)
            break;

        uint64_t size = 0;
        uref_rate_limit_get_size(uref, &size);
        upipe_rate_limit->size -= size;
        ulist_delete(uchain);
        uref_free(uref);
    }
}

/** @internal @This is called to update rate limit size.
 * The input upump is unblocked if all pending urefs has been outputted,
 * or it sleeps again.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_rate_limit_wake(struct upipe *upipe)
{
    upipe_rate_limit_flush(upipe);

    upipe_use(upipe);
    bool unblock = upipe_rate_limit_output_input(upipe);
    bool single = upipe_single(upipe);
    upipe_release(upipe);
    if (unlikely(single))
        return;

    if (unblock)
        upipe_rate_limit_unblock_input(upipe);
    else
        upipe_rate_limit_wait(upipe);
}

/** @internal @This is called when the timer end.
 *
 * @param upump description structure of timer
 */
static void upipe_rate_limit_wake_upump(struct upump *upump)
{
    return upipe_rate_limit_wake(upump_get_opaque(upump, struct upipe *));
}

/** @internal @This waits to rate limit the output.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_rate_limit_wait(struct upipe *upipe)
{
    struct upipe_rate_limit *upipe_rate_limit =
        upipe_rate_limit_from_upipe(upipe);

    assert(upipe_rate_limit->uclock != NULL);
    uint64_t now = uclock_now(upipe_rate_limit->uclock);

    struct uchain *uchain = ulist_peek(&upipe_rate_limit->sent_blocks);
    assert(uchain != NULL);

    struct uref *uref = uref_from_uchain(uchain);
    uint64_t date;
    ubase_assert(uref_rate_limit_get_date(uref, &date));
    assert(now >= date);
    uint64_t timeout = 1;
    if (likely(upipe_rate_limit->duration > now - date))
        timeout = upipe_rate_limit->duration - (now - date);
    upipe_verbose_va(upipe, "wait %"PRIu64"ms", timeout / (UCLOCK_FREQ / 1000));
    upipe_rate_limit_wait_upump(upipe, timeout, upipe_rate_limit_wake_upump);
}

/** @internal @This tries to output an uref.
 * If rate limit size is reached start rate limit watcher and ask to hold uref
 * and block input.
 *
 * @param upipe description structure of the pipe
 * @param uref uref to output
 * @param upump_p reference to the pump that generated the buffer
 * @return true if the uref has been outputted, false otherwise
 */
static bool upipe_rate_limit_process(struct upipe *upipe,
                                     struct uref *uref,
                                     struct upump **upump_p)
{
    struct upipe_rate_limit *upipe_rate_limit =
        upipe_rate_limit_from_upipe(upipe);

    if (upipe_rate_limit->rate_limit == (uint64_t)-1) {
        /* no rate limit set, output... */
        upipe_rate_limit_output(upipe, uref, upump_p);
        return true;
    }

    if (unlikely(upipe_rate_limit->uclock == NULL)) {
        /* no clock, output... */
        upipe_warn(upipe, "no clock");
        upipe_rate_limit_output(upipe, uref, upump_p);
        return true;
    }

    uint64_t now = uclock_now(upipe_rate_limit->uclock);
    size_t size = 0;
    uref_block_size(uref, &size);

    if (upipe_rate_limit->size &&
        (upipe_rate_limit->size + size) * UCLOCK_FREQ /
        upipe_rate_limit->duration > upipe_rate_limit->rate_limit) {
        /* rate limit is reached, wait... */
        upipe_rate_limit_wait(upipe);
        return false;
    }

    struct uref *uref_ctrl = uref_alloc_control(uref->mgr);
    if (unlikely(uref_ctrl == NULL) ||
        !ubase_check(uref_rate_limit_set_size(uref_ctrl, size)) ||
        !ubase_check(uref_rate_limit_set_date(uref_ctrl, now))) {
        uref_free(uref);
        uref_free(uref_ctrl);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return true;
    }

    ulist_add(&upipe_rate_limit->sent_blocks, uref_to_uchain(uref_ctrl));
    upipe_rate_limit->size += size;
    upipe_rate_limit_output(upipe, uref, upump_p);
    return true;
}

/** @internal @This is called when there is input data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref carrying the data
 * @param upump_p reference to the pump that generated the buffer
 */
static void upipe_rate_limit_input(struct upipe *upipe,
                                   struct uref *uref,
                                   struct upump **upump_p)
{
    if (!upipe_rate_limit_output_input(upipe) ||
        !upipe_rate_limit_process(upipe, uref, upump_p)) {
        upipe_rate_limit_hold_input(upipe, uref);
        upipe_rate_limit_block_input(upipe, upump_p);
    }
}

/** @internal @This sets the flow format of pipe.
 *
 * @param upipe description structure of the pipe
 * @param flow_def the flow format to set
 * @return an error code
 */
static int upipe_rate_limit_set_flow_def(struct upipe *upipe,
                                         struct uref *flow_def)
{
    struct uref *flow_def_dup = uref_dup(flow_def);
    if (unlikely(flow_def_dup == NULL))
        return UBASE_ERR_ALLOC;

    upipe_rate_limit_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This gets the rate limit in octets/s.
 *
 * @param upipe description structure of the pipe
 * @param rate_limit_p pointer filled with the rate limit
 * @return an error code
 */
static int _upipe_rate_limit_get_limit(struct upipe *upipe,
                                       uint64_t *rate_limit_p)
{
    struct upipe_rate_limit *upipe_rate_limit =
        upipe_rate_limit_from_upipe(upipe);
    *rate_limit_p = upipe_rate_limit->rate_limit;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the rate limit in octets/s.
 *
 * @param upipe description structure of the pipe
 * @param rate_limit rate limit to set
 * @return an error code
 */
static int _upipe_rate_limit_set_limit(struct upipe *upipe,
                                       uint64_t rate_limit)
{
    struct upipe_rate_limit *upipe_rate_limit =
        upipe_rate_limit_from_upipe(upipe);
    upipe_dbg_va(upipe, "set rate limit to %"PRIu64" bytes/s", rate_limit);
    upipe_rate_limit->rate_limit = rate_limit;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the rate limit window.
 *
 * @param upipe description structure of the pipe
 * @param duration window duration in clock tick
 * @return an error code
 */
static int _upipe_rate_limit_set_duration(struct upipe *upipe,
                                          uint64_t duration)
{
    struct upipe_rate_limit *upipe_rate_limit =
        upipe_rate_limit_from_upipe(upipe);
    upipe_rate_limit->duration = duration;
    return UBASE_ERR_NONE;
}

/** @internal @This gets the rate limit window.
 *
 * @param upipe description structure of the pipe
 * @param duration_p pointer filled with the window duration in clock tick
 * @return an error code
 */
static int _upipe_rate_limit_get_duration(struct upipe *upipe,
                                          uint64_t *duration_p)
{
    struct upipe_rate_limit *upipe_rate_limit =
        upipe_rate_limit_from_upipe(upipe);
    if (duration_p)
        *duration_p = upipe_rate_limit->duration;
    return UBASE_ERR_NONE;
}

/** @internal @This dispatches the control commands.
 *
 * @param upipe description structure of the pipe
 * @param command command to dispatch
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_rate_limit_control(struct upipe *upipe,
                                     int command,
                                     va_list args)
{
    switch (command) {
    case UPIPE_REGISTER_REQUEST: {
        struct urequest *request = va_arg(args, struct urequest *);
        return upipe_rate_limit_alloc_output_proxy(upipe, request);
    }
    case UPIPE_UNREGISTER_REQUEST: {
        struct urequest *request = va_arg(args, struct urequest *);
        return upipe_rate_limit_free_output_proxy(upipe, request);
    }

    case UPIPE_ATTACH_UPUMP_MGR:
        return upipe_rate_limit_attach_upump_mgr(upipe);

    case UPIPE_ATTACH_UCLOCK:
        upipe_rate_limit_require_uclock(upipe);
        return UBASE_ERR_NONE;

    case UPIPE_GET_OUTPUT: {
        struct upipe **output_p = va_arg(args, struct upipe **);
        return upipe_rate_limit_get_output(upipe, output_p);
    }
    case UPIPE_SET_OUTPUT: {
        struct upipe *output = va_arg(args, struct upipe *);
        return upipe_rate_limit_set_output(upipe, output);
    }
    case UPIPE_GET_FLOW_DEF: {
        struct uref **flow_def_p = va_arg(args, struct uref **);
        return upipe_rate_limit_get_flow_def(upipe, flow_def_p);
    }
    case UPIPE_SET_FLOW_DEF: {
        struct uref *flow_def = va_arg(args, struct uref *);
        return upipe_rate_limit_set_flow_def(upipe, flow_def);
    }

    case UPIPE_RATE_LIMIT_SET_LIMIT: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_RATE_LIMIT_SIGNATURE);
        uint64_t rate_limit = va_arg(args, uint64_t);
        return _upipe_rate_limit_set_limit(upipe, rate_limit);
    }
    case UPIPE_RATE_LIMIT_GET_LIMIT: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_RATE_LIMIT_SIGNATURE);
        uint64_t *rate_limit_p = va_arg(args, uint64_t *);
        return _upipe_rate_limit_get_limit(upipe, rate_limit_p);
    }
    case UPIPE_RATE_LIMIT_SET_DURATION: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_RATE_LIMIT_SIGNATURE);
        uint64_t duration = va_arg(args, uint64_t);
        return _upipe_rate_limit_set_duration(upipe, duration);
    }
    case UPIPE_RATE_LIMIT_GET_DURATION: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_RATE_LIMIT_SIGNATURE);
        uint64_t *duration_p = va_arg(args, uint64_t *);
        return _upipe_rate_limit_get_duration(upipe, duration_p);
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
static int upipe_rate_limit_check(struct upipe *upipe,
                                  struct uref *flow_format)
{
    struct upipe_rate_limit *upipe_rate_limit =
        upipe_rate_limit_from_upipe(upipe);

    int ret = upipe_rate_limit_check_upump_mgr(upipe);
    if (!ubase_check(ret))
        return ret;

    if (unlikely(upipe_rate_limit->uclock == NULL))
        upipe_rate_limit_require_uclock(upipe);
    return UBASE_ERR_NONE;
}

/** @internal @This dispatches commands and checks upump manager and uclock.
 *
 * @param upipe description structure of the pipe
 * @param command command to dispatch
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_rate_limit_control(struct upipe *upipe,
                                    int command,
                                    va_list args)
{
    UBASE_RETURN(_upipe_rate_limit_control(upipe, command, args));
    return upipe_rate_limit_check(upipe, NULL);
}

/** @internal @This is the static rate limit pipe manager. */
struct upipe_mgr upipe_rate_limit_mgr = {
    .refcount = NULL,
    .signature = UPIPE_RATE_LIMIT_SIGNATURE,
    .upipe_command_str = upipe_rate_limit_command_str,
    .upipe_alloc = upipe_rate_limit_alloc,
    .upipe_input = upipe_rate_limit_input,
    .upipe_control = upipe_rate_limit_control,
};

/** @This returns the static rate limit pipe manager. */
struct upipe_mgr *upipe_rate_limit_mgr_alloc(void)
{
    return &upipe_rate_limit_mgr;
}
