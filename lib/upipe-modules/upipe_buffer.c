/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * Authors: Arnaud de Turckheim
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
 * @short Upipe buffer module
 *
 * The buffer pipe directly forwards the input uref if it can. When the output
 * upump is blocked by the output pipe, the buffer pipe still accepts the input
 * uref until the maximum size is reached.
 */


#include <upipe/uclock.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-modules/upipe_buffer.h>

/** @internal @This throws an update event.
 *
 * @param upipe description structure of the pipe
 * @param old_state the previous state
 * @param new_state the new state
 * @return an error code
 */
static inline int upipe_buffer_throw_update(struct upipe *upipe,
                                            enum upipe_buffer_state old_state,
                                            enum upipe_buffer_state new_state)
{
    return upipe_throw(upipe, UPROBE_BUFFER_UPDATE, UPIPE_BUFFER_SIGNATURE,
                       old_state, new_state);
}

/** @internal @This is the private context of a buffer pipe. */
struct upipe_buffer {
    /** upipe structure for helper */
    struct upipe upipe;
    /** urefcount structure for helper */
    struct urefcount urefcount;
    /** upump_mgr pointer for helper */
    struct upump_mgr *upump_mgr;
    /** upump pointer for helper */
    struct upump *upump;
    /** upipe pointer for output helper */
    struct upipe *output;
    /** uref pointer for output helper to store flow def */
    struct uref *flow_def;
    /** output_state for output helper */
    enum upipe_helper_output_state output_state;
    /** uchain structure for output helper to store request list */
    struct uchain request_list;
    /** list of urefs for input helper */
    struct uchain urefs;
    /** number of urefs for input helper */
    unsigned int nb_urefs;
    /** max of urefs for input helper */
    unsigned int max_urefs;
    /** uchain structure for input helper to store blockers */
    struct uchain blockers;
    /** total size of all the pending urefs */
    size_t size;
    /** max buffered size */
    uint64_t max_size;
    /** low limit */
    uint64_t low_limit;
    /** high limit */
    uint64_t high_limit;
    /** list of buffered urefs */
    struct uchain buffered;
    /** last received dts */
    uint64_t last_dts;
    /** buffer state */
    enum upipe_buffer_state state;
};

/** @hidden */
static bool upipe_buffer_process(struct upipe *upipe,
                                 struct uref *uref,
                                 struct upump **upump_p);

UPIPE_HELPER_UPIPE(upipe_buffer, upipe, UPIPE_BUFFER_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_buffer, urefcount, upipe_buffer_free);
UPIPE_HELPER_VOID(upipe_buffer);
UPIPE_HELPER_UPUMP_MGR(upipe_buffer, upump_mgr);
UPIPE_HELPER_UPUMP(upipe_buffer, upump, upump_mgr);
UPIPE_HELPER_INPUT(upipe_buffer, urefs, nb_urefs, max_urefs, blockers,
                   upipe_buffer_process);
UPIPE_HELPER_OUTPUT(upipe_buffer, output, flow_def, output_state, request_list);

/** @internal @This allocates a buffer pipe.
 *
 * @param mgr pointer to upipe manager
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return an allocated pipe.
 */
static struct upipe *upipe_buffer_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        uint32_t signature,
                                        va_list args)
{
    struct upipe *upipe = upipe_buffer_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;
    struct upipe_buffer *upipe_buffer = upipe_buffer_from_upipe(upipe);

    upipe_buffer_init_urefcount(upipe);
    upipe_buffer_init_upump_mgr(upipe);
    upipe_buffer_init_upump(upipe);
    upipe_buffer_init_input(upipe);
    upipe_buffer_init_output(upipe);
    ulist_init(&upipe_buffer->buffered);
    upipe_buffer->size = 0;
    upipe_buffer->max_size = 0;
    upipe_buffer->low_limit = 0;
    upipe_buffer->high_limit = 0;
    upipe_buffer->last_dts = 0;
    upipe_buffer->state = UPIPE_BUFFER_LOW;

    upipe_throw_ready(upipe);

    return upipe;
}

