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

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/ubuf.h>
#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_sound.h>
#include <upipe/upump.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe/uqueue.h>

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

/** @internal expected input flow format */
#define EXPECTED_FLOW_DEF   "sound."
/** @internal AudioQueueBuffer size */
#define DEFAULT_BUFFER_SIZE 4096
/** @internal number of AudioQueueBuffer */
#define N_BUFFER            3
/** @internal warn interval for late packets */
#define LATE_WARN           (UCLOCK_FREQ)

/** @internal @This describes an OSX AudioQueue sink pipe. */
struct upipe_osx_audioqueue_sink {
    /** refcount management structure */
    struct urefcount urefcount;

    /** audioqueue */
    AudioQueueRef queue;
    /** audio buffers */
    AudioQueueBufferRef buffers[N_BUFFER];
    /** sound volume */
    float volume;

    /** public upipe structure */
    struct upipe upipe;

    /** input flow def */
    struct uref *input_flow;
    /** flow format attributes */
    struct uref *flow_attr;
    /** list of uref */
    struct uchain urefs;
    /** number of uref in the list */
    unsigned int nb_urefs;
    /** maximum of uref in the list */
    unsigned int max_urefs;
    /** list of blockers */
    struct uchain blockers;
    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** write watcher */
    struct upump *watcher;
    /** property listener */
    struct upump *listener;
    /** uclock */
    struct uclock *uclock;
    /** uclock request */
    struct urequest uclock_request;
    /** buffer queue */
    struct uqueue uqueue;
    /** extra queue space */
    char uqueue_extra[uqueue_sizeof(N_BUFFER)];
    /** audio buffer size */
    size_t buffer_size;
    /** audio queue is running? */
    bool running;
    /** listener event fd */
    struct ueventfd ev;
};

static bool upipe_osx_audioqueue_sink_handle(struct upipe *upipe,
                                             struct uref *uref,
                                             struct upump **upump_p);

UPIPE_HELPER_UPIPE(upipe_osx_audioqueue_sink, upipe,
                   UPIPE_OSX_AUDIOQUEUE_SINK_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_osx_audioqueue_sink, urefcount,
                       upipe_osx_audioqueue_sink_free)
UPIPE_HELPER_VOID(upipe_osx_audioqueue_sink)
UPIPE_HELPER_FLOW_DEF(upipe_osx_audioqueue_sink, input_flow, flow_attr);
UPIPE_HELPER_INPUT(upipe_osx_audioqueue_sink, urefs, nb_urefs, max_urefs,
                   blockers, upipe_osx_audioqueue_sink_handle);
UPIPE_HELPER_UPUMP_MGR(upipe_osx_audioqueue_sink, upump_mgr);
UPIPE_HELPER_UPUMP(upipe_osx_audioqueue_sink, watcher, upump_mgr);
UPIPE_HELPER_UPUMP(upipe_osx_audioqueue_sink, listener, upump_mgr);
UPIPE_HELPER_UCLOCK(upipe_osx_audioqueue_sink, uclock, uclock_request,
					NULL, upipe_throw_provide_request,
                                        NULL);

/** @internal @This is called by AudioQueue after reading a buffer
 * @param user_data description structure of the pipe (void)
 * @param queue AudioQueue
 * @param qbuf AudioQueue buffer
 */
static void upipe_osx_audioqueue_sink_cb(void *user_data,
                                         AudioQueueRef queue,
                                         struct AudioQueueBuffer *qbuf)
{
    struct uqueue *uqueue = user_data;
    assert(uqueue_push(uqueue, qbuf));
}

/** @internal @This is called by AudioQueue when a property change.
 *
 * @param user_data uevent to notify (void)
 * @param queue AudioQueue
 * @param id property ID
 */
static void upipe_osx_audioqueue_sink_listener(void *user_data,
                                               AudioQueueRef queue,
                                               AudioQueuePropertyID id)
{
    struct ueventfd *ev = user_data;

    if (id != kAudioQueueProperty_IsRunning)
        return;

    ueventfd_write(ev);
}

