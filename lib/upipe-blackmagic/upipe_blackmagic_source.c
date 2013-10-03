/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation https (the
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
 * @short Upipe source module for BlackMagic Design SDI cards
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_dump.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/upump.h>
#include <upipe/ubuf.h>
#include <upipe/ufifo.h>
#include <upipe/uqueue.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe-blackmagic/upipe_blackmagic_source.h>
#include "blackmagic_wrap.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

/** default size of buffers when unspecified */
#define UBUF_DEFAULT_SIZE       4096
#define MAX_QUEUE_LENGTH 255
#define CHROMA "uyvy422"

/** @internal @This is the private context of a http source pipe. */
struct upipe_bmd_src {
    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;

    /** pipe acting as output */
    struct upipe *output;
    /** flow definition packet */
    struct uref *output_flow;
    /** true if the flow definition has already been sent */
    bool output_flow_sent;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** read watcher */
    struct upump *upump;
    /** init pump */
    struct upump *upump_init;

    /** queue between blackmagic thread and pipe thread */
    struct uqueue uqueue;
    /** queue extra */
    uint8_t *queue_extra;

    /** uclock structure, if not NULL we are in live mode */
    struct uclock *uclock;

    /** blackmagic wrapper */
    struct bmd_wrap *bmd_wrap;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_bmd_src, upipe, UPIPE_BMD_SRC_SIGNATURE)
UPIPE_HELPER_VOID(upipe_bmd_src)
UPIPE_HELPER_UREF_MGR(upipe_bmd_src, uref_mgr)

UPIPE_HELPER_UBUF_MGR(upipe_bmd_src, ubuf_mgr)
UPIPE_HELPER_OUTPUT(upipe_bmd_src, output, output_flow, output_flow_sent)

UPIPE_HELPER_UPUMP_MGR(upipe_bmd_src, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_bmd_src, upump, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_bmd_src, upump_init, upump_mgr)
UPIPE_HELPER_UCLOCK(upipe_bmd_src, uclock)

/** @internal @This is called in blackmagic thread upon receiving video frames.
 * @param _upipe description structure of the pipe
 * @param frame received frame
 */
static void upipe_bmd_src_video_cb(void *_upipe, struct bmd_frame *frame)
{
    struct upipe *upipe = _upipe;
    struct upipe_bmd_src *upipe_bmd_src = upipe_bmd_src_from_upipe(upipe);
    struct uref *uref;
    struct uchain *uchain;
    const uint8_t *buf_in;
    uint8_t *buf_out = NULL;
    size_t stride;
    uint8_t hsub, vsub, macropixel, macropixel_size;
    uint64_t timestamp = UINT64_MAX;
    int i;

    /* get uclock timestamps first */
    if (upipe_bmd_src->uclock) {
        timestamp = uclock_now(upipe_bmd_src->uclock);
    }

    /* check incoming frame */
    if (frame->width == 0 || frame->height == 0 || frame->stride == 0
        || frame->data == NULL) {
        upipe_err_va(upipe, "invalid frame %"PRId64" (%p) (%zux%zu %zu)",
                     frame->timecode, frame->width, frame->height,
                     frame->data, frame->stride);
    }

    /* alloc and map uref */
    uref = uref_pic_alloc(upipe_bmd_src->uref_mgr, upipe_bmd_src->ubuf_mgr,
                          frame->width, frame->height);
    if (unlikely(!uref)) {
        /* TODO queue probe */
        return;
    }
    uref_pic_size(uref, NULL, NULL, &macropixel);
    uref_pic_plane_write(uref, CHROMA, 0, 0, -1, -1, &buf_out);
    uref_pic_plane_size(uref, CHROMA, &stride, &hsub, &vsub, &macropixel_size);

    if (unlikely(!buf_out)) {
        /* TODO queue error log could not map uref */
        uref_free(uref);
        return;
    }

    /* copy */
    buf_in = frame->data;
    for (i=0; i < frame->height/vsub; i++) {
        memcpy(buf_out, buf_in, frame->width*macropixel_size/hsub/macropixel);
        buf_out += stride;
        buf_in += frame->stride;
    }

    uref_pic_plane_unmap(uref, CHROMA, 0, 0, -1, -1);

    /* set uclock timestamps */
    if (upipe_bmd_src->uclock) {
        uref_clock_set_systime(uref, timestamp);
    }

    /* TODO read timecode/duration from SDI */

    /* queue uref */
    uchain = uref_to_uchain(uref);
    if (unlikely(!uqueue_push(&upipe_bmd_src->uqueue, uchain))) {
        uref_free(uref);
    }
}

/** @internal @This outputs urefs.
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump upump structure
 */
void upipe_bmd_src_output_uref(struct upipe *upipe,
                               struct uref *uref, struct upump *upump)
{
    struct upipe_bmd_src *upipe_bmd_src = upipe_bmd_src_from_upipe(upipe);

    /* output flow def */
    if (unlikely(!upipe_bmd_src->output_flow)) {
        struct uref *flow;
        uint8_t macropixel;
        uref_pic_size(uref, NULL, NULL, &macropixel);
        flow = uref_pic_flow_alloc_def(upipe_bmd_src->uref_mgr, macropixel);
        upipe_bmd_src_store_flow_def(upipe, flow);
    }

    upipe_bmd_src_output(upipe, uref, upump);
}

/** @internal @This flushes the internal queue.
 *
 * @param upipe description structure of the pipe
 * @return false in case of error
 */
void upipe_bmd_src_flush(struct upipe *upipe)
{
    struct upipe_bmd_src *upipe_bmd_src = upipe_bmd_src_from_upipe(upipe);
    struct uchain *uchain;
    struct uref *uref;
    
    /* unqueue urefs */
    while ((uchain = uqueue_pop(&upipe_bmd_src->uqueue))) {
        uref = uref_from_uchain(uchain);
        upipe_bmd_src_output_uref(upipe, uref, NULL);
    }
}

/** @internal @This reads data from the source and outputs it.
 * It is called upon receiving new data from the card.
 *
 * @param upump description structure of the read watcher
 */
static void upipe_bmd_src_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_bmd_src *upipe_bmd_src = upipe_bmd_src_from_upipe(upipe);
    struct uref *uref;
    struct uchain *uchain;

    /* unqueue urefs */
    while((uchain = uqueue_pop(&upipe_bmd_src->uqueue))) {
        uref = uref_from_uchain(uchain);
        upipe_bmd_src_output_uref(upipe, uref, upump);
    }
}

