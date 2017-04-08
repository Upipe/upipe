/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
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
 * @short Upipe h264 & h265 attributes for uref
 */

#ifndef _UPIPE_UREF_H26X_H_
/** @hidden */
#define _UPIPE_UREF_H26X_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/uref_block.h>

UREF_ATTR_UNSIGNED_VA(h26x, nal_offset, "h26x.n[%" PRIu64"]", nal offset,
        uint64_t nal, nal)

/** @This iterates over the NALs of an uref. Initialize counter_p at 0, and
 * don't modify the arguments between calls to this function.
 *
 * @param uref uref description structure
 * @param counter_p index of the NAL in the uref, initialize iteration at 0,
 * will be incremented on every call
 * @param offset_p filled in with the offset in octets of the NAL in the uref
 * @param size_p filled in with the size in octets of the NAL in the uref,
 * possibly including the next start code
 * @param correction correction to apply to the offsets is the uref has been
 * modfied in the meantime
 * @return an error code
 */
static inline int uref_h26x_iterate_nal(struct uref *uref, uint64_t *counter_p,
                                        uint64_t *offset_p, uint64_t *size_p,
                                        int64_t correction)
{
    uint64_t next_offset;
    if (!*counter_p)
        *offset_p = 0;
    else
        *offset_p += *size_p;

    if (ubase_check(uref_h26x_get_nal_offset(uref, &next_offset,
                                             *counter_p))) {
        if (correction) {
            next_offset += correction;
            UBASE_RETURN(uref_h26x_set_nal_offset(uref, next_offset,
                                                  *counter_p))
        }
        *size_p = next_offset - *offset_p;
        (*counter_p)++;
        return UBASE_ERR_NONE;
    }

    size_t size;
    UBASE_RETURN(uref_block_size(uref, &size))
    if (size > *offset_p) {
        *size_p = size - *offset_p;
        (*counter_p)++;
        return UBASE_ERR_NONE;
    }
    return UBASE_ERR_INVALID;
}

/** @This prepends a NAL to the given uref.
 *
 * @param uref uref description structure
 * @param ubuf ubuf to prepend
 * @return an error code
 */
static inline int uref_h26x_prepend_nal(struct uref *uref, struct ubuf *ubuf)
{
    size_t ubuf_size;
    UBASE_RETURN(ubuf_block_size(ubuf, &ubuf_size))
    UBASE_RETURN(uref_block_insert(uref, 0, ubuf))

    uint64_t nal_units = 0;
    uint64_t nal_offset = 0;
    uint64_t next_offset;
    while (ubase_check(uref_h26x_get_nal_offset(uref, &next_offset,
                                                nal_units))) {
        UBASE_RETURN(uref_h26x_set_nal_offset(uref, nal_offset + ubuf_size,
                                              nal_units))
        nal_units++;
        nal_offset = next_offset;
    }

    UBASE_RETURN(uref_h26x_set_nal_offset(uref, nal_offset + ubuf_size,
                                          nal_units))
    return UBASE_ERR_NONE;
}

#ifdef __cplusplus
}
#endif
#endif
