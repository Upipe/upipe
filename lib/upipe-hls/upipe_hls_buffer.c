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

#include <upipe-hls/upipe_hls_buffer.h>

#include <upipe-modules/upipe_buffer.h>
#include <upipe-modules/upipe_probe_uref.h>

#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_upipe.h>

#include <upipe/uprobe_prefix.h>

#include <upipe/uref_clock.h>
#include <upipe/uref_block.h>

#include <upipe/ueventfd.h>

#include <upipe/uclock.h>

#define DEFAULT_MAX_DURATION        (10 * UCLOCK_FREQ)

struct upipe_hls_buffer {
    struct upipe upipe;
    struct urefcount urefcount;
    struct upump_mgr *upump_mgr;
    struct upump *upump;
    enum upipe_helper_output_state output_state;
    struct uchain requests;
    struct upipe *output;
    struct uref *flow_def;
    struct uchain urefs;
    unsigned nb_urefs;
    unsigned max_urefs;
    struct uchain blockers;
    struct uchain buffer;
    uint64_t max_duration;
    struct ueventfd ueventfd;
};

/** @hidden */
static bool upipe_hls_buffer_process(struct upipe *upipe, struct uref *uref,
                                     struct upump **upump_p);

UPIPE_HELPER_UPIPE(upipe_hls_buffer, upipe, UPIPE_HLS_BUFFER_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_hls_buffer, urefcount, upipe_hls_buffer_no_ref);
UPIPE_HELPER_VOID(upipe_hls_buffer);
UPIPE_HELPER_OUTPUT(upipe_hls_buffer, output, flow_def, output_state, requests);
UPIPE_HELPER_INPUT(upipe_hls_buffer, urefs, nb_urefs, max_urefs, blockers,
                   upipe_hls_buffer_process);
UPIPE_HELPER_UPUMP_MGR(upipe_hls_buffer, upump_mgr);
UPIPE_HELPER_UPUMP(upipe_hls_buffer, upump, upump_mgr);

static struct upipe *upipe_hls_buffer_alloc(struct upipe_mgr *mgr,
                                            struct uprobe *uprobe,
                                            uint32_t signature,
                                            va_list args)
{
    struct upipe *upipe =
        upipe_hls_buffer_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_hls_buffer_init_urefcount(upipe);
    upipe_hls_buffer_init_output(upipe);
    upipe_hls_buffer_init_input(upipe);
    upipe_hls_buffer_init_upump_mgr(upipe);
    upipe_hls_buffer_init_upump(upipe);

    struct upipe_hls_buffer *upipe_hls_buffer =
        upipe_hls_buffer_from_upipe(upipe);

    ulist_init(&upipe_hls_buffer->buffer);
    ueventfd_init(&upipe_hls_buffer->ueventfd, false);
    upipe_hls_buffer->max_duration = DEFAULT_MAX_DURATION;

    upipe_throw_ready(upipe);

    return upipe;
}

static void upipe_hls_buffer_no_ref(struct upipe *upipe)
{
    struct upipe_hls_buffer *upipe_hls_buffer =
        upipe_hls_buffer_from_upipe(upipe);

    upipe_throw_dead(upipe);

    struct uchain *uchain;
    while ((uchain = ulist_pop(&upipe_hls_buffer->buffer)))
        uref_free(uref_from_uchain(uchain));

    upipe_hls_buffer_clean_upump(upipe);
    upipe_hls_buffer_clean_upump_mgr(upipe);
    ueventfd_clean(&upipe_hls_buffer->ueventfd);
    upipe_hls_buffer_clean_input(upipe);
    upipe_hls_buffer_clean_output(upipe);
    upipe_hls_buffer_clean_urefcount(upipe);
    upipe_hls_buffer_free_void(upipe);
}

static int upipe_hls_buffer_set_flow_def(struct upipe *upipe,
                                         struct uref *flow_def)
{
    struct uref *flow_def_dup = uref_dup(flow_def);
    UBASE_ALLOC_RETURN(flow_def_dup);
    upipe_input(upipe, flow_def_dup, NULL);
    return UBASE_ERR_NONE;
}

static void upipe_hls_buffer_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_hls_buffer *upipe_hls_buffer =
        upipe_hls_buffer_from_upipe(upipe);
    struct uchain *uchain = ulist_pop(&upipe_hls_buffer->buffer);

    ueventfd_read(&upipe_hls_buffer->ueventfd);
    if (likely(upipe_hls_buffer_output_input(upipe)))
        upipe_hls_buffer_unblock_input(upipe);

    if (unlikely(uchain == NULL))
        return;
    struct uref *uref = uref_from_uchain(uchain);
    ueventfd_write(&upipe_hls_buffer->ueventfd);

    const char *flow_def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &flow_def))))
        upipe_hls_buffer_store_flow_def(upipe, uref);
    else
        upipe_hls_buffer_output(upipe, uref_from_uchain(uchain), &upump);
}

static bool upipe_hls_buffer_process(struct upipe *upipe,
                                     struct uref *uref,
                                     struct upump **upump_p)
{
    struct upipe_hls_buffer *upipe_hls_buffer =
        upipe_hls_buffer_from_upipe(upipe);

