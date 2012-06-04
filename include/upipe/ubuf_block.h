/*****************************************************************************
 * ubuf_block.h: declarations for the ubuf manager for block formats
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

#ifndef _UPIPE_UBUF_BLOCK_H_
/** @hidden */
#define _UPIPE_UBUF_BLOCK_H_

#include <upipe/ubuf.h>

/*
 * Please note that you must maintain at least one manager per thread,
 * because due to the pool implementation, only one thread can make
 * allocations (structures can be released from any thread though).
 */

/** @This allocates a new instance of the ubuf manager for block formats.
 *
 * @param small_pool_depth maximum number of small blocks in the pool
 * @param big_pool_depth maximum number of big blocks in the pool
 * @param size limit between small blocks (including) and big
 * blocks (excluding); also default block size when none indicated (if set to
 * -1, a default sensible value is used)
 * @param prepend default minimum extra space before buffer (if set to -1, a
 * default sensible value is used)
 * @param append default minimum extra space after buffer (if set to -1, a
 * default sensible value is used)
 * @param align default alignment in octets (if set to -1, a default sensible
 * value is used)
 * @param align_offset offset of the aligned octet, in octets (may be negative)
 * @return pointer to manager, or NULL in case of error
 */
struct ubuf_mgr *ubuf_block_mgr_alloc(unsigned int small_pool_depth,
                                      unsigned int big_pool_depth, int size,
                                      int prepend, int append, int align,
                                      int align_offset);

/** @This returns a new struct ubuf from a block allocator.
 *
 * @param mgr management structure for this struct ubuf pool
 * @param size size of the buffer, or -1
 * @return pointer to struct ubuf or NULL in case of failure
 */
static inline struct ubuf *ubuf_block_alloc(struct ubuf_mgr *mgr, int size)
{
    return ubuf_alloc(mgr, UBUF_ALLOC_TYPE_BLOCK, size);
}

/** @This resizes a ubuf.
 *
 * @param mgr management structure used to create a new buffer, if needed
 * (can be NULL if ubuf_single(ubuf))
 * @param ubuf_p reference to a pointer to struct ubuf (possibly modified)
 * @param new_size final size of the buffer (if set to -1, keep same buffer
 * end)
 * @param skip number of octets to skip at the beginning of the buffer
 * (if < 0, extend buffer upwards)
 * @return true if the operation succeeded
 */
static inline bool ubuf_block_resize(struct ubuf_mgr *mgr, struct ubuf **ubuf_p,
                                     int new_size, int skip)
{
    return ubuf_resize(mgr, ubuf_p, UBUF_ALLOC_TYPE_BLOCK, new_size, skip);
}

#endif
