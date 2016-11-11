/*****************************************************************************
 * upipe_av.h: application interface for common function for libav wrappers
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

#ifndef _UPIPE_AV_UPIPE_AV_H_
/** @hidden */
#define _UPIPE_AV_UPIPE_AV_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/** @hidden */
struct uprobe;

/** @This initializes non-reentrant parts of avcodec and avformat. Call it
 * before allocating managers from this library.
 *
 * @param init_avcodec_only if set to true, avformat source and sink may not
 * be used (saves memory)
 * @param uprobe uprobe to print libav messages
 * @return false in case of error
 */
bool upipe_av_init(bool init_avcodec_only, struct uprobe *uprobe);

/** @This cleans up memory allocated by @ref upipe_av_init. Call it when all
 * avformat- and avcodec-related managers have been freed.
 */
void upipe_av_clean(void);

#ifdef __cplusplus
}
#endif
#endif