/** @internal @This is called when the AudioQueue notify a property change.
 *
 * @param upump ueventfd watcher
 */
static void upipe_osx_audioqueue_sink_update(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_osx_audioqueue_sink *upipe_osx_audioqueue_sink =
        upipe_osx_audioqueue_sink_from_upipe(upipe);
    uint32_t running;
    uint32_t size = sizeof (running);

    ueventfd_read(&upipe_osx_audioqueue_sink->ev);

    OSStatus status = AudioQueueGetProperty(upipe_osx_audioqueue_sink->queue,
                                            kAudioQueueProperty_IsRunning,
                                            &running, &size);
    if (status) {
        upipe_err(upipe, "fail to get property");
        return;
    }

    upipe_notice_va(upipe, "audio queue %s", running ? "running" : "stopped");
    upipe_osx_audioqueue_sink->running = running;

    if (upipe_osx_audioqueue_sink_check_input(upipe))
        return;
    upipe_osx_audioqueue_sink_output_input(upipe);
    if (upipe_osx_audioqueue_sink_check_input(upipe))
        upipe_release(upipe);
}

/** @internal @This destroys the current audioqueue
 * @param upipe description structure of the pipe
 */
static void upipe_osx_audioqueue_sink_destroy(struct upipe *upipe)
{
    struct upipe_osx_audioqueue_sink *upipe_osx_audioqueue_sink =
                 upipe_osx_audioqueue_sink_from_upipe(upipe);

    upipe_osx_audioqueue_sink_store_flow_def_input(upipe, NULL);
    if (upipe_osx_audioqueue_sink->queue)
        AudioQueueStop(upipe_osx_audioqueue_sink->queue, true);
    while (uqueue_pop(&upipe_osx_audioqueue_sink->uqueue, AudioQueueBufferRef));
    if (upipe_osx_audioqueue_sink->queue)
        AudioQueueDispose(upipe_osx_audioqueue_sink->queue, true);
    upipe_osx_audioqueue_sink->queue = NULL;
    upipe_notice(upipe, "audioqueue destroyed");
}

/** @internal @This is called when an AudioQueueBuffer is ready to be enqueue.
 *
 * @param upump uqueue watcher
 */
static void upipe_osx_audioqueue_sink_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_osx_audioqueue_sink *upipe_osx_audioqueue_sink =
        upipe_osx_audioqueue_sink_from_upipe(upipe);
    AudioQueueBufferRef qbuf;
    struct uref *uref;
    const char *def;

    qbuf = uqueue_pop(&upipe_osx_audioqueue_sink->uqueue, AudioQueueBufferRef);
    if (!qbuf)
        return;

    uref = upipe_osx_audioqueue_sink_pop_input(upipe);
    if (unlikely(!uref)) {
        upipe_warn(upipe, "underflow, playing silence");
        qbuf->mAudioDataByteSize = qbuf->mAudioDataBytesCapacity;
        memset(qbuf->mAudioData, 0, qbuf->mAudioDataByteSize);
        assert(AudioQueueEnqueueBuffer(upipe_osx_audioqueue_sink->queue,
                                       qbuf, 0, NULL) == 0);
        return;
    }

    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        upipe_osx_audioqueue_sink_unshift_input(upipe, uref);
        AudioQueueStop(upipe_osx_audioqueue_sink->queue, false);
        return;
    }

    size_t size;
    uint8_t sample_size;
    ubase_assert(uref_sound_size(uref, &size, &sample_size));

    const void *audios[1];
    ubase_assert(uref_sound_read_void(uref, 0, size, audios, 1));

    assert(size * sample_size < qbuf->mAudioDataBytesCapacity);
    memcpy(qbuf->mAudioData, audios[0], size * sample_size);
    qbuf->mAudioDataByteSize = size * sample_size;
    uref_sound_unmap(uref, 0, size * sample_size, 1);
    uref_free(uref);

    assert(AudioQueueEnqueueBuffer(upipe_osx_audioqueue_sink->queue,
                                   qbuf, 0, NULL) == 0);

    upipe_osx_audioqueue_sink_unblock_input(upipe);
    if (upipe_osx_audioqueue_sink_check_input(upipe))
        upipe_release(upipe);
}

