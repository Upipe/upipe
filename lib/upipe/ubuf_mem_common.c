/*
 * Copyright (C) 2012-2016 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe common functions for ubuf managers with umem storage
 */

#include "upipe/ubase.h"
#include "upipe/uatomic.h"
#include "upipe/ubuf_mem_common.h"

#include <stdlib.h>

/** @internal @This allocates the shared data structure.
 *
 * @param upool pointer to upool
 * @return pointer to ubuf_block_mem or NULL in case of allocation error
 */
void *ubuf_mem_shared_alloc_inner(struct upool *upool)
{
    struct ubuf_mem_shared *shared = malloc(sizeof(struct ubuf_mem_shared));
    if (unlikely(shared == NULL))
        return NULL;
    uatomic_init(&shared->refcount, 1);
    shared->pool = upool;
    return shared;
}

/** @internal @This frees a shared structure.
 *
 * @param upool pointer to upool
 * @param _shared pointer to shared structure to free
 */
void ubuf_mem_shared_free_inner(struct upool *upool, void *_shared)
{
    struct ubuf_mem_shared *shared = (struct ubuf_mem_shared *)_shared;
    uatomic_clean(&shared->refcount);
    free(shared);
}
