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
 * @short Upipe block flow definition attributes for uref
 */

#ifndef _UPIPE_UREF_BLOCK_FLOW_H_
/** @hidden */
#define _UPIPE_UREF_BLOCK_FLOW_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/uref_flow.h>

#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>

/** @internal flow definition prefix for block allocator */
#define UREF_BLOCK_FLOW_DEF "block."

UREF_ATTR_UNSIGNED(block_flow, octetrate, "b.octetrate", octets per second)
UREF_ATTR_UNSIGNED(block_flow, max_octetrate, "b.max_octetrate",
        maximum octets per second for profile/level)
UREF_ATTR_UNSIGNED(block_flow, buffer_size, "b.bs",
        size of coded buffer in octets)
UREF_ATTR_UNSIGNED(block_flow, max_buffer_size, "b.max_bs",
        maximum size of coded buffer in octets)
UREF_ATTR_UNSIGNED(block_flow, align, "b.align", alignment in octets)
UREF_ATTR_INT(block_flow, align_offset, "b.align_offset",
        offset of the aligned octet)
UREF_ATTR_UNSIGNED(block_flow, size, "b.size", block size)

/** @This allocates a control packet to define a new block flow.
 *
 * @param mgr uref management structure
 * @param def_suffix suffix to append to "block." flow definition
 * @return pointer to uref control packet, or NULL in case of error
 */
static inline struct uref *uref_block_flow_alloc_def(struct uref_mgr *mgr,
                                                     const char *def_suffix)
{
    struct uref *uref = uref_alloc_control(mgr);
    if (unlikely(uref == NULL))
        return NULL;
    if (unlikely(def_suffix == NULL))
        def_suffix = "";

    char def[sizeof(UREF_BLOCK_FLOW_DEF) + strlen(def_suffix)];
    sprintf(def, UREF_BLOCK_FLOW_DEF "%s", def_suffix);
    if (unlikely(!ubase_check(uref_flow_set_def(uref, def)))) {
        uref_free(uref);
        return NULL;
    }
    return uref;
}

/** @This allocates a control packet to define a new block flow, with
 * printf-style definition suffix generation
 *
 * @param mgr uref management structure
 * @param def_suffix suffix to append to "block." flow definition
 * @return pointer to uref control packet, or NULL in case of error
 */
static inline struct uref *uref_block_flow_alloc_def_va(struct uref_mgr *mgr,
                                                        const char *format, ...)
{
    UBASE_VARARG(uref_block_flow_alloc_def(mgr, string))
}

/** @This clears the attributes that are no longer relevant when the block is
 * decoded.
 *
 * @param uref uref control packet
 */
static inline void uref_block_flow_clear_format(struct uref *uref)
{
    uref_block_flow_delete_octetrate(uref);
    uref_block_flow_delete_max_octetrate(uref);
    uref_block_flow_delete_buffer_size(uref);
    uref_block_flow_delete_max_buffer_size(uref);
    uref_block_flow_delete_align(uref);
    uref_block_flow_delete_align_offset(uref);
    uref_block_flow_delete_size(uref);
}

#undef UREF_BLOCK_FLOW_DEF

#ifdef __cplusplus
}
#endif
#endif
