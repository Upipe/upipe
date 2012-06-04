/*****************************************************************************
 * uref_block_flow.h: block flow definition attributes for uref
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

#ifndef _UPIPE_UREF_BLOCK_FLOW_H_
/** @hidden */
#define _UPIPE_UREF_BLOCK_FLOW_H_

#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>

#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/uref_flow.h>

/** @internal flow definition prefix for block allocator */
#define UREF_BLOCK_FLOW_DEFINITION "block."

UREF_ATTR_TEMPLATE(block_flow, octetrate, "b.octetrate", unsigned, uint64_t, octets per second)
UREF_ATTR_TEMPLATE(block_flow, max_octetrate, "b.max_octetrate", unsigned, uint64_t, maximum octets per second for profile/level)
UREF_ATTR_TEMPLATE(block_flow, cpb_buffer, "b.cpb_buffer", unsigned, uint64_t, size of coded picture buffer in octets)
UREF_ATTR_TEMPLATE(block_flow, prepend, "b.prepend", unsigned, uint64_t, extra octets added before buffer)
UREF_ATTR_TEMPLATE(block_flow, append, "b.append", unsigned, uint64_t, extra octets added after buffer)
UREF_ATTR_TEMPLATE(block_flow, align, "b.align", unsigned, uint64_t, alignment in octets)
UREF_ATTR_TEMPLATE(block_flow, align_offset, "b.align_offset", int, int64_t, offset of the aligned octet)
/* size may also be specified in a flow definition packet when
 * the flow has fixed block size */

/** @This allocates a control packet to define a new block flow.
 *
 * @param mgr uref management structure
 * @param def_suffix suffix to append to "block." flow definition
 * @return pointer to uref control packet, or NULL in case of error
 */
static inline struct uref *uref_block_flow_alloc_definition(struct uref_mgr *mgr,
                                                            const char *def_suffix)
{
    struct uref *uref = uref_alloc_control(mgr);
    if (unlikely(uref == NULL)) return NULL;
    if (unlikely(def_suffix == NULL))
        def_suffix = "";

    char def[sizeof(UREF_BLOCK_FLOW_DEFINITION) + strlen(def_suffix)];
    sprintf(def, UREF_BLOCK_FLOW_DEFINITION "%s", def_suffix);
    if (unlikely(!uref_flow_set_definition(&uref, def))) {
        uref_release(uref);
        return NULL;
    }
    return uref;
}

#undef UREF_BLOCK_FLOW_DEFINITION

#endif
