/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
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
    /** audioqueue */
    AudioQueueRef queue;
    /** sound volume */
    float volume;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_osx_audioqueue_sink, upipe);

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

/** @internal @This creates a new audioqueue
 * @param upipe description structure of the pipe
 * @param flow description structure of the flow
 * @return false in case of error
 */
static bool upipe_osx_audioqueue_sink_create(struct upipe *upipe,
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
        return false;
    }

    /* change volume */
    AudioQueueSetParameter(osx_audioqueue->queue, kAudioQueueParam_Volume, 
                           osx_audioqueue->volume);

    /* start queue ! */
    AudioQueueStart(osx_audioqueue->queue, NULL);
    upipe_notice_va(upipe, "audioqueue started (%uHz, %hhuch, %db)",
                        sample_rate, channels, sample_size*8);

    return true;
}

/** @internal @This handles audio input.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump upump structure
 */
static void upipe_osx_audioqueue_sink_input_audio(struct upipe *upipe,
                                        struct uref *uref, struct upump *upump)
{
    struct upipe_osx_audioqueue_sink *osx_audioqueue =
                 upipe_osx_audioqueue_sink_from_upipe(upipe);
    struct AudioQueueBuffer *qbuf;
    size_t size = 0;

    if (unlikely(!uref_block_size(uref, &size))) {
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
    qbuf->mUserData = upump->mgr;
    AudioQueueEnqueueBuffer(osx_audioqueue->queue, qbuf, 0, NULL);

    uref_free(uref);
}

/** @internal @This handles input.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump upump structure
 */
static void upipe_osx_audioqueue_sink_input(struct upipe *upipe,
                                        struct uref *uref, struct upump *upump)
{
    struct upipe_osx_audioqueue_sink *osx_audioqueue =
                upipe_osx_audioqueue_sink_from_upipe(upipe);

    /* flow def */
    const char *def;
    if (unlikely(uref_flow_get_def(uref, &def))) {
        if(unlikely(ubase_ncmp(def, "block."))) {
            upipe_throw_flow_def_error(upipe, uref);
            uref_free(uref);
            return;
        }
        upipe_dbg_va(upipe, "flow definition %s", def);
        upipe_osx_audioqueue_sink_create(upipe, uref);
        uref_free(uref);
        return;

    }
    /* flow end */
    if (unlikely(uref_flow_get_end(uref))) {
        upipe_osx_audioqueue_sink_remove(upipe);
        uref_free(uref);
        upipe_throw_need_input(upipe);
        return;
    }

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

    upipe_osx_audioqueue_sink_input_audio(upipe, uref, upump);
}

/** @internal @This processes control commands on a file source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_osx_audioqueue_sink_control(struct upipe *upipe,
                                      enum upipe_command command, va_list args)
{
    switch (command) {
        default:
            return false;
    }
}

/** @internal @This allocates a osx_audioqueue_sink pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_osx_audioqueue_sink_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe)
{
    struct upipe_osx_audioqueue_sink *osx_audioqueue =
                 malloc(sizeof(struct upipe_osx_audioqueue_sink));
    if (unlikely(osx_audioqueue == NULL)) {
        return NULL;
    }
    struct upipe *upipe = upipe_osx_audioqueue_sink_to_upipe(osx_audioqueue);
    upipe_init(upipe, mgr, uprobe);
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
    struct upipe_osx_audioqueue_sink *osx_audioqueue =
                 upipe_osx_audioqueue_sink_from_upipe(upipe);

    upipe_osx_audioqueue_sink_remove(upipe);
    upipe_throw_dead(upipe);
    upipe_clean(upipe);
    free(osx_audioqueue);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_osx_audioqueue_sink_mgr = {
    .signature = UPIPE_OSX_AUDIOQUEUE_SINK_SIGNATURE,

    .upipe_alloc = upipe_osx_audioqueue_sink_alloc,
    .upipe_input = upipe_osx_audioqueue_sink_input,
    .upipe_control = upipe_osx_audioqueue_sink_control,
    .upipe_free = upipe_osx_audioqueue_sink_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for osx_audioqueue_sink pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_osx_audioqueue_sink_mgr_alloc(void)
{
    return &upipe_osx_audioqueue_sink_mgr;
}
