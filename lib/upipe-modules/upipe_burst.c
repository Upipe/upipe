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
 * @short Upipe burst module
 *
 * The burst pipe makes sure that an entire block is read without blocking
 * the pump.
 */

#include <upipe/uref_block.h>
#include <upipe/ueventfd.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe-modules/upipe_burst.h>

/** @internal @This stores the private context of a burst pipe. */
struct upipe_burst {
    /** public pipe */
    struct upipe upipe;
    /** refcount structure */
    struct urefcount urefcount;
    /** real refcount */
    struct urefcount urefcount_real;
    /** output pipe */
    struct upipe *output;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** output flow format */
    struct uref *flow_def;
    /** output request list */
    struct uchain requests;
    /** list of hold uref */
    struct uchain urefs;
    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** burst pump */
    struct upump *upump;
    /** ueventfd for pump */
    struct ueventfd ueventfd;
    /** upipe burst state */
    bool empty;
};

UBASE_FROM_TO(upipe_burst, urefcount, urefcount_real, urefcount_real);

/** @hidden */
static void upipe_burst_free(struct urefcount *urefcount);

UPIPE_HELPER_UPIPE(upipe_burst, upipe, UPIPE_BURST_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_burst, urefcount, upipe_burst_no_ref);
UPIPE_HELPER_VOID(upipe_burst);
UPIPE_HELPER_OUTPUT(upipe_burst, output, flow_def, output_state, requests);
UPIPE_HELPER_UPUMP_MGR(upipe_burst, upump_mgr);
UPIPE_HELPER_UPUMP(upipe_burst, upump, upump_mgr);

/** @hidden */
static int upipe_burst_throw_update(struct upipe *upipe, bool empty)
{
    struct upipe_burst *upipe_burst = upipe_burst_from_upipe(upipe);

    if (unlikely(upipe_burst->empty == empty))
        return UBASE_ERR_NONE;
    upipe_burst->empty = empty;

    upipe_dbg_va(upipe, "throw update %s", empty ? "empty" : "not empty");
    return upipe_throw(upipe, UPROBE_BURST_UPDATE, UPIPE_BURST_SIGNATURE,
                       empty);
}

/** @internal @This allocates a burst pipe.
 *
 * @param mgr management structure for this pipe type
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return an allocated pipe
 */
static struct upipe *upipe_burst_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature,
                                       va_list args)
{
    struct upipe *upipe = upipe_burst_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_burst_init_urefcount(upipe);
    upipe_burst_init_output(upipe);
    upipe_burst_init_upump_mgr(upipe);
    upipe_burst_init_upump(upipe);

    struct upipe_burst *upipe_burst = upipe_burst_from_upipe(upipe);
    urefcount_init(&upipe_burst->urefcount_real, upipe_burst_free);
    ulist_init(&upipe_burst->urefs);
    ueventfd_init(&upipe_burst->ueventfd, false);
    upipe_burst->empty = true;

    upipe_throw_ready(upipe);

    return upipe;
}

/** @internal @This frees a burst pipe.
 *
 * @param urefcount pointer to real urefcount structure
 */
static void upipe_burst_free(struct urefcount *urefcount)
{
    struct upipe_burst *upipe_burst =
        upipe_burst_from_urefcount_real(urefcount);
    struct upipe *upipe = upipe_burst_to_upipe(upipe_burst);

    upipe_throw_dead(upipe);

    struct uchain *uchain;
    while ((uchain = ulist_pop(&upipe_burst->urefs)))
        uref_free(uref_from_uchain(uchain));

    urefcount_clean(&upipe_burst->urefcount_real);
    ueventfd_clean(&upipe_burst->ueventfd);
    upipe_burst_clean_upump(upipe);
    upipe_burst_clean_upump_mgr(upipe);
    upipe_burst_clean_output(upipe);
    upipe_burst_clean_urefcount(upipe);
    upipe_burst_free_void(upipe);
}

/** @internal @This is called when there is no external reference to the pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_burst_no_ref(struct upipe *upipe)
{
    struct upipe_burst *upipe_burst = upipe_burst_from_upipe(upipe);
    urefcount_release(&upipe_burst->urefcount_real);
}

/** @internal @This is called to output the data.
 *
 * @param upump description structure of the event watcher
 */
