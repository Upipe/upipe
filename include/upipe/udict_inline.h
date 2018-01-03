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
 * @short Upipe inline manager of dictionary of attributes
 * This manager stores all attributes inline inside a single umem block.
 * This is designed in order to minimize calls to memory allocators, and
 * to transmit dictionaries over streams.
 */

#ifndef _UPIPE_UDICT_INLINE_H_
/** @hidden */
#define _UPIPE_UDICT_INLINE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/udict.h>

/** @This allocates a new instance of the inline udict manager.
 *
 * @param udict_pool_depth maximum number of udict structures in the pool
 * @param umem_mgr memory allocator to use for buffers
 * @param min_size minimum allocated space for the udict (if set to -1, a
 * default sensible value is used)
 * @param extra_size extra space added when the udict needs to be resized
 * (if set to -1, a default sensible value is used)
 * @return pointer to manager, or NULL in case of error
 */
struct udict_mgr *udict_inline_mgr_alloc(uint16_t udict_pool_depth,
                                         struct umem_mgr *umem_mgr,
                                         int min_size, int extra_size);

#ifdef __cplusplus
}
#endif
#endif
