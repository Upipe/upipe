/*
 * Copyright (C) 2012-2014 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
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
 * @short Upipe OSX_AUDIOQUEUE (OpenGL/X11) sink module
 */

/* FIXME this pipe does NOT work (port me to latest upipe API please) */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/ubuf.h>
#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_block.h>
#include <upipe/upump.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe-osx/upipe_osx_audioqueue_sink.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <AudioToolbox/AudioToolbox.h>

struct upipe_osx_audioqueue_sink {
    /** refcount management structure */
    struct urefcount urefcount;

    /** audioqueue */
    AudioQueueRef queue;
    /** sound volume */
    float volume;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_osx_audioqueue_sink, upipe, UPIPE_OSX_AUDIOQUEUE_SINK_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_osx_audioqueue_sink, urefcount, upipe_osx_audioqueue_sink_free)
UPIPE_HELPER_VOID(upipe_osx_audioqueue_sink)

/** @internal @This is called by AudioQueue after reading a buffer
 * @param _upipe description structure of the pipe (void)
 * @param queue AudioQueue
 * @param qbuf AudioQueue buffer
 */
static void upipe_osx_audioqueue_sink_cb(void *_upipe, AudioQueueRef queue,
                                         struct AudioQueueBuffer *qbuf)
{
/* TODO uncblock ? */
#if 0
    struct upump_mgr *upump_mgr = qbuf->mUserData;
    upump_mgr_sink_unblock(upump_mgr);
    upump_mgr_release(upump_mgr);
#endif
    AudioQueueFreeBuffer(queue, qbuf);
}

/** @internal @This destroys the current audioqueue
 * @param upipe description structure of the pipe
 */
static void upipe_osx_audioqueue_sink_remove(struct upipe *upipe)
{
    struct upipe_osx_audioqueue_sink *osx_audioqueue =
                 upipe_osx_audioqueue_sink_from_upipe(upipe);
    if (unlikely(!osx_audioqueue->queue)) {
        return;
    }

    AudioQueueStop(osx_audioqueue->queue, true);
    AudioQueueDispose(osx_audioqueue->queue, true);
    osx_audioqueue->queue = NULL;
    upipe_notice(upipe, "audioqueue destroyed");
}

/** @internal @This handles audio input.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 */
static void upipe_osx_audioqueue_sink_input_audio(struct upipe *upipe,
                                        struct uref *uref, struct upump **upump_p)
{
    struct upipe_osx_audioqueue_sink *osx_audioqueue =
                 upipe_osx_audioqueue_sink_from_upipe(upipe);
    struct AudioQueueBuffer *qbuf;
    size_t size = 0;

    if (unlikely(!ubase_check(uref_block_size(uref, &size)))) {
        upipe_warn(upipe, "could not get block size");
        uref_free(uref);
        return;
    }

    /* TODO block ? */
    #if 0
    upump_mgr_use(upump->mgr);
    upump_mgr_sink_block(upump->mgr);
    #endif

    /* allocate queue buf, extract block, enqueue
     * Audioqueue has no support for "external" buffers */
    AudioQueueAllocateBuffer(osx_audioqueue->queue, size, &qbuf);
    uref_block_extract(uref, 0, -1, qbuf->mAudioData);
    qbuf->mAudioDataByteSize = size;
    qbuf->mUserData = (*upump_p)->mgr;
    AudioQueueEnqueueBuffer(osx_audioqueue->queue, qbuf, 0, NULL);

    uref_free(uref);
}

/** @internal @This handles input.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 */
static void upipe_osx_audioqueue_sink_input(struct upipe *upipe,
                                        struct uref *uref, struct upump **upump_p)
{
    struct upipe_osx_audioqueue_sink *osx_audioqueue =
                upipe_osx_audioqueue_sink_from_upipe(upipe);

    /* empty uref */
    if (unlikely(!uref->ubuf)) {
        uref_free(uref);
        return;
    }

