/*
 * Copyright (C) 2014-2015 OpenHeadend S.A.R.L.
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
 * @short probe catching provide_request events asking for a ubuf manager, and keeping the managers in a pool
 */

#include <upipe/ubase.h>
#include <upipe/umem.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_mem.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_ubuf_mem_pool.h>
#include <upipe/uprobe_helper_alloc.h>
#include <upipe/upipe.h>

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/** @This is a manager registered into the probe as a thread-safe linked list.
 */
struct uprobe_ubuf_mem_pool_element {
    /** pointer to ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** pointer to next element */
    uatomic_ptr_t next;
};

/** @internal @This catches events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return an error code
 */
static int uprobe_ubuf_mem_pool_throw(struct uprobe *uprobe,
                                      struct upipe *upipe,
                                      int event, va_list args)
{
    struct uprobe_ubuf_mem_pool *uprobe_ubuf_mem_pool =
        uprobe_ubuf_mem_pool_from_uprobe(uprobe);

    if (event != UPROBE_PROVIDE_REQUEST ||
        uprobe_ubuf_mem_pool->umem_mgr == NULL)
        return uprobe_throw_next(uprobe, upipe, event, args);

    va_list args_copy;
    va_copy(args_copy, args);
    struct urequest *urequest = va_arg(args_copy, struct urequest *);
    va_end(args_copy);

    if (urequest->type == UREQUEST_SINK_LATENCY)
        return urequest_provide_sink_latency(urequest, 0);

    if (urequest->type != UREQUEST_UBUF_MGR &&
        urequest->type != UREQUEST_FLOW_FORMAT)
        return uprobe_throw_next(uprobe, upipe, event, args);

    struct uref *uref = uref_dup(urequest->uref);
    if (unlikely(uref == NULL))
        return UBASE_ERR_ALLOC;

    if (urequest->type == UREQUEST_FLOW_FORMAT)
        return urequest_provide_flow_format(urequest, uref);

    uatomic_ptr_t *elem_p = &uprobe_ubuf_mem_pool->first;
    struct uprobe_ubuf_mem_pool_element *elem;

    for ( ; ; ) {
        while ((elem = uatomic_ptr_load_ptr(elem_p,
                            struct uprobe_ubuf_mem_pool_element *)) != NULL) {
            if (ubase_check(ubuf_mgr_check(elem->ubuf_mgr, uref)))
                return urequest_provide_ubuf_mgr(urequest,
                            ubuf_mgr_use(elem->ubuf_mgr), uref);
            elem_p = &elem->next;
        }

        struct ubuf_mgr *ubuf_mgr = ubuf_mem_mgr_alloc_from_flow_def(
                uprobe_ubuf_mem_pool->ubuf_pool_depth,
                uprobe_ubuf_mem_pool->shared_pool_depth,
                uprobe_ubuf_mem_pool->umem_mgr, uref);
        if (unlikely(ubuf_mgr == NULL)) {
            uref_free(uref);
            return uprobe_throw_next(uprobe, upipe, event, args);
        }

        struct uprobe_ubuf_mem_pool_element *new_elem =
            malloc(sizeof(struct uprobe_ubuf_mem_pool_element));
        if (unlikely(new_elem == NULL))
            return urequest_provide_ubuf_mgr(urequest, ubuf_mgr, uref);

        new_elem->ubuf_mgr = ubuf_mgr;
        uatomic_ptr_init(&new_elem->next, NULL);
        if (likely(uatomic_ptr_compare_exchange_ptr(elem_p, &elem, new_elem)))
            return urequest_provide_ubuf_mgr(urequest, ubuf_mgr_use(ubuf_mgr),
                                             uref);

        /* retry */
        ubuf_mgr_release(new_elem->ubuf_mgr);
        uatomic_ptr_clean(&new_elem->next);
        free(new_elem);
    }
}

