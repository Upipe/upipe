/*****************************************************************************
 * upipe_av.c: common functions for libav wrappers
 *****************************************************************************
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
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
 *****************************************************************************/

#include <upipe/udeal.h>
#include <upipe-av/upipe_av.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

#include "upipe_av_internal.h"

#include <stdbool.h>

/** structure to protect exclusive access to avcodec_open() */
struct udeal upipe_av_deal;
/** @internal true if only avcodec was initialized */
static bool avcodec_only = false;

/** @This initializes non-reentrant parts of avcodec and avformat. Call it
 * before allocating managers from this library.
 *
 * @param init_avcodec_only if set to true, avformat source and sink may not
 * be used (saves memory)
 * @return false in case of error
 */
bool upipe_av_init(bool init_avcodec_only)
{
    avcodec_only = init_avcodec_only;

    if (unlikely(!udeal_init(&upipe_av_deal)))
        return false;

    if (unlikely(avcodec_only)) {
        avcodec_register_all();
    } else {
        av_register_all(); /* does avcodec_register_all() behind our back */
        avformat_network_init();
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
}
