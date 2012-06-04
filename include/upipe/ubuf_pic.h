/*****************************************************************************
 * ubuf_pic.h: declarations for the ubuf manager for picture formats
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

#ifndef _UPIPE_UBUF_PIC_H_
/** @hidden */
#define _UPIPE_UBUF_PIC_H_

#include <upipe/ubuf.h>

/*
 * Please note that you must maintain at least one manager per thread,
 * because due to the pool implementation, only one thread can make
 * allocations (structures can be released from any thread though).
 */

/** @This allocates a new instance of the ubuf manager for picture formats.
 *
 * @param pool_depth maximum number of pictures in the pool
 * @param macropixel number of pixels in a macropixel (typically 1)
 * @param hmprepend extra macropixels added before each line (if set to -1, a
 * default sensible value is used)
 * @param hmappend extra macropixels added after each line (if set to -1, a
 * default sensible value is used)
 * @param vprepend extra lines added before buffer (if set to -1, a
 * default sensible value is used)
 * @param vappend extra lines added after buffer (if set to -1, a
 * default sensible value is used)
 * @param align alignment in octets (if set to -1, a default sensible
 * value is used)
 * @param align_hmoffset horizontal offset of the aligned macropixel (may be
 * negative)
 * @return pointer to manager, or NULL in case of error
 */
struct ubuf_mgr *ubuf_pic_mgr_alloc(unsigned int pool_depth, uint8_t macropixel,
                                    int hmprepend, int hmappend,
                                    int vprepend, int vappend,
                                    int align, int align_hmoffset);

/** @This adds a new plane to a ubuf manager for picture formats. It may
 * only be called on initializing the manager.
 *
 * @param mgr pointer to a ubuf_mgr structure
 * @param hsub horizontal subsamping
 * @param vsub vertical subsamping
 * @param macropixel_size size of a macropixel in octets
 * @return false in case of error
 */
bool ubuf_pic_mgr_add_plane(struct ubuf_mgr *mgr, uint8_t hsub, uint8_t vsub,
                            uint8_t macropixel_size);

/** @This returns a new struct ubuf from a pic allocator.
 *
 * @param mgr management structure for this struct ubuf pool
 * @param hsize horizontal size in pixels
 * @param vsize vertical size in lines
 * @return pointer to struct ubuf or NULL in case of failure
 */
static inline struct ubuf *ubuf_pic_alloc(struct ubuf_mgr *mgr,
                                          int hsize, int vsize)
{
    return ubuf_alloc(mgr, UBUF_ALLOC_TYPE_PICTURE, hsize, vsize);
}

/** @This resizes a ubuf.
 *
 * @param mgr management structure used to create a new buffer, if needed
 * (can be NULL if ubuf_single(ubuf) and requested picture is smaller than
 * the original)
 * @param ubuf_p reference to a pointer to struct ubuf (possibly modified)
 * @param new_hsize final horizontal size of the buffer, in pixels (if set
 * to -1, keep same line ends)
 * @param new_vsize final vertical size of the buffer, in lines (if set
 * to -1, keep same last line)
 * @param hskip number of pixels to skip at the beginning of each line (if < 0,
 * extend the picture leftwards)
 * @param vskip number of lines to skip at the beginning of the picture (if < 0,
 * extend the picture upwards)
 * @return true if the operation succeeded
 */
static inline bool ubuf_pic_resize(struct ubuf_mgr *mgr, struct ubuf **ubuf_p,
                                   int new_hsize, int new_vsize,
                                   int hskip, int vskip)
{
    return ubuf_resize(mgr, ubuf_p, UBUF_ALLOC_TYPE_PICTURE,
                       new_hsize, new_vsize, hskip, vskip);
}

#endif
