/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
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
 * @short common functions for libav wrappers
 */

#include <upipe/udeal.h>
#include <upipe/uprobe.h>
#include <upipe-av/upipe_av.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

#include "upipe_av_internal.h"

#include <stdbool.h>
#include <ctype.h>

/** structure to protect exclusive access to avcodec_open() */
struct udeal upipe_av_deal;
/** @internal true if only avcodec was initialized */
static bool avcodec_only = false;
/** @internal probe used by upipe_av_vlog, defined in upipe_av_init() */
static struct uprobe *logprobe = NULL;

/** @internal @This replaces av_log_default_callback
 * @param avcl A pointer to an arbitrary struct of which the first field is a
 * pointer to an AVClass struct.
 * @param level The importance level of the message, lower values signifying
 * higher importance.
 * @param fmt The format string (printf-compatible) that specifies how
 * subsequent arguments are converted to output.
 * @param args optional arguments
 */
static void upipe_av_vlog(void *avcl, int level,
                          const char *fmt, va_list args)
{
    enum uprobe_log_level loglevel = UPROBE_LOG_ERROR;

    switch(level) {
        case AV_LOG_PANIC:
        case AV_LOG_ERROR:
            loglevel = UPROBE_LOG_ERROR;
            break;
        case AV_LOG_WARNING:
            loglevel = UPROBE_LOG_WARNING;
            break;
        case AV_LOG_INFO:
            loglevel = UPROBE_LOG_NOTICE;
            break;
        case AV_LOG_VERBOSE:
            loglevel = UPROBE_LOG_DEBUG;
            break;
        case AV_LOG_DEBUG:
        default:
            loglevel = UPROBE_LOG_VERBOSE;
            break;
    }

    assert(logprobe);
    va_list args_copy;
    va_copy(args_copy, args);
    size_t len = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);
    if (len > 0) {
        char string[len + 1];
        vsnprintf(string, len + 1, fmt, args);
        char *end = string + len - 1;
        if (isspace(*end)) {
            *end = '\0';
        }
        uprobe_log(logprobe, NULL, loglevel, string);
    }
}

/** @This initializes non-reentrant parts of avcodec and avformat. Call it
 * before allocating managers from this library.
 *
 * @param init_avcodec_only if set to true, avformat source and sink may not
 * be used (saves memory)
 * @param uprobe uprobe to print libav messages
 * @return false in case of error
 */
bool upipe_av_init(bool init_avcodec_only, struct uprobe *uprobe)
{
    avcodec_only = init_avcodec_only;

    if (unlikely(!udeal_init(&upipe_av_deal))) {
        uprobe_release(uprobe);
        return false;
    }

    if (unlikely(avcodec_only)) {
        avcodec_register_all();
    } else {
        av_register_all(); /* does avcodec_register_all() behind our back */
        avformat_network_init();
    }

    if (uprobe) {
        logprobe = uprobe;
        av_log_set_callback(upipe_av_vlog);
    }
    return true;
}

/** @This cleans up memory allocated by @ref upipe_av_init. Call it when all
 * avformat- and avcodec-related managers have been freed.
 */
void upipe_av_clean(void)
{
    if (likely(!avcodec_only))
        avformat_network_deinit();
    udeal_clean(&upipe_av_deal);
    if (logprobe)
        uprobe_release(logprobe);
}
