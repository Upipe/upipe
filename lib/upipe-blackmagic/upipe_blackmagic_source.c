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

/* TODO dynamic manager with input card selection */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/upump.h>
#include <upipe/ubuf.h>
#include <upipe/ufifo.h>
#include <upipe/uqueue.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_subpipe.h>
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
#define MAX_QUEUE_LENGTH 255
#define CHROMA "u8y8v8y8"

/** @internal pipe type */
enum output_type {
    OUTPUT_TYPE_AUDIO,
    OUTPUT_TYPE_VIDEO,
};

/** @internal @This is the private context of a http source pipe. */
struct upipe_bmd_src {
    /** real refcount management structure */
    struct urefcount urefcount_real;
    /** refcount management structure exported to the public structure */
    struct urefcount urefcount;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** init pump */
    struct upump *upump_init;

    /** list of output subpipes */
    struct uchain outputs;
    /** manager to create output subpipes */
    struct upipe_mgr sub_mgr;
    /** video subpipe */
    struct upipe *video_subpipe;
    /** audio subpipe */
    struct upipe *audio_subpipe;

    /** blackmagic wrapper */
    struct bmd_wrap *bmd_wrap;

    /** public upipe structure */
    struct upipe upipe;
};

/** @internal @This is the private context of an output of a bmdsrc pipe */
struct upipe_bmd_src_output {
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked lists */
    struct uchain uchain;

    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** uclock structure */
    struct uclock *uclock;

    /** pipe acting as output */
    struct upipe *output;
    /** flow definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /** incoming frames watcher */
    struct upump *upump;
    /** upump manager */
    struct upump_mgr *upump_mgr;

    /** queue between blackmagic thread and pipe thread */
    struct uqueue uqueue;
    /** queue extra */
    uint8_t *queue_extra;

    /** output pipe type */
    enum output_type type;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_bmd_src, upipe, UPIPE_BMD_SRC_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_bmd_src, urefcount, upipe_bmd_src_no_input)
UPIPE_HELPER_VOID(upipe_bmd_src)
UPIPE_HELPER_UPUMP_MGR(upipe_bmd_src, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_bmd_src, upump_init, upump_mgr)
UBASE_FROM_TO(upipe_bmd_src, urefcount, urefcount_real, urefcount_real)

UPIPE_HELPER_UPIPE(upipe_bmd_src_output, upipe, UPIPE_BMD_SRC_OUTPUT_SIGNATURE)
UPIPE_HELPER_FLOW(upipe_bmd_src_output, "")
UPIPE_HELPER_UREFCOUNT(upipe_bmd_src_output, urefcount, upipe_bmd_src_output_free)
UPIPE_HELPER_UPUMP_MGR(upipe_bmd_src_output, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_bmd_src_output, upump, upump_mgr)
UPIPE_HELPER_UREF_MGR(upipe_bmd_src_output, uref_mgr)
UPIPE_HELPER_UBUF_MGR(upipe_bmd_src_output, ubuf_mgr, flow_def)
UPIPE_HELPER_UCLOCK(upipe_bmd_src_output, uclock)
UPIPE_HELPER_OUTPUT(upipe_bmd_src_output, output, flow_def, flow_def_sent)

UPIPE_HELPER_SUBPIPE(upipe_bmd_src, upipe_bmd_src_output, output, sub_mgr, outputs,
                     uchain)

