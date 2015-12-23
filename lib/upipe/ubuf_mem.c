/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
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
 * @short Upipe functions to allocate ubuf managers using umem storage
 */

#include <upipe/ubase.h>
#include <upipe/umem.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_mem.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/ubuf_pic_mem.h>
#include <upipe/ubuf_sound_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_sound_flow.h>

#include <stdint.h>

/** @This allocates an ubuf manager using umem from a flow definition.
 *
 * @param ubuf_pool_depth maximum number of ubuf structures in the pool
 * @param shared_pool_depth maximum number of shared structures in the pool
 * @param umem_mgr memory allocator to use for buffers
 * @param flow_def flow definition packet
 * @return pointer to manager, or NULL in case of error
 */
struct ubuf_mgr *ubuf_mem_mgr_alloc_from_flow_def(uint16_t ubuf_pool_depth,
        uint16_t shared_pool_depth, struct umem_mgr *umem_mgr,
        struct uref *flow_def)
{
    const char *def;
    if (!ubase_check(uref_flow_get_def(flow_def, &def)))
        return NULL;

    if (!ubase_ncmp(def, "block.")) {
        uint64_t align = 0;
        int64_t align_offset = 0;
        uref_block_flow_get_align(flow_def, &align);
        uref_block_flow_get_align_offset(flow_def, &align_offset);
        return ubuf_block_mem_mgr_alloc(ubuf_pool_depth, shared_pool_depth,
                                        umem_mgr, align, align_offset);
    }

    if (!ubase_ncmp(def, "pic.")) {
        uint8_t macropixel;
        uint8_t planes;
        if (unlikely(!ubase_check(uref_pic_flow_get_macropixel(flow_def,
                                                               &macropixel) ||
                     !ubase_check(uref_pic_flow_get_planes(flow_def,
                                                           &planes)))))
            return NULL;

        uint8_t hmprepend = 0, hmappend = 0, vprepend = 0, vappend = 0;
        uint64_t align = 0;
        int64_t align_hmoffset = 0;

        uref_pic_flow_get_hmprepend(flow_def, &hmprepend);
        uref_pic_flow_get_hmappend(flow_def, &hmappend);
        uref_pic_flow_get_vprepend(flow_def, &vprepend);
        uref_pic_flow_get_vappend(flow_def, &vappend);
        uref_pic_flow_get_align(flow_def, &align);
        uref_pic_flow_get_align_hmoffset(flow_def, &align_hmoffset);

        struct ubuf_mgr *mgr = ubuf_pic_mem_mgr_alloc(ubuf_pool_depth,
                shared_pool_depth, umem_mgr, macropixel,
                hmprepend * macropixel, hmappend * macropixel,
                vprepend, vappend, align, align_hmoffset);
        if (unlikely(mgr == NULL))
            return NULL;

        for (uint8_t plane = 0; plane < planes; plane++) {
            const char *chroma;
            uint8_t hsub, vsub, macropixel_size;
            if (unlikely(!ubase_check(uref_pic_flow_get_chroma(flow_def,
                                                             &chroma, plane)) ||
                         !ubase_check(uref_pic_flow_get_hsubsampling(flow_def,
                                                             &hsub, plane)) ||
                         !ubase_check(uref_pic_flow_get_vsubsampling(flow_def,
                                                             &vsub, plane)) ||
                         !ubase_check(uref_pic_flow_get_macropixel_size(flow_def,
                                                &macropixel_size, plane)) ||
                         !ubase_check(ubuf_pic_mem_mgr_add_plane(mgr,
                                 chroma, hsub, vsub, macropixel_size)))) {
                ubuf_mgr_release(mgr);
                return NULL;
            }
        }
        return mgr;
    }

    if (!ubase_ncmp(def, "sound.")) {
        uint8_t sample_size;
        uint8_t planes;
        if (unlikely(!ubase_check(uref_sound_flow_get_sample_size(flow_def,
                                                               &sample_size) ||
                     !ubase_check(uref_sound_flow_get_planes(flow_def,
                                                             &planes)))))
            return NULL;

        uint64_t align = 0;
        uref_sound_flow_get_align(flow_def, &align);

        struct ubuf_mgr *mgr = ubuf_sound_mem_mgr_alloc(ubuf_pool_depth,
                shared_pool_depth, umem_mgr, sample_size, align);
        if (unlikely(mgr == NULL))
            return NULL;

        for (uint8_t plane = 0; plane < planes; plane++) {
            const char *channel;
            if (unlikely(!ubase_check(uref_sound_flow_get_channel(flow_def,
                                                         &channel, plane)) ||
                         !ubase_check(ubuf_sound_mem_mgr_add_plane(mgr,
                                                                   channel)))) {
                ubuf_mgr_release(mgr);
                return NULL;
            }
        }
        return mgr;
    }

    return NULL;
}
