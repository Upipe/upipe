/*
 * Copyright (C) 2015 Arnaud de Turckheim
 *
 * Authors: Arnaud de Turckheim
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
 * @short Upipe module to dump input urefs
 */

#ifndef _UPIPE_MODULES_UPIPE_DUMP_H_
# define _UPIPE_MODULES_UPIPE_DUMP_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

# define UPIPE_DUMP_SIGNATURE UBASE_FOURCC('d','u','m','p')

enum upipe_dump_command {
    UPIPE_DUMP_SENTINEL = UPIPE_CONTROL_LOCAL,

    UPIPE_DUMP_SET_MAX_LEN,
    UPIPE_DUMP_SET_TEXT_MODE,
};

static inline int upipe_dump_set_max_len(struct upipe *upipe, size_t len)
{
    return upipe_control(upipe, UPIPE_DUMP_SET_MAX_LEN, UPIPE_DUMP_SIGNATURE,
                         len);
}

static inline int upipe_dump_set_text_mode(struct upipe *upipe)
{
    return upipe_control(upipe, UPIPE_DUMP_SET_TEXT_MODE, UPIPE_DUMP_SIGNATURE);
}

struct upipe_mgr *upipe_dump_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