/** @internal @This set the input flow def and creates an AudioQueue.
 *
 * @param upipe description structure of the pipe
 * @param flow input flow format to set
 * @return an error code
 */
static int upipe_osx_audioqueue_sink_set_flow_def_real(struct upipe *upipe,
                                                       struct uref *flow)
{
    struct upipe_osx_audioqueue_sink *upipe_osx_audioqueue_sink =
        upipe_osx_audioqueue_sink_from_upipe(upipe);
    struct AudioStreamBasicDescription fmt;
    uint8_t planes = 0;
    uint64_t sample_rate = 0; /* hush gcc */
    uint8_t channels = 0;
    uint8_t sample_size = 0;
    OSStatus status;
    int ret;

    upipe_osx_audioqueue_sink_destroy(upipe);

    /* check flow format */
    ubase_assert(uref_flow_match_def(flow, EXPECTED_FLOW_DEF));

    /* retrieve flow format information */
    ubase_assert(uref_sound_flow_get_planes(flow, &planes));
    assert(planes == 1);
    ubase_assert(uref_sound_flow_get_rate(flow, &sample_rate));
    ubase_assert(uref_sound_flow_get_sample_size(flow, &sample_size));
    ubase_assert(uref_sound_flow_get_channels(flow, &channels));

    /* build format description */
    memset(&fmt, 0, sizeof(struct AudioStreamBasicDescription));
    fmt.mSampleRate = sample_rate;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kAudioFormatFlagIsPacked;
    if (ubase_check(uref_flow_match_def(flow, EXPECTED_FLOW_DEF "s")))
        fmt.mFormatFlags |= kAudioFormatFlagIsSignedInteger;
    fmt.mFramesPerPacket = 1;
    fmt.mChannelsPerFrame = channels;
    fmt.mBytesPerPacket = fmt.mBytesPerFrame = sample_size * channels;
    fmt.mBitsPerChannel = sample_size * 8;

    /* create output */
    AudioQueueRef queue;
    status = AudioQueueNewOutput(&fmt, upipe_osx_audioqueue_sink_cb,
                                 &upipe_osx_audioqueue_sink->uqueue,
                                 NULL, kCFRunLoopCommonModes, 0,
                                 &queue);
    if (unlikely(status)) {
        upipe_warn(upipe, "unsupported data format");
        uref_free(flow);
        return UBASE_ERR_EXTERNAL;
    }

    status = AudioQueueAddPropertyListener(queue, kAudioQueueProperty_IsRunning,
                                           upipe_osx_audioqueue_sink_listener,
                                           &upipe_osx_audioqueue_sink->ev);
    if (unlikely(status)) {
        upipe_err(upipe, "fail to add property listener");
        AudioQueueDispose(queue, true);
        uref_free(flow);
        return UBASE_ERR_EXTERNAL;
    }

    /* change volume */
    status = AudioQueueSetParameter(queue, kAudioQueueParam_Volume, 
                                    upipe_osx_audioqueue_sink->volume);
    if (unlikely(status))
        upipe_warn(upipe, "fail to set volume");

    /* allocate buffers */
    for (unsigned i = 0; i < N_BUFFER; i++) {
        AudioQueueBufferRef buffer = upipe_osx_audioqueue_sink->buffers[i];

        status = AudioQueueAllocateBuffer(queue,
                                          upipe_osx_audioqueue_sink->buffer_size,
                                          &buffer);
        if (unlikely(status)) {
            upipe_err(upipe, "fail to allocate audio queue buffers");
            AudioQueueDispose(queue, true);
            uref_free(flow);
            return UBASE_ERR_ALLOC;
        }

        assert(uqueue_push(&upipe_osx_audioqueue_sink->uqueue, buffer));
    }

    /* start queue ! */
    status = AudioQueueStart(queue, NULL);
    if (unlikely(status)) {
        upipe_err(upipe, "fail to start audio queue");
        AudioQueueDispose(queue, true);
        uref_free(flow);
        return UBASE_ERR_EXTERNAL;
    }