/** @This initializes an already allocated uprobe_ubuf_mem_pool structure.
 *
 * @param uprobe_ubuf_mem_pool pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @param umem_mgr memory allocator to use for buffers
 * @param ubuf_pool_depth maximum number of ubuf structures in the pool
 * @param shared_pool_depth maximum number of shared structures in the pool
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *
    uprobe_ubuf_mem_pool_init(struct uprobe_ubuf_mem_pool *uprobe_ubuf_mem_pool,
                              struct uprobe *next, struct umem_mgr *umem_mgr,
                              uint16_t ubuf_pool_depth,
                              uint16_t shared_pool_depth)
{
    assert(uprobe_ubuf_mem_pool != NULL);
    struct uprobe *uprobe = uprobe_ubuf_mem_pool_to_uprobe(uprobe_ubuf_mem_pool);
    uprobe_ubuf_mem_pool->umem_mgr = umem_mgr_use(umem_mgr);
    uprobe_ubuf_mem_pool->ubuf_pool_depth = ubuf_pool_depth;
    uprobe_ubuf_mem_pool->shared_pool_depth = shared_pool_depth;
    uatomic_ptr_init(&uprobe_ubuf_mem_pool->first, NULL);
    uprobe_init(uprobe, uprobe_ubuf_mem_pool_throw, next);
    return uprobe;
}

/** @This instructs an existing probe to release all manager currently
 * kept in pools. Please not that this function is not thread-safe, and mustn't
 * be used if the probe may be called from another thread.
 *
 * @param uprobe_ubuf_mem_pool structure to vacuum
 */
void uprobe_ubuf_mem_pool_vacuum(struct uprobe_ubuf_mem_pool *uprobe_ubuf_mem_pool)
{
    struct uprobe_ubuf_mem_pool_element *elem =
        uatomic_ptr_load_ptr(&uprobe_ubuf_mem_pool->first,
                             struct uprobe_ubuf_mem_pool_element *);
    while (unlikely(!uatomic_ptr_compare_exchange_ptr(
                    &uprobe_ubuf_mem_pool->first, &elem, NULL)));

    while (elem != NULL) {
        struct uprobe_ubuf_mem_pool_element *next_elem =
            uatomic_ptr_load_ptr(&elem->next,
                                 struct uprobe_ubuf_mem_pool_element *);
        uatomic_ptr_clean(&elem->next);
        ubuf_mgr_release(elem->ubuf_mgr);
        free(elem);
        elem = next_elem;
    }
}

/** @This cleans a uprobe_ubuf_mem_pool structure.
 *
 * @param uprobe_ubuf_mem_pool structure to clean
 */
void uprobe_ubuf_mem_pool_clean(struct uprobe_ubuf_mem_pool *uprobe_ubuf_mem_pool)
{
    assert(uprobe_ubuf_mem_pool != NULL);
    uprobe_ubuf_mem_pool_vacuum(uprobe_ubuf_mem_pool);
    uatomic_ptr_clean(&uprobe_ubuf_mem_pool->first);
    umem_mgr_release(uprobe_ubuf_mem_pool->umem_mgr);
    struct uprobe *uprobe = uprobe_ubuf_mem_pool_to_uprobe(uprobe_ubuf_mem_pool);
    uprobe_clean(uprobe);
}

#define ARGS_DECL struct uprobe *next, struct umem_mgr *umem_mgr, uint16_t ubuf_pool_depth, uint16_t shared_pool_depth
#define ARGS next, umem_mgr, ubuf_pool_depth, shared_pool_depth
UPROBE_HELPER_ALLOC(uprobe_ubuf_mem_pool)
#undef ARGS
#undef ARGS_DECL

/** @This changes the umem_mgr used by this probe.
 *
 * @param uprobe pointer to probe
 * @param umem_mgr new umem manager to use
 */
void uprobe_ubuf_mem_pool_set(struct uprobe *uprobe, struct umem_mgr *umem_mgr)
{
    struct uprobe_ubuf_mem_pool *uprobe_ubuf_mem_pool =
        uprobe_ubuf_mem_pool_from_uprobe(uprobe);
    umem_mgr_release(uprobe_ubuf_mem_pool->umem_mgr);
    uprobe_ubuf_mem_pool->umem_mgr = umem_mgr_use(umem_mgr);
}
