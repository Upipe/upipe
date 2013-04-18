/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *          Benjamin Cohen     <bencoh@notk.org>
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
 * @short upipe-av flow def/libavcodec codec_id translation
 */

#include "upipe_av_internal.h"
#include <upipe_av_codecs.h> // auto-generated header

const char *upipe_av_to_flow_def(enum CodecID id)
{
    for (unsigned int i = 0; upipe_av_codecs[i].id; i++)
        if (upipe_av_codecs[i].id == id)
            return upipe_av_codecs[i].flow_def;
    return NULL;
}

enum CodecID upipe_av_from_flow_def(const char *flow_def)
{
    for (unsigned int i = 0; upipe_av_codecs[i].id; i++)
        if (!ubase_ncmp(flow_def, upipe_av_codecs[i].flow_def))
            return upipe_av_codecs[i].id;
    return 0;
}