static void upipe_burst_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_burst *upipe_burst = upipe_burst_from_upipe(upipe);

    ueventfd_read(&upipe_burst->ueventfd);
    if (unlikely(ulist_empty(&upipe_burst->urefs))) {
        upipe_burst_throw_update(upipe, true);
    }
    else {
        struct uref *uref = uref_from_uchain(ulist_pop(&upipe_burst->urefs));
        ueventfd_write(&upipe_burst->ueventfd);
        upipe_burst_output(upipe, uref, &upump);
    }
}

/** @internal @This is called when there is input data.
 *
 * @param upipe description structure of the pipe
 * @param uref input uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_burst_input(struct upipe *upipe, struct uref *uref,
                              struct upump **upump_p)
{
    struct upipe_burst *upipe_burst = upipe_burst_from_upipe(upipe);

    ulist_add(&upipe_burst->urefs, uref_to_uchain(uref));
    upipe_burst_throw_update(upipe, false);
    ueventfd_write(&upipe_burst->ueventfd);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_burst_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (unlikely(!ubase_check(uref_flow_match_def(flow_def, "block."))))
        return UBASE_ERR_INVALID;

    struct uref *flow_def_dup = uref_dup(flow_def);
    if (unlikely(flow_def_dup == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    upipe_burst_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This allocates the upump if needed.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_burst_check(struct upipe *upipe)
{
    struct upipe_burst *upipe_burst = upipe_burst_from_upipe(upipe);

    upipe_burst_check_upump_mgr(upipe);
    if (unlikely(upipe_burst->upump_mgr == NULL))
        return UBASE_ERR_INVALID;

    if (unlikely(upipe_burst->upump == NULL)) {
        struct upump *upump = ueventfd_upump_alloc(
            &upipe_burst->ueventfd, upipe_burst->upump_mgr,
            upipe_burst_worker, upipe, upipe->refcount);
        if (unlikely(upump == NULL))
            return UBASE_ERR_UPUMP;
        upump_start(upump);
        upipe_burst_set_upump(upipe, upump);
        if (unlikely(!ulist_empty(&upipe_burst->urefs)))
            ueventfd_write(&upipe_burst->ueventfd);
    }

    return UBASE_ERR_NONE;
}

/** @internal @This dispatches commands to the pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args optional arguments
 * @return an error code
 */
static int _upipe_burst_control(struct upipe *upipe, int command,
                                va_list args)
{
    switch (command) {
    case UPIPE_ATTACH_UPUMP_MGR:
        upipe_burst_set_upump(upipe, NULL);
        return upipe_burst_attach_upump_mgr(upipe);

    case UPIPE_REGISTER_REQUEST: {
        struct urequest *urequest = va_arg(args, struct urequest *);
        return upipe_burst_alloc_output_proxy(upipe, urequest);
    }
    case UPIPE_UNREGISTER_REQUEST: {
        struct urequest *urequest = va_arg(args, struct urequest *);
        return upipe_burst_free_output_proxy(upipe, urequest);
    }
    case UPIPE_GET_FLOW_DEF: {
        struct uref **flow_def_p = va_arg(args, struct uref **);
        return upipe_burst_get_flow_def(upipe, flow_def_p);
    }
    case UPIPE_SET_FLOW_DEF: {
        struct uref *flow_def = va_arg(args, struct uref *);
        return upipe_burst_set_flow_def(upipe, flow_def);
    }
    case UPIPE_GET_OUTPUT: {
        struct upipe **output_p = va_arg(args, struct upipe **);
        return upipe_burst_get_output(upipe, output_p);
    }
    case UPIPE_SET_OUTPUT: {
        struct upipe *output = va_arg(args, struct upipe *);
        return upipe_burst_set_output(upipe, output);
    }
    }

    return UBASE_ERR_UNHANDLED;
}

/** @internal @This dispatches commands to the pipe and allocates the pump
 * if needed.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args optional arguments
 * @return an error code
 */
static int upipe_burst_control(struct upipe *upipe, int command,
                               va_list args)
{
    UBASE_RETURN(_upipe_burst_control(upipe, command, args));
    return upipe_burst_check(upipe);
}

/** @internal @This is the static structure for burst manager. */
static struct upipe_mgr upipe_burst_mgr = {
    .refcount = NULL,
    .signature = UPIPE_BURST_SIGNATURE,
    .upipe_event_str = upipe_burst_event_str,
    .upipe_alloc = upipe_burst_alloc,
    .upipe_control = upipe_burst_control,
    .upipe_input = upipe_burst_input,
};

/** @This returns the burst manager.
 *
 * @return a pointer to the burst manager. 
 */
struct upipe_mgr *upipe_burst_mgr_alloc(void)
{
    return &upipe_burst_mgr;
}
