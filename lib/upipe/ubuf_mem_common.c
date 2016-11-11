/*
 * Copyright (C) 2012-2016 OpenHeadend S.A.R.L.
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
 * @short Upipe common functions for ubuf managers with umem storage
 */

#include <upipe/ubase.h>
#include <upipe/uatomic.h>
#include <upipe/urefcount.h>
#include <upipe/upool.h>
#include <upipe/umem.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_mem_common.h>

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

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