/** @internal @This allocates an output subpipe of a dup pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_bmd_src_output_alloc(struct upipe_mgr *mgr,
               struct uprobe *uprobe, uint32_t signature, va_list args)
{
    struct upipe_bmd_src *upipe_bmd_src = upipe_bmd_src_from_sub_mgr(mgr);

    struct uref *flow_def;
    struct upipe *upipe = upipe_bmd_src_output_alloc_flow(mgr,
                           uprobe, signature, args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_bmd_src_output *upipe_bmd_src_output =
           upipe_bmd_src_output_from_upipe(upipe);

    const char *def = "(invalid)";
    uref_flow_get_def(flow_def, &def);
    if (!ubase_ncmp(def, "pic.")) {
        upipe_bmd_src_output->type = OUTPUT_TYPE_VIDEO;
    } else if (!ubase_ncmp(def, "sound.")) {
        upipe_bmd_src_output->type = OUTPUT_TYPE_AUDIO;
    } else {
        upipe_bmd_src_output_free_flow(upipe);
        return NULL;
    }

    /* init queue */
    upipe_bmd_src_output->queue_extra = malloc(ufifo_sizeof(MAX_QUEUE_LENGTH));
    if (unlikely(!upipe_bmd_src_output->queue_extra)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        upipe_bmd_src_output_free_flow(upipe);
        return NULL;
    }
    uqueue_init(&upipe_bmd_src_output->uqueue, MAX_QUEUE_LENGTH,
                upipe_bmd_src_output->queue_extra);

    upipe_bmd_src_output_init_urefcount(upipe);
    upipe_bmd_src_output_init_uref_mgr(upipe);
    upipe_bmd_src_output_init_ubuf_mgr(upipe);
    upipe_bmd_src_output_init_upump_mgr(upipe);
    upipe_bmd_src_output_init_upump(upipe);
    upipe_bmd_src_output_init_uclock(upipe);
    upipe_bmd_src_output_init_output(upipe);
    upipe_bmd_src_output_init_sub(upipe);

    /* cannot send probes from blackmagic thread ... */
    if (unlikely(!ubase_check(upipe_bmd_src_output_check_upump_mgr(upipe)))) {
        return NULL; /* FIXME */
    }
    if (unlikely(!ubase_check(upipe_bmd_src_output_check_uref_mgr(upipe)))) {
        return NULL; /* FIXME */
    }

    /* flow format and ubuf manager */
    switch (upipe_bmd_src_output->type) {
        case OUTPUT_TYPE_VIDEO: {
            uref_pic_flow_clear_format(flow_def);
            uref_pic_flow_set_planes(flow_def, 0);
            uref_pic_flow_set_macropixel(flow_def, 2);
            uref_pic_flow_add_plane(flow_def, 1, 1, 4, CHROMA);
            enum ubase_err ret = upipe_throw_new_flow_format(upipe, 
                    flow_def, &upipe_bmd_src_output->ubuf_mgr);
            if (unlikely(!ubase_check(ret))) {
                return NULL; /* FIXME */
            }
            upipe_bmd_src->video_subpipe = upipe;
        }
        case OUTPUT_TYPE_AUDIO: {
            /* TODO */
            enum ubase_err ret = upipe_throw_new_flow_format(upipe, 
                    flow_def, &upipe_bmd_src_output->ubuf_mgr);
            if (unlikely(!ubase_check(ret))) {
                return NULL; /* FIXME */
            }
            upipe_bmd_src->audio_subpipe = upipe;
        }

        default: /* should never be here */
            break;
    }

    uref_free(flow_def);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This outputs urefs.
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump upump structure
 */
void upipe_bmd_src_output_uref(struct upipe *upipe,
                               struct uref *uref, struct upump *upump)
{
    struct upipe_bmd_src_output *upipe_bmd_src_output =
           upipe_bmd_src_output_from_upipe(upipe);
    size_t hsize, vsize;

    /* output flow def */
    if (unlikely(!upipe_bmd_src_output->flow_def)) {
        struct uref *flow;
        uint8_t macropixel;
        struct urational fps = {25, 1}; /* FIXME */
        uref_pic_size(uref, &hsize, &vsize, &macropixel);
        flow = uref_pic_flow_alloc_def(upipe_bmd_src_output->uref_mgr,
                                       macropixel);
        uref_pic_flow_add_plane(flow, 1, 1, 4, CHROMA);
        uref_pic_flow_set_hsize(flow, hsize);
        uref_pic_flow_set_vsize(flow, vsize);
        uref_pic_flow_set_fps(flow, fps);
        upipe_bmd_src_output_store_flow_def(upipe, flow);
    }

    if (ubase_check(uref_pic_size(uref, &hsize, &vsize, NULL))) {
        upipe_verbose_va(upipe, "sending picture %zux%zu %p",
                         hsize, vsize, uref);
    } else {
        upipe_verbose_va(upipe, "sending uref %p", uref);
    }
    upipe_bmd_src_output_output(upipe, uref, &upump);
}

/** @internal @This flushes the internal queue.
 *
 * @param upipe description structure of the pipe
 * @param upump description structure of the pump
 */