    upipe_osx_audioqueue_sink->queue = queue;
    upipe_osx_audioqueue_sink_store_flow_def_input(upipe, flow);

    upipe_notice_va(upipe, "audioqueue started (%uHz, %hhuch, %db)",
                    sample_rate, channels, sample_size*8);

    return UBASE_ERR_NONE;
}

/** @internal @This handles an input uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref to handle
 * @param upump_p reference to source pump to block
 * @return true if the uref was handled, false otherwise
 */
static bool upipe_osx_audioqueue_sink_handle(struct upipe *upipe,
                                             struct uref *uref,
                                             struct upump **upump_p)
{
    struct upipe_osx_audioqueue_sink *upipe_osx_audioqueue_sink =
        upipe_osx_audioqueue_sink_from_upipe(upipe);
    const char *def;

    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        if (!upipe_osx_audioqueue_sink->running) {
            upipe_osx_audioqueue_sink_set_flow_def_real(upipe, uref);
            return true;
        }
        else
            return false;
    }

    return false;
}

/** @internal @This handles input.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 */
static void upipe_osx_audioqueue_sink_input(struct upipe *upipe,
                                            struct uref *uref,
                                            struct upump **upump_p)
{
    struct upipe_osx_audioqueue_sink *upipe_osx_audioqueue_sink =
                upipe_osx_audioqueue_sink_from_upipe(upipe);
    bool checked = upipe_osx_audioqueue_sink_check_input(upipe);

    if (!checked || !upipe_osx_audioqueue_sink_handle(upipe, uref, upump_p)) {
        upipe_osx_audioqueue_sink_hold_input(upipe, uref);
        upipe_osx_audioqueue_sink_block_input(upipe, upump_p);

        if (checked)
            upipe_use(upipe);
    }
}

/** @internal @This creates a new audioqueue
 * @param upipe description structure of the pipe
 * @param flow description structure of the flow
 * @return an error code
 */
static int upipe_osx_audioqueue_sink_set_flow_def(struct upipe *upipe,
                                                  struct uref *flow)
{
    struct upipe_osx_audioqueue_sink *osx_audioqueue =
        upipe_osx_audioqueue_sink_from_upipe(upipe);
    struct uref *flow_dup;
    uint8_t planes = 0;
    uint64_t sample_rate = 0; /* hush gcc */
    uint8_t channels = 0;
    uint8_t sample_size = 0;
    OSStatus status;

    /* check flow format */
    UBASE_RETURN(uref_flow_match_def(flow, EXPECTED_FLOW_DEF));

    /* retrieve flow format information */
    UBASE_RETURN(uref_sound_flow_get_planes(flow, &planes));
    if (planes != 1) {
        upipe_warn(upipe, "multiple planes not supported");
        return UBASE_ERR_INVALID;
    }
    UBASE_RETURN(uref_sound_flow_get_rate(flow, &sample_rate));
    UBASE_RETURN(uref_sound_flow_get_sample_size(flow, &sample_size));
    UBASE_RETURN(uref_sound_flow_get_channels(flow, &channels));

    /* duplicate flow */
    flow_dup = uref_dup(flow);
    UBASE_ALLOC_RETURN(flow_dup);