/** @internal @This is called (once) to start blackmagic streams.
 *
 * @param upump description structure of the read watcher
 */
static void upipe_bmd_src_init_cb(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_bmd_src *upipe_bmd_src = upipe_bmd_src_from_upipe(upipe);

    /* start blackmagic streams */
    if (unlikely(!bmd_wrap_start(upipe_bmd_src->bmd_wrap))) {
        upipe_err(upipe, "could not start blackmagic streams");
    }

    /* destroy pump */
    upipe_bmd_src_set_upump_init(upipe, NULL);
}

/** @internal @This sets the upump manager and allocates read pump
 *
 * @param upipe description structure of the pipe
 * @param upump_mgr upump manager
 * @return false in case of error
 */
static bool _upipe_bmd_src_set_upump_mgr(struct upipe *upipe,
                                         struct upump_mgr *mgr)
{
    struct upipe_bmd_src *upipe_bmd_src = upipe_bmd_src_from_upipe(upipe);
    struct upump *upump;

    /* cannot send need_ubuf/uref_mgr from blackmagic thread ... */
    if (unlikely(!upipe_bmd_src->uref_mgr)) {
        upipe_throw_need_uref_mgr(upipe);
        if (unlikely(!upipe_bmd_src->uref_mgr)) {
            upipe_warn(upipe, "no uref manager defined");
            return false;
        }
    }
    if (unlikely(!upipe_bmd_src->ubuf_mgr)) {
        upipe_throw_need_ubuf_mgr(upipe, NULL);
        if (unlikely(!upipe_bmd_src->ubuf_mgr)) {
            upipe_warn(upipe, "no ubuf manager defined");
            return false;
        }
    }

    /* set upump manager */
    if (unlikely(!upipe_bmd_src_set_upump_mgr(upipe, mgr))) {
        return false;
    }

    /* allocate new uqueue pump */
    upipe_bmd_src_set_upump(upipe, NULL);
    upump = uqueue_upump_alloc_pop(&upipe_bmd_src->uqueue, mgr,
                                   upipe_bmd_src_worker, upipe);
    if (unlikely(!upump)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_UPUMP);
        return false;
    }

    upipe_bmd_src_set_upump(upipe, upump);
    upump_start(upump);

    /* allocate new init pump */
    upipe_bmd_src_set_upump_init(upipe, NULL);
    upump = upump_alloc_idler(mgr, upipe_bmd_src_init_cb, upipe);

    if (unlikely(!upump)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_UPUMP);
        return false;
    }

    upipe_bmd_src_set_upump_init(upipe, upump);
    upump_start(upump);

    return true;
}