void upipe_bmd_src_output_flush(struct upipe *upipe, struct upump *upump)
{
    struct upipe_bmd_src_output *upipe_bmd_src_output =
           upipe_bmd_src_output_from_upipe(upipe);
    struct uchain *uchain;
    struct uref *uref;
    
    /* unqueue urefs */
    while ((uchain = uqueue_pop(&upipe_bmd_src_output->uqueue))) {
        uref = uref_from_uchain(uchain);
        upipe_bmd_src_output_uref(upipe, uref, upump);
    }
}

/** @internal @This reads data from the source and outputs it.
 * It is called upon receiving new data from the card.
 *
 * @param upump description structure of the read watcher
 */
static void upipe_bmd_src_output_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    upipe_bmd_src_output_flush(upipe, upump);
}

/** @internal @This sets the upump manager and allocates read pump
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static enum ubase_err _upipe_bmd_src_output_attach_upump_mgr(
                                          struct upipe *upipe)
{
    struct upipe_bmd_src_output *upipe_bmd_src_output =
           upipe_bmd_src_output_from_upipe(upipe);
    struct upump *upump;

    upipe_bmd_src_output_set_upump(upipe, NULL);
    UBASE_RETURN(upipe_bmd_src_output_attach_upump_mgr(upipe))

    /* allocate new uqueue pump */
    upipe_bmd_src_output_set_upump(upipe, NULL);
    upump = uqueue_upump_alloc_pop(&upipe_bmd_src_output->uqueue,
        upipe_bmd_src_output->upump_mgr, upipe_bmd_src_output_worker, upipe);
    if (unlikely(!upump)) {
        upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
        return UBASE_ERR_UPUMP;
    }

    upipe_bmd_src_output_set_upump(upipe, upump);
    upump_start(upump);

    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a blackmagic output pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static enum ubase_err upipe_bmd_src_output_control(struct upipe *upipe,
                                                   enum upipe_command command,
                                                   va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UREF_MGR: {
            return upipe_bmd_src_output_attach_uref_mgr(upipe);
        }
        case UPIPE_ATTACH_UBUF_MGR: {
            return upipe_bmd_src_output_attach_ubuf_mgr(upipe);
        }
        case UPIPE_ATTACH_UCLOCK: {
            return upipe_bmd_src_output_attach_uclock(upipe);
        }
        case UPIPE_ATTACH_UPUMP_MGR: {
            return _upipe_bmd_src_output_attach_upump_mgr(upipe);
        }

        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_bmd_src_output_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_bmd_src_output_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_bmd_src_output_set_output(upipe, output);
        }
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_bmd_src_output_get_super(upipe, p);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}


/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_bmd_src_output_free(struct upipe *upipe)
{
    struct upipe_bmd_src *upipe_bmd_src =
           upipe_bmd_src_from_sub_mgr(upipe->mgr);
    struct upipe_bmd_src_output *upipe_bmd_src_output =
           upipe_bmd_src_output_from_upipe(upipe);

    upipe_bmd_src_output_flush(upipe, NULL);
    uqueue_clean(&upipe_bmd_src_output->uqueue);
    free(upipe_bmd_src_output->queue_extra);

    upipe_throw_dead(upipe);

    upipe_bmd_src_output_clean_output(upipe);
    upipe_bmd_src_output_clean_ubuf_mgr(upipe);
    upipe_bmd_src_output_clean_uref_mgr(upipe);
    upipe_bmd_src_output_clean_uclock(upipe);
    upipe_bmd_src_output_clean_upump(upipe);
    upipe_bmd_src_output_clean_upump_mgr(upipe);
    upipe_bmd_src_output_clean_sub(upipe);
    upipe_bmd_src_output_clean_urefcount(upipe);

    /* FIXME switch audio/video */
    switch (upipe_bmd_src_output->type) {
        case OUTPUT_TYPE_VIDEO:
            upipe_bmd_src->video_subpipe = NULL;
            break;
        case OUTPUT_TYPE_AUDIO:
            upipe_bmd_src->audio_subpipe = NULL;
            break;
    }

    upipe_clean(upipe);
    upipe_bmd_src_output_free_flow(upipe);
}

/** @internal @This is called in blackmagic thread when receiving video frames.
 * @param _upipe description structure of the pipe
 * @param frame received frame
 */
