/*
 * Copyright (C) 2012-2016 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe ubuf manager for block formats with umem storage
 */

#ifndef _UPIPE_UBUF_BLOCK_MEM_H_
/** @hidden */
#define _UPIPE_UBUF_BLOCK_MEM_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/ubase.h"
#include "upipe/ubuf.h"
#include "upipe/ubuf_block.h"

#include <stdint.h>

/** @hidden */
struct umem_mgr;

/** @This is the signature to use to allocate from an ubuf_pic plane. */
#define UBUF_BLOCK_MEM_ALLOC_FROM_PIC UBASE_FOURCC('m','e','m','p')
/** @This is the signature to use to allocate from an ubuf_sound plane. */
#define UBUF_BLOCK_MEM_ALLOC_FROM_SOUND UBASE_FOURCC('m','e','m','s')

/** @This returns a new ubuf from the block mem allocator, using a chroma of
 * a ubuf pic mem.
 *
 * @param mgr management structure for this ubuf type
 * @param ubuf_pic ubuf pic mem structure to use
 * @param chroma chroma type (see chroma reference)
 * @return pointer to ubuf or NULL in case of failure
 */
static inline struct ubuf *ubuf_block_mem_alloc_from_pic(struct ubuf_mgr *mgr,
        struct ubuf *ubuf_pic, const char *chroma)
{
    return ubuf_alloc(mgr, UBUF_BLOCK_MEM_ALLOC_FROM_PIC, ubuf_pic, chroma);
}

/** @This returns a new ubuf from the block mem allocator, using a plane of
 * a ubuf sound mem.
 *
 * @param mgr management structure for this ubuf type
 * @param ubuf_sound ubuf sound mem structure to use
 * @param channel channel type (see channel reference)
 * @return pointer to ubuf or NULL in case of failure
 */
static inline struct ubuf *ubuf_block_mem_alloc_from_sound(struct ubuf_mgr *mgr,
        struct ubuf *ubuf_sound, const char *channel)
{
    return ubuf_alloc(mgr, UBUF_BLOCK_MEM_ALLOC_FROM_SOUND, ubuf_sound, channel);
}

/** @This allocates a new instance of the ubuf manager for block formats
 * using umem.
 *
 * @param ubuf_pool_depth maximum number of ubuf structures in the pool
 * @param shared_pool_depth maximum number of shared structures in the pool
 * @param umem_mgr memory allocator to use for buffers
 * @param prepend default minimum extra space before buffer (if set to -1, a
 * default sensible value is used)
 * @param append extra space after buffer
 * @param align default alignment in octets (if set to -1, a default sensible
 * value is used)
 * @param align_offset offset of the aligned octet, in octets (may be negative)
 * @return pointer to manager, or NULL in case of error
 */
struct ubuf_mgr *ubuf_block_mem_mgr_alloc(uint16_t ubuf_pool_depth,
                                          uint16_t shared_pool_depth,
                                          struct umem_mgr *umem_mgr,
                                          int prepend, int append,
                                          int align, int align_offset);

#ifdef __cplusplus
}
#endif
#endif