/** @internal @This free a buffer pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_buffer_free(struct upipe *upipe)
{
    struct upipe_buffer *upipe_buffer = upipe_buffer_from_upipe(upipe);

    upipe_throw_dead(upipe);

    struct uchain *uchain;
    while ((uchain = ulist_pop(&upipe_buffer->buffered)) != NULL)
        uref_free(uref_from_uchain(uchain));

    upipe_buffer_clean_output(upipe);
    upipe_buffer_clean_input(upipe);
    upipe_buffer_clean_upump(upipe);
    upipe_buffer_clean_upump_mgr(upipe);
    upipe_buffer_clean_urefcount(upipe);
    upipe_buffer_free_void(upipe);
}

/** @internal @This gets the duration of the buffer.
 * The duration is the difference between the last received uref DTS and
 * the first retained uref DTS.
 *
 * @param upipe description structure of the pipe
 * @param duration_p a pointer filled with the duration
 * @return an error code
 */
static int upipe_buffer_get_duration(struct upipe *upipe, uint64_t *duration_p)
{
    struct upipe_buffer *upipe_buffer = upipe_buffer_from_upipe(upipe);

    struct uchain *uchain = ulist_peek(&upipe_buffer->buffered);
    if (!uchain) {
        *duration_p = 0;
        return UBASE_ERR_NONE;
    }
    struct uref *uref = uref_from_uchain(uchain);
    uint64_t date;
    if (ubase_check(uref_clock_get_dts_prog(uref, &date)) ||
        ubase_check(uref_clock_get_dts_sys(uref, &date))) {
        *duration_p = upipe_buffer->last_dts - date;
        return UBASE_ERR_NONE;
    }
    return UBASE_ERR_INVALID;
}

/** @internal @This checks if the buffer pipe state has changed.
 * If the buffer pipe state has changed then it throws an update event
 * (see @ref upipe_buffer_throw_update).
 *
 * @param upipe description structure of the pipe
 */
static void upipe_buffer_update(struct upipe *upipe)
{
    struct upipe_buffer *upipe_buffer = upipe_buffer_from_upipe(upipe);
    enum upipe_buffer_state old_state = upipe_buffer->state;
    enum upipe_buffer_state new_state;

    if (upipe_buffer->low_limit &&
        upipe_buffer->size < upipe_buffer->low_limit)
        new_state = UPIPE_BUFFER_LOW;
    else if (upipe_buffer->high_limit &&
             upipe_buffer->size >= upipe_buffer->high_limit)
        new_state = UPIPE_BUFFER_HIGH;
    else
        new_state = UPIPE_BUFFER_MIDDLE;

    if (new_state != old_state) {
        upipe_buffer->state = new_state;
        upipe_buffer_throw_update(upipe, old_state, new_state);
    }

    uint64_t duration;
    if (ubase_check(upipe_buffer_get_duration(upipe, &duration)))
        upipe_verbose_va(upipe, "duration: %zums",
                         duration / (UCLOCK_FREQ / 1000));
}

/** @internal @This is called when output pipe need some data.
 *
 * @param upump description structure of the output watcher
 */
static void upipe_buffer_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_buffer *upipe_buffer = upipe_buffer_from_upipe(upipe);

    struct uchain *uchain = ulist_pop(&upipe_buffer->buffered);
    if (!uchain)
        return;

    struct uref *uref = uref_from_uchain(uchain);
    size_t block_size;
    ubase_assert(uref_block_size(uref, &block_size));
    assert(upipe_buffer->size >= block_size);
    upipe_buffer->size -= block_size;
    upipe_buffer_update(upipe);

    upipe_buffer_output(upipe, uref, &upipe_buffer->upump);
    if (upipe_buffer_output_input(upipe))
        upipe_buffer_unblock_input(upipe);
}

/** @internal @This allocates the upump if it's not.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_buffer_upump_check(struct upipe *upipe)
{
    struct upipe_buffer *upipe_buffer = upipe_buffer_from_upipe(upipe);

    int ret = upipe_buffer_check_upump_mgr(upipe);
    if (!ubase_check(ret))
        return ret;
    if (unlikely(upipe_buffer->upump_mgr == NULL)) {
        upipe_err(upipe, "no upump manager");
        return UBASE_ERR_INVALID;
    }

    if (upipe_buffer->upump)
        return UBASE_ERR_NONE;

    struct upump *upump =
        upump_alloc_idler(upipe_buffer->upump_mgr, upipe_buffer_worker, upipe,
                          upipe->refcount);
    if (unlikely(upump == NULL))
        return UBASE_ERR_ALLOC;

    upipe_buffer_set_upump(upipe, upump);
    upump_start(upump);
    return UBASE_ERR_NONE;
}

/** @internal @This processes an input uref.
 *
 * @param upipe description structure of the pipe
 * @param uref input uref
 * @param upump_p reference to pump that generated the buffer
 * @return true if the uref has been processed, false otherwise
 */