static void upipe_bmd_src_video_cb(void *_upipe, struct bmd_frame *frame)
{
    struct upipe *grandpipe = _upipe;
    struct upipe_bmd_src *upipe_bmd_src = upipe_bmd_src_from_upipe(grandpipe);
    if (unlikely(!upipe_bmd_src->video_subpipe)) {
        return;
    }
    struct upipe *upipe = upipe_bmd_src->video_subpipe;
    struct upipe_bmd_src_output *upipe_bmd_src_output =
           upipe_bmd_src_output_from_upipe(upipe);
    
    struct uref *uref;
    struct uchain *uchain;
    const uint8_t *buf_in;
    uint8_t *buf_out = NULL;
    size_t stride;
    uint8_t hsub, vsub, macropixel, macropixel_size;
    uint64_t timestamp = UINT64_MAX;
    int i;

    /* get uclock timestamps first */
    if (upipe_bmd_src_output->uclock) {
        timestamp = uclock_now(upipe_bmd_src_output->uclock);
    }

    /* check incoming frame */
    if (frame->width == 0 || frame->height == 0 || frame->stride == 0
        || frame->data == NULL) {
        upipe_err_va(upipe, "invalid frame %"PRId64" (%p) (%zux%zu %zu)",
                     frame->timecode, frame->width, frame->height,
                     frame->data, frame->stride);
    }

    /* alloc and map uref */
    uref = uref_pic_alloc(upipe_bmd_src_output->uref_mgr, upipe_bmd_src_output->ubuf_mgr,
                          frame->width, frame->height);
    if (unlikely(!uref)) {
        return;
    }
    uref_pic_size(uref, NULL, NULL, &macropixel);
    uref_pic_plane_write(uref, CHROMA, 0, 0, -1, -1, &buf_out);
    uref_pic_plane_size(uref, CHROMA, &stride, &hsub, &vsub, &macropixel_size);

    if (unlikely(!buf_out)) {
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
    if (upipe_bmd_src_output->uclock) {
        uref_clock_set_pts_sys(uref, timestamp);
    }

    /* TODO read timecode/duration from SDI */

    /* queue uref */
    uchain = uref_to_uchain(uref);
    if (unlikely(!uqueue_push(&upipe_bmd_src_output->uqueue, uchain))) {
        uref_free(uref);
    }
}

/** @internal @This is called in blackmagic thread when receiving audio frames.
 * @param _upipe description structure of the pipe
 * @param frame received frame
 */
static void upipe_bmd_src_audio_cb(void *_upipe, struct bmd_frame *frame)
{
    struct upipe *grandpipe = _upipe;
    struct upipe_bmd_src *upipe_bmd_src = upipe_bmd_src_from_upipe(grandpipe);
    if (unlikely(!upipe_bmd_src->audio_subpipe)) {
        return;
    }
#if 0
    struct upipe *upipe = upipe_bmd_src->audio_subpipe;
    struct upipe_bmd_src_output *upipe_bmd_src_output =
           upipe_bmd_src_output_from_upipe(upipe);
    
    struct uref *uref;
    struct uchain *uchain;
    uint64_t timestamp = UINT64_MAX;

    /* get uclock timestamps first */
    if (upipe_bmd_src_output->uclock) {
        timestamp = uclock_now(upipe_bmd_src_output->uclock);
    }

    /* TODO copy samples */

    /* set uclock timestamps */
    if (upipe_bmd_src_output->uclock) {
        uref_clock_set_pts_sys(uref, timestamp);
    }

    /* TODO read timecode/duration from SDI */

    /* queue uref */
    uchain = uref_to_uchain(uref);
    if (unlikely(!uqueue_push(&upipe_bmd_src_output->uqueue, uchain))) {
        uref_free(uref);
    }
#endif
}

/** @internal @This is called (once) to start blackmagic streams.
 *
 * @param upump description structure of the read watcher
 */
static void upipe_bmd_src_init_cb(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_bmd_src *upipe_bmd_src = upipe_bmd_src_from_upipe(upipe);

    /* destroy pump */
    upipe_bmd_src_set_upump_init(upipe, NULL);

    upipe_dbg(upipe, "starting streams");
    /* start blackmagic streams */
    if (unlikely(!bmd_wrap_start(upipe_bmd_src->bmd_wrap))) {
        upipe_err(upipe, "could not start blackmagic streams");
    }
}

/** @internal @This sets the upump manager and allocates read pump
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static enum ubase_err _upipe_bmd_src_attach_upump_mgr(struct upipe *upipe)
{
    struct upipe_bmd_src *upipe_bmd_src = upipe_bmd_src_from_upipe(upipe);
    struct upump *upump;

    upipe_bmd_src_set_upump_init(upipe, NULL);
    UBASE_RETURN(upipe_bmd_src_attach_upump_mgr(upipe))

    /* allocate new init pump */
    upipe_bmd_src_set_upump_init(upipe, NULL);
    upump = upump_alloc_idler(upipe_bmd_src->upump_mgr,
                              upipe_bmd_src_init_cb, upipe);

    if (unlikely(!upump)) {
        upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
        return UBASE_ERR_UPUMP;
    }

    upipe_bmd_src_set_upump_init(upipe, upump);
    upump_start(upump);

    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a blackmagic source pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static enum ubase_err upipe_bmd_src_control(struct upipe *upipe,
                                            enum upipe_command command,
                                            va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR: {
            return _upipe_bmd_src_attach_upump_mgr(upipe);
        }
        case UPIPE_GET_SUB_MGR: {
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);
            return upipe_bmd_src_get_sub_mgr(upipe, p);
        }
        case UPIPE_ITERATE_SUB: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_bmd_src_iterate_sub(upipe, p);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_bmd_src_free(struct urefcount *urefcount_real)
{
    struct upipe_bmd_src *upipe_bmd_src =
           upipe_bmd_src_from_urefcount_real(urefcount_real);
    struct upipe *upipe = upipe_bmd_src_to_upipe(upipe_bmd_src);

    bmd_wrap_free(upipe_bmd_src->bmd_wrap);

    upipe_throw_dead(upipe);

    upipe_bmd_src_clean_upump_init(upipe);
    upipe_bmd_src_clean_upump_mgr(upipe);

    upipe_bmd_src_clean_sub_outputs(upipe);
    urefcount_clean(urefcount_real);
    upipe_bmd_src_clean_urefcount(upipe);

    upipe_bmd_src_free_void(upipe);
}

/** @internal @This initializes the output manager for a blackmagic pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_bmd_src_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_bmd_src *upipe_bmd_src = upipe_bmd_src_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_bmd_src->sub_mgr;
    sub_mgr->refcount = upipe_bmd_src_to_urefcount_real(upipe_bmd_src);
    sub_mgr->signature = UPIPE_BMD_SRC_OUTPUT_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_bmd_src_output_alloc;
    sub_mgr->upipe_input = NULL;
    sub_mgr->upipe_control = upipe_bmd_src_output_control;
    sub_mgr->upipe_mgr_control = NULL;
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
        upipe_throw_fatal(upipe, UBASE_ERR_EXTERNAL);
        upipe_bmd_src_free_void(upipe);
        return NULL;
    }
    bmd_wrap_set_video_cb(upipe_bmd_src->bmd_wrap, upipe_bmd_src_video_cb);
    bmd_wrap_set_audio_cb(upipe_bmd_src->bmd_wrap, upipe_bmd_src_audio_cb);

    upipe_bmd_src_init_urefcount(upipe);
    urefcount_init(upipe_bmd_src_to_urefcount_real(upipe_bmd_src),
                   upipe_bmd_src_free);
    upipe_bmd_src_init_sub_mgr(upipe);
    upipe_bmd_src_init_sub_outputs(upipe);
    upipe_bmd_src_init_upump_mgr(upipe);
    upipe_bmd_src_init_upump_init(upipe);
    upipe_bmd_src->video_subpipe = NULL;
    upipe_bmd_src->audio_subpipe = NULL;

    upipe_throw_ready(upipe);

    return upipe;
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_bmd_src_no_input(struct upipe *upipe)
{
    struct upipe_bmd_src *upipe_bmd_src = upipe_bmd_src_from_upipe(upipe);
    upipe_bmd_src_throw_sub_outputs(upipe, UPROBE_SOURCE_END);
    urefcount_release(upipe_bmd_src_to_urefcount_real(upipe_bmd_src));
}

/** module manager static descriptor */
static struct upipe_mgr upipe_bmd_src_mgr = {
    .refcount = NULL,
    .signature = UPIPE_BMD_SRC_SIGNATURE,

    .upipe_alloc = upipe_bmd_src_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_bmd_src_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all http source pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_bmd_src_mgr_alloc(void)
{
    return &upipe_bmd_src_mgr;
}