    /* check queue */
    if (unlikely(!osx_audioqueue->queue)) {
        upipe_warn(upipe, "audioqueue not configured");
        uref_free(uref);
        return;
    }

    upipe_osx_audioqueue_sink_input_audio(upipe, uref, upump_p);
}

/** @internal @This creates a new audioqueue
 * @param upipe description structure of the pipe
 * @param flow description structure of the flow
 * @return an error code
 */
static int upipe_osx_audioqueue_sink_set_flow_def(struct upipe *upipe,
                                                  struct uref *flow)
{
    OSStatus status;
    uint64_t sample_rate = 0; /* hush gcc */
    uint8_t channels = 0;
    uint8_t sample_size = 0;
    struct AudioStreamBasicDescription fmt;
    struct upipe_osx_audioqueue_sink *osx_audioqueue =
                 upipe_osx_audioqueue_sink_from_upipe(upipe);

    if (unlikely(osx_audioqueue->queue)) {
        upipe_osx_audioqueue_sink_remove(upipe);
    }

    /* retrieve flow format information */
    uref_sound_flow_get_rate(flow, &sample_rate);
    uref_sound_flow_get_sample_size(flow, &sample_size);
    uref_sound_flow_get_channels(flow, &channels);

    /* build format description */
    memset(&fmt, 0, sizeof(struct AudioStreamBasicDescription));
    fmt.mSampleRate = sample_rate;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    fmt.mFramesPerPacket = 1;
    fmt.mChannelsPerFrame = channels;
    fmt.mBytesPerPacket = fmt.mBytesPerFrame = sample_size * channels;
    fmt.mBitsPerChannel = sample_size * 8;

    /* create queue */
    status = AudioQueueNewOutput(&fmt, upipe_osx_audioqueue_sink_cb, upipe,
                       NULL, kCFRunLoopCommonModes, 0, &osx_audioqueue->queue);
    if (unlikely(status == kAudioFormatUnsupportedDataFormatError)) {
        upipe_warn(upipe, "unsupported data format");
        return UBASE_ERR_EXTERNAL;
    }

    /* change volume */
    AudioQueueSetParameter(osx_audioqueue->queue, kAudioQueueParam_Volume, 
                           osx_audioqueue->volume);

    /* start queue ! */
    AudioQueueStart(osx_audioqueue->queue, NULL);
    upipe_notice_va(upipe, "audioqueue started (%uHz, %hhuch, %db)",
                        sample_rate, channels, sample_size*8);

    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a file source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_osx_audioqueue_sink_control(struct upipe *upipe,
                                             int command, va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_osx_audioqueue_sink_set_flow_def(upipe, flow_def);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This allocates a osx_audioqueue_sink pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_osx_audioqueue_sink_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_osx_audioqueue_sink_alloc_void(mgr, uprobe,
                                        signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_osx_audioqueue_sink *osx_audioqueue =
        upipe_osx_audioqueue_sink_from_upipe(upipe);
    upipe_osx_audioqueue_sink_init_urefcount(upipe);
    osx_audioqueue->queue = NULL;
    osx_audioqueue->volume = 1.0;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_osx_audioqueue_sink_free(struct upipe *upipe)
{
    upipe_osx_audioqueue_sink_remove(upipe);
    upipe_throw_dead(upipe);
    upipe_osx_audioqueue_sink_clean_urefcount(upipe);
    upipe_osx_audioqueue_sink_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_osx_audioqueue_sink_mgr = {
    .refcount = NULL,
    .signature = UPIPE_OSX_AUDIOQUEUE_SINK_SIGNATURE,

    .upipe_alloc = upipe_osx_audioqueue_sink_alloc,
    .upipe_input = upipe_osx_audioqueue_sink_input,
    .upipe_control = upipe_osx_audioqueue_sink_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for osx_audioqueue_sink pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_osx_audioqueue_sink_mgr_alloc(void)
{
    return &upipe_osx_audioqueue_sink_mgr;
}