static bool upipe_buffer_process(struct upipe *upipe,
                                 struct uref *uref,
                                 struct upump **upump_p)
{
    struct upipe_buffer *upipe_buffer = upipe_buffer_from_upipe(upipe);
    int ret;

    size_t block_size;
    ret = uref_block_size(uref, &block_size);
    if (!ubase_check(ret)) {
        upipe_err(upipe, "fail to get block size, dropping...");
        uref_free(uref);
        return true;
    }

    if (block_size + upipe_buffer->size > upipe_buffer->max_size)
        return false;

    ret = upipe_buffer_upump_check(upipe);
    if (!ubase_check(ret)) {
        upipe_err(upipe, "fail to allocate upump, dropping...");
        uref_free(uref);
        return true;
    }

    uint64_t date;
    if (ubase_check(uref_clock_get_dts_prog(uref, &date)) ||
        ubase_check(uref_clock_get_dts_sys(uref, &date))) {
        if (date < upipe_buffer->last_dts)
            upipe_warn(upipe, "uref DTS is in the past");
        upipe_buffer->last_dts = date;
    }
    else
        upipe_warn(upipe, "uref has no DTS");

    upipe_buffer->size += block_size;
    ulist_add(&upipe_buffer->buffered, uref_to_uchain(uref));
    upipe_buffer_update(upipe);
    return true;
}

/** @internal @This tries to output an input uref, holds otherwise
 *
 * @param upipe description structure of the pipe
 * @param uref input uref
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_buffer_input(struct upipe *upipe,
                               struct uref *uref,
                               struct upump **upump_p)
{
    if (!upipe_buffer_output_input(upipe) ||
        !upipe_buffer_process(upipe, uref, upump_p)) {
        upipe_buffer_hold_input(upipe, uref);
        upipe_buffer_block_input(upipe, upump_p);
    }
}

/** @internal @This sets the flow format of the buffer pipe.
 *
 * @param upipe description structure of the pipe
 * @param flow_def the flow format to set
 * @return an error code
 */
static int upipe_buffer_set_flow_def(struct upipe *upipe,
                                     struct uref *flow_def)
{
    int ret = uref_flow_match_def(flow_def, "block.");
    if (!ubase_check(ret))
        return ret;