    uint64_t pts;
    if (unlikely(!ubase_check(uref_clock_get_pts_prog(uref, &pts)))) {
        upipe_warn(upipe, "non dated uref");
        ulist_add(&upipe_hls_buffer->buffer, uref_to_uchain(uref));
        return true;
    }

    struct uchain *uchain;
    ulist_foreach(&upipe_hls_buffer->buffer, uchain) {
        struct uref *uref_tmp = uref_from_uchain(uchain);
        uint64_t pts_tmp;
        if (unlikely(!ubase_check(uref_clock_get_pts_prog(uref_tmp,
                                                          &pts_tmp))))
            continue;

        if (pts_tmp > pts) {
            upipe_warn(upipe, "PTS in the past");
            continue;
        }

        if (pts - pts_tmp > upipe_hls_buffer->max_duration)
            return false;

        break;
    }

    ulist_add(&upipe_hls_buffer->buffer, uref_to_uchain(uref));
    return true;
}

static void upipe_hls_buffer_input(struct upipe *upipe,
                                   struct uref *uref,
                                   struct upump **upump_p)
{
    struct upipe_hls_buffer *upipe_hls_buffer =
        upipe_hls_buffer_from_upipe(upipe);

    if (unlikely(!upipe_hls_buffer_check_input(upipe)) ||
        unlikely(!upipe_hls_buffer_process(upipe, uref, upump_p))) {
        upipe_hls_buffer_hold_input(upipe, uref);
        upipe_hls_buffer_block_input(upipe, upump_p);
    }
    ueventfd_write(&upipe_hls_buffer->ueventfd);
}

static int _upipe_hls_buffer_control(struct upipe *upipe,
                                     int command,
                                     va_list args)
{
    switch (command) {
    case UPIPE_ATTACH_UPUMP_MGR:
        upipe_hls_buffer_set_upump(upipe, NULL);
        return upipe_hls_buffer_attach_upump_mgr(upipe);

    case UPIPE_REGISTER_REQUEST: {
        struct urequest *urequest = va_arg(args, struct urequest *);
        return upipe_hls_buffer_alloc_output_proxy(upipe, urequest);
    }
    case UPIPE_UNREGISTER_REQUEST:{
        struct urequest *urequest = va_arg(args, struct urequest *);
        return upipe_hls_buffer_free_output_proxy(upipe, urequest);
    }
    case UPIPE_GET_FLOW_DEF: {
        struct uref **flow_def_p = va_arg(args, struct uref **);
        return upipe_hls_buffer_get_flow_def(upipe, flow_def_p);
    }
    case UPIPE_SET_FLOW_DEF: {
        struct uref *flow_def = va_arg(args, struct uref *);
        return upipe_hls_buffer_set_flow_def(upipe, flow_def);
    }
    case UPIPE_GET_OUTPUT: {
        struct upipe **output_p = va_arg(args, struct upipe **);
        return upipe_hls_buffer_get_output(upipe, output_p);
    }
    case UPIPE_SET_OUTPUT: {
        struct upipe *output = va_arg(args, struct upipe *);
        return upipe_hls_buffer_set_output(upipe, output);
    }
    }
    return UBASE_ERR_UNHANDLED;
}

static int upipe_hls_buffer_check(struct upipe *upipe)
{
    struct upipe_hls_buffer *upipe_hls_buffer =
        upipe_hls_buffer_from_upipe(upipe);

    UBASE_RETURN(upipe_hls_buffer_check_upump_mgr(upipe));
    if (unlikely(upipe_hls_buffer->upump == NULL)) {
        struct upump *upump =
            ueventfd_upump_alloc(&upipe_hls_buffer->ueventfd,
                                 upipe_hls_buffer->upump_mgr,
                                 upipe_hls_buffer_worker, upipe,
                                 upipe->refcount);
        if (unlikely(upump == NULL))
            return UBASE_ERR_UPUMP;

        upipe_hls_buffer_set_upump(upipe, upump);
        upump_start(upump);
        if (unlikely(!upipe_hls_buffer_check_input(upipe)) ||
            likely(!ulist_empty(&upipe_hls_buffer->buffer)))
            ueventfd_write(&upipe_hls_buffer->ueventfd);
    }
    return UBASE_ERR_NONE;
}

static int upipe_hls_buffer_control(struct upipe *upipe,
                                    int command,
                                    va_list args)
{
    UBASE_RETURN(_upipe_hls_buffer_control(upipe, command, args));
    return upipe_hls_buffer_check(upipe);
}

static struct upipe_mgr upipe_hls_buffer_mgr = {
    .refcount = NULL,
    .signature = UPIPE_HLS_BUFFER_SIGNATURE,
    .upipe_command_str = upipe_hls_buffer_command_str,
    .upipe_event_str = upipe_hls_buffer_event_str,
    .upipe_alloc = upipe_hls_buffer_alloc,
    .upipe_control = upipe_hls_buffer_control,
    .upipe_input = upipe_hls_buffer_input,
};

struct upipe_mgr *upipe_hls_buffer_mgr_alloc(void)
{
    return &upipe_hls_buffer_mgr;
}
