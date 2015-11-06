/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
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
 * @short internal interface to av managers
 */

#include <upipe/udeal.h>
#include <upipe/upump.h>

#include <stdbool.h>

#include <libavutil/error.h>
#include <libavcodec/avcodec.h>

/** typical size of the buffer for av_strerror() */
#define UPIPE_AV_STRERROR_SIZE 64

/** @hidden */
enum AVCodecID;
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(54, 51, 100)
/** @hidden */
enum CodecID;
/** @hidden */
#define AVCodecID CodecID
#define AV_CODEC_ID_FIRST_SUBTITLE CODEC_ID_FIRST_SUBTITLE
#define AV_CODEC_ID_FIRST_AUDIO CODEC_ID_FIRST_AUDIO
#endif

/** structure to protect exclusive access to avcodec_open() */
extern struct udeal upipe_av_deal;

/** @This allocates a watcher triggering when exclusive access to avcodec_open()
 * is granted.
 *
 * @param upump_mgr management structure for this event loop
 * @param cb function to call when the watcher triggers
 * @param opaque pointer to the module's internal structure
 * @param refcount pointer to urefcount structure to increment during callback,
 * or NULL
 * @return pointer to allocated watcher, or NULL in case of failure
 */
static inline struct upump *upipe_av_deal_upump_alloc(
        struct upump_mgr *upump_mgr, upump_cb cb, void *opaque,
        struct urefcount *refcount)
{
    return udeal_upump_alloc(&upipe_av_deal, upump_mgr, cb, opaque, refcount);
}

/** @This starts the watcher on exclusive access to avcodec_open().
 *
 * @param upump watcher allocated by @ref udeal_upump_alloc
 */
static inline void upipe_av_deal_start(struct upump *upump)
{
    udeal_start(&upipe_av_deal, upump);
}

/** @This tries to grab the exclusive access to avcodec_open().
 */
static inline bool upipe_av_deal_grab(void)
{
    return udeal_grab(&upipe_av_deal);
}

/** @This yields exclusive access to avcodec_open() previously acquired from
 * @ref upipe_av_deal_grab.
 *
 * @param upump watcher allocated by @ref udeal_upump_alloc
 */
static inline void upipe_av_deal_yield(struct upump *upump)
{
    udeal_yield(&upipe_av_deal, upump);
}

/** @This aborts the watcher before it has had a chance to run. It must only
 * be called in case of abort, otherwise @ref upipe_av_deal_yield does the
 * same job.
 *
 * @param upump watcher allocated by @ref udeal_upump_alloc
 */
static inline void upipe_av_deal_abort(struct upump *upump)
{
    udeal_abort(&upipe_av_deal, upump);
}

/** @This wraps around av_strerror() using ulog storage.
 *
 * @param ulog utility structure passed to the module
 * @param errnum avutil error code
 * @return pointer to a buffer containing a description of the error
 */
#define upipe_av_strerror(errnum, buf)                                      \
    char buf[UPIPE_AV_STRERROR_SIZE];                                       \
    av_strerror(errnum, buf, UPIPE_AV_STRERROR_SIZE);

/** @This allows to convert from avcodec ID to flow definition.
 *
 * @param id avcodec ID
 * @return flow definition, or NULL if not found
 */
const char *upipe_av_to_flow_def(enum AVCodecID id);

/** @This allows to convert to avcodec ID from flow definition.
 *
 * @param flow_def flow definition
 * @return avcodec ID, or 0 if not found
 */
enum AVCodecID upipe_av_from_flow_def(const char *flow_def);