/** @internal @This processes control commands on a blackmagic source pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_bmd_src_control(struct upipe *upipe, enum upipe_command command,
                                va_list args)
{
    switch (command) {
        case UPIPE_GET_UREF_MGR: {
            struct uref_mgr **p = va_arg(args, struct uref_mgr **);
            return upipe_bmd_src_get_uref_mgr(upipe, p);
        }
        case UPIPE_SET_UREF_MGR: {
            struct uref_mgr *uref_mgr = va_arg(args, struct uref_mgr *);
            return upipe_bmd_src_set_uref_mgr(upipe, uref_mgr);
        }

        case UPIPE_GET_UBUF_MGR: {
            struct ubuf_mgr **p = va_arg(args, struct ubuf_mgr **);
            return upipe_bmd_src_get_ubuf_mgr(upipe, p);
        }
        case UPIPE_SET_UBUF_MGR: {
            struct ubuf_mgr *ubuf_mgr = va_arg(args, struct ubuf_mgr *);
            return upipe_bmd_src_set_ubuf_mgr(upipe, ubuf_mgr);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_bmd_src_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_bmd_src_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_bmd_src_set_output(upipe, output);
        }

        case UPIPE_GET_UPUMP_MGR: {
            struct upump_mgr **p = va_arg(args, struct upump_mgr **);
            return upipe_bmd_src_get_upump_mgr(upipe, p);
        }
        case UPIPE_SET_UPUMP_MGR: {
            struct upump_mgr *upump_mgr = va_arg(args, struct upump_mgr *);
            return _upipe_bmd_src_set_upump_mgr(upipe, upump_mgr);
        }
        case UPIPE_GET_UCLOCK: {
            struct uclock **p = va_arg(args, struct uclock **);
            return upipe_bmd_src_get_uclock(upipe, p);
        }
        case UPIPE_SET_UCLOCK: {
            struct uclock *uclock = va_arg(args, struct uclock *);
            return upipe_bmd_src_set_uclock(upipe, uclock);
        }
        default:
            return false;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_bmd_src_free(struct upipe *upipe)
{
    struct upipe_bmd_src *upipe_bmd_src = upipe_bmd_src_from_upipe(upipe);

    bmd_wrap_free(upipe_bmd_src->bmd_wrap);

    upipe_bmd_src_flush(upipe);
    uqueue_clean(&upipe_bmd_src->uqueue);
    free(upipe_bmd_src->queue_extra);

    upipe_throw_dead(upipe);

    upipe_bmd_src_clean_uclock(upipe);
    upipe_bmd_src_clean_upump_init(upipe);
    upipe_bmd_src_clean_upump(upipe);
    upipe_bmd_src_clean_upump_mgr(upipe);
    upipe_bmd_src_clean_output(upipe);
    upipe_bmd_src_clean_ubuf_mgr(upipe);
    upipe_bmd_src_clean_uref_mgr(upipe);

    upipe_bmd_src_free_void(upipe);
}

/** @internal @This allocates a http source pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_bmd_src_alloc(struct upipe_mgr *mgr,
                                          struct uprobe *uprobe,
                                          uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_bmd_src_alloc_void(mgr, uprobe, signature,
                                                    args);
    if (unlikely(upipe == NULL)) return NULL;
    struct upipe_bmd_src *upipe_bmd_src = upipe_bmd_src_from_upipe(upipe);

    /* allocate blackmagic context */
    upipe_bmd_src->bmd_wrap = bmd_wrap_alloc(upipe);
    if (unlikely(!upipe_bmd_src->bmd_wrap)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_EXTERNAL);
        upipe_bmd_src_free_void(upipe);
        return NULL;
    }
    bmd_wrap_set_video_cb(upipe_bmd_src->bmd_wrap, upipe_bmd_src_video_cb);

    /* init queue */
    upipe_bmd_src->queue_extra = malloc(ufifo_sizeof(MAX_QUEUE_LENGTH));
    if (unlikely(!upipe_bmd_src->queue_extra)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        bmd_wrap_free(upipe_bmd_src->bmd_wrap);
        upipe_bmd_src_free_void(upipe);
        return NULL;
    }
    uqueue_init(&upipe_bmd_src->uqueue, MAX_QUEUE_LENGTH,
                upipe_bmd_src->queue_extra);

    upipe_bmd_src_init_uref_mgr(upipe);
    upipe_bmd_src_init_ubuf_mgr(upipe);
    upipe_bmd_src_init_output(upipe);
    upipe_bmd_src_init_upump_mgr(upipe);
    upipe_bmd_src_init_upump(upipe);
    upipe_bmd_src_init_upump_init(upipe);
    upipe_bmd_src_init_uclock(upipe);

    upipe_throw_ready(upipe);
    return upipe;
}

/** module manager static descriptor */
static struct upipe_mgr upipe_bmd_src_mgr = {
    .signature = UPIPE_BMD_SRC_SIGNATURE,

    .upipe_alloc = upipe_bmd_src_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_bmd_src_control,
    .upipe_free = upipe_bmd_src_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for all http source pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_bmd_src_mgr_alloc(void)
{
    return &upipe_bmd_src_mgr;
}