    struct uref *flow_def_dup = uref_dup(flow_def);
    if (unlikely(flow_def_dup == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    upipe_buffer_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This gets the maximum retain size of the buffer pipe.
 *
 * @param upipe description structure of the pipe
 * @param max_size_p a pointer filled with the maximum size
 * @return an error code
 */
static int _upipe_buffer_get_max_size(struct upipe *upipe,
                                      uint64_t *max_size_p)
{
    struct upipe_buffer *upipe_buffer = upipe_buffer_from_upipe(upipe);
    if (max_size_p)
        *max_size_p = upipe_buffer->max_size;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the maximun retain size of the buffer pipe.
 * The buffer pipe will retain at most @tt {max_size} bytes before blocking
 * the input upump.
 *
 * @param upipe description structure of the pipe
 * @param max_size the maximum size to set
 * @return an error code
 */
static int _upipe_buffer_set_max_size(struct upipe *upipe,
                                      size_t max_size)
{
    struct upipe_buffer *upipe_buffer = upipe_buffer_from_upipe(upipe);
    upipe_buffer->max_size = max_size;
    return UBASE_ERR_NONE;
}

/** @internal @This gets the low limit of the buffer pipe.
 *
 * @param upipe description structure of the pipe
 * @param low_limit_p a pointer filled with the low limit
 * @return an error code
 */
static int _upipe_buffer_get_low(struct upipe *upipe, uint64_t *low_limit_p)
{
    struct upipe_buffer *upipe_buffer = upipe_buffer_from_upipe(upipe);
    if (low_limit_p)
        *low_limit_p = upipe_buffer->low_limit;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the low limit of the buffer pipe.
 *
 * @param upipe description structure of the pipe
 * @param low_limit the low limit to set
 * @return an error code
 */
static int _upipe_buffer_set_low(struct upipe *upipe, uint64_t low_limit)
{
    struct upipe_buffer *upipe_buffer = upipe_buffer_from_upipe(upipe);
    upipe_buffer->low_limit = low_limit;
    return UBASE_ERR_NONE;
}

/** @internal @This gets the high limit of the buffer pipe.
 *
 * @param upipe description structure of the pipe
 * @param high_limit_p a pointer filled with the high limit
 * @return an error code
 */
static int _upipe_buffer_get_high(struct upipe *upipe, uint64_t *high_limit_p)
{
    struct upipe_buffer *upipe_buffer = upipe_buffer_from_upipe(upipe);
    if (high_limit_p)
        *high_limit_p = upipe_buffer->high_limit;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the high limit of the buffer pipe.
 *
 * @param upipe description structure of the pipe
 * @param high_limit the high limit to set
 * @return an error code
 */
static int _upipe_buffer_set_high(struct upipe *upipe, uint64_t high_limit)
{
    struct upipe_buffer *upipe_buffer = upipe_buffer_from_upipe(upipe);
    upipe_buffer->high_limit = high_limit;
    return UBASE_ERR_NONE;
}

/** @internal @This dispatches the control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to dispatch
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_buffer_control(struct upipe *upipe,
                                int command,
                                va_list args)
{
    switch (command) {
    case UPIPE_ATTACH_UPUMP_MGR:
        upipe_buffer_set_upump(upipe, NULL);
        return upipe_buffer_attach_upump_mgr(upipe);

    case UPIPE_REGISTER_REQUEST: {
        struct urequest *urequest = va_arg(args, struct urequest *);
        return upipe_buffer_alloc_output_proxy(upipe, urequest);
    }
    case UPIPE_UNREGISTER_REQUEST: {
        struct urequest *urequest = va_arg(args, struct urequest *);
        return upipe_buffer_free_output_proxy(upipe, urequest);
    }
    case UPIPE_GET_FLOW_DEF: {
        struct uref **flow_def_p = va_arg(args, struct uref **);
        return upipe_buffer_get_flow_def(upipe, flow_def_p);
    }
    case UPIPE_SET_FLOW_DEF: {
        struct uref *flow_def = va_arg(args, struct uref *);
        return upipe_buffer_set_flow_def(upipe, flow_def);
    }
    case UPIPE_GET_OUTPUT: {
        struct upipe **output_p = va_arg(args, struct upipe **);
        return upipe_buffer_get_output(upipe, output_p);
    }
    case UPIPE_SET_OUTPUT: {
        struct upipe *output = va_arg(args, struct upipe *);
        return upipe_buffer_set_output(upipe, output);
    }

    case UPIPE_BUFFER_GET_MAX_SIZE: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_BUFFER_SIGNATURE)
        uint64_t *max_size_p = va_arg(args, uint64_t *);
        return _upipe_buffer_get_max_size(upipe, max_size_p);
    }
    case UPIPE_BUFFER_SET_MAX_SIZE: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_BUFFER_SIGNATURE)
        uint64_t max_size = va_arg(args, uint64_t);
        return _upipe_buffer_set_max_size(upipe, max_size);
    }
    case UPIPE_BUFFER_GET_LOW: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_BUFFER_SIGNATURE)
        uint64_t *low_limit_p = va_arg(args, uint64_t *);
        return _upipe_buffer_get_low(upipe, low_limit_p);
    }
    case UPIPE_BUFFER_SET_LOW: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_BUFFER_SIGNATURE)
        uint64_t low_limit = va_arg(args, uint64_t);
        return _upipe_buffer_set_low(upipe, low_limit);
    }
    case UPIPE_BUFFER_GET_HIGH: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_BUFFER_SIGNATURE)
        uint64_t *high_limit_p = va_arg(args, uint64_t *);
        return _upipe_buffer_get_high(upipe, high_limit_p);
    }
    case UPIPE_BUFFER_SET_HIGH: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_BUFFER_SIGNATURE)
        uint64_t high_limit = va_arg(args, uint64_t);
        return _upipe_buffer_set_high(upipe, high_limit);
    }
    }
    return UBASE_ERR_UNHANDLED;
}

/** @internal @This is the static buffer pipe manager. */
static struct upipe_mgr upipe_buffer_mgr = {
    .refcount = NULL,
    .signature = UPIPE_BUFFER_SIGNATURE,
    .upipe_command_str = upipe_buffer_command_str,
    .upipe_event_str = upipe_buffer_event_str,
    .upipe_alloc = upipe_buffer_alloc,
    .upipe_input = upipe_buffer_input,
    .upipe_control = upipe_buffer_control,
};

/** @This returns the static buffer pipe manager. */
struct upipe_mgr *upipe_buffer_mgr_alloc(void)
{
    return &upipe_buffer_mgr;
}