    upipe_input(upipe, flow_dup, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This checks for upump manager and creates the needed upumps.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_osx_audioqueue_sink_check(struct upipe *upipe)
{
    struct upipe_osx_audioqueue_sink *upipe_osx_audioqueue_sink =
        upipe_osx_audioqueue_sink_from_upipe(upipe);

    /* check for upump manager */
    int ret = upipe_osx_audioqueue_sink_check_upump_mgr(upipe);
    if (unlikely(!ubase_check(ret)))
        return ret;

    /* create property listener */
    struct upump *listener =
        ueventfd_upump_alloc(&upipe_osx_audioqueue_sink->ev,
                             upipe_osx_audioqueue_sink->upump_mgr,
                             upipe_osx_audioqueue_sink_update,
                             upipe, upipe->refcount);
    if (unlikely(!listener)) {
        upipe_err(upipe, "fail to allocate upump");
        return UBASE_ERR_ALLOC;
    }
    upump_start(listener);

    /* creeate queue watcher */
    struct upump *watcher =
        uqueue_upump_alloc_pop(&upipe_osx_audioqueue_sink->uqueue,
                               upipe_osx_audioqueue_sink->upump_mgr,
                               upipe_osx_audioqueue_sink_worker,
                               upipe, upipe->refcount);
    if (unlikely(!watcher)) {
        upipe_err(upipe, "fail to allocate watcher");
        upump_free(listener);
        return UBASE_ERR_ALLOC;
    }
    upump_start(watcher);

    /* store the upumps */
    upipe_osx_audioqueue_sink_set_listener(upipe, listener);
    upipe_osx_audioqueue_sink_set_watcher(upipe, watcher);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_osx_audioqueue_sink_control_real(struct upipe *upipe,
                                                  int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_control_provide_request(upipe, command, args));

    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_osx_audioqueue_sink_set_listener(upipe, NULL);
            upipe_osx_audioqueue_sink_set_watcher(upipe, NULL);
            return upipe_osx_audioqueue_sink_attach_upump_mgr(upipe);

        case UPIPE_ATTACH_UCLOCK:
            upipe_osx_audioqueue_sink_require_uclock(upipe);
            return UBASE_ERR_NONE;

        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_osx_audioqueue_sink_set_flow_def(upipe, flow_def);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This processes control commands, and checks the status of
 * the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_osx_audioqueue_sink_control(struct upipe *upipe,
                                             int command, va_list args)
{
    UBASE_RETURN(upipe_osx_audioqueue_sink_control_real(upipe, command, args));
    return upipe_osx_audioqueue_sink_check(upipe);
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
                                                     uint32_t signature,
                                                     va_list args)
{
    struct upipe *upipe =
        upipe_osx_audioqueue_sink_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_osx_audioqueue_sink *upipe_osx_audioqueue_sink =
        upipe_osx_audioqueue_sink_from_upipe(upipe);
    upipe_osx_audioqueue_sink_init_urefcount(upipe);
    upipe_osx_audioqueue_sink_init_flow_def(upipe);
    upipe_osx_audioqueue_sink_init_input(upipe);
    upipe_osx_audioqueue_sink_init_upump_mgr(upipe);
    upipe_osx_audioqueue_sink_init_watcher(upipe);
    upipe_osx_audioqueue_sink_init_listener(upipe);
    upipe_osx_audioqueue_sink_init_uclock(upipe);
    upipe_osx_audioqueue_sink_set_max_length(upipe, N_BUFFER + 1);
    upipe_osx_audioqueue_sink->queue = NULL;
    upipe_osx_audioqueue_sink->running = false;
    upipe_osx_audioqueue_sink->buffer_size = DEFAULT_BUFFER_SIZE;
    uqueue_init(&upipe_osx_audioqueue_sink->uqueue, N_BUFFER,
                upipe_osx_audioqueue_sink->uqueue_extra);
    ueventfd_init(&upipe_osx_audioqueue_sink->ev, false);
    upipe_osx_audioqueue_sink->volume = 1.0;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_osx_audioqueue_sink_free(struct upipe *upipe)
{
    struct upipe_osx_audioqueue_sink *upipe_osx_audioqueue_sink =
        upipe_osx_audioqueue_sink_from_upipe(upipe);

    upipe_osx_audioqueue_sink_destroy(upipe);

    upipe_throw_dead(upipe);

    ueventfd_clean(&upipe_osx_audioqueue_sink->ev);
    uqueue_clean(&upipe_osx_audioqueue_sink->uqueue);
    upipe_osx_audioqueue_sink_clean_uclock(upipe);
    upipe_osx_audioqueue_sink_clean_listener(upipe);
    upipe_osx_audioqueue_sink_clean_watcher(upipe);
    upipe_osx_audioqueue_sink_clean_upump_mgr(upipe);
    upipe_osx_audioqueue_sink_clean_input(upipe);
    upipe_osx_audioqueue_sink_clean_flow_def(upipe);
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
