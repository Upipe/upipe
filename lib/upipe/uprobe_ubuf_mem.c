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
 * @short probe catching provide_request events asking for a ubuf manager
 */

#include <upipe/ubase.h>
#include <upipe/umem.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_mem.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/uprobe_helper_alloc.h>
#include <upipe/upipe.h>

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/** @internal @This catches events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return an error code
 */
static int uprobe_ubuf_mem_throw(struct uprobe *uprobe, struct upipe *upipe,
                                 int event, va_list args)
{
    struct uprobe_ubuf_mem *uprobe_ubuf_mem =
        uprobe_ubuf_mem_from_uprobe(uprobe);

    if (event != UPROBE_PROVIDE_REQUEST || uprobe_ubuf_mem->umem_mgr == NULL)
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

    struct ubuf_mgr *ubuf_mgr =
        ubuf_mem_mgr_alloc_from_flow_def(uprobe_ubuf_mem->ubuf_pool_depth,
                                         uprobe_ubuf_mem->shared_pool_depth,
                                         uprobe_ubuf_mem->umem_mgr, uref);
    if (ubuf_mgr == NULL) {
        uref_free(uref);
        return uprobe_throw_next(uprobe, upipe, event, args);
    }

    return urequest_provide_ubuf_mgr(urequest, ubuf_mgr, uref);
}

/** @This initializes an already allocated uprobe_ubuf_mem structure.
 *
 * @param uprobe_ubuf_mem pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @param umem_mgr memory allocator to use for buffers
 * @param ubuf_pool_depth maximum number of ubuf structures in the pool
 * @param shared_pool_depth maximum number of shared structures in the pool
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_ubuf_mem_init(struct uprobe_ubuf_mem *uprobe_ubuf_mem,
                                    struct uprobe *next,
                                    struct umem_mgr *umem_mgr,
                                    uint16_t ubuf_pool_depth,
                                    uint16_t shared_pool_depth)
{
    assert(uprobe_ubuf_mem != NULL);
    struct uprobe *uprobe = uprobe_ubuf_mem_to_uprobe(uprobe_ubuf_mem);
    uprobe_ubuf_mem->umem_mgr = umem_mgr_use(umem_mgr);
    uprobe_ubuf_mem->ubuf_pool_depth = ubuf_pool_depth;
    uprobe_ubuf_mem->shared_pool_depth = shared_pool_depth;
    uprobe_init(uprobe, uprobe_ubuf_mem_throw, next);
    return uprobe;
}

/** @This cleans a uprobe_ubuf_mem structure.
 *
 * @param uprobe_ubuf_mem structure to clean
 */
void uprobe_ubuf_mem_clean(struct uprobe_ubuf_mem *uprobe_ubuf_mem)
{
    assert(uprobe_ubuf_mem != NULL);
    struct uprobe *uprobe = uprobe_ubuf_mem_to_uprobe(uprobe_ubuf_mem);
    umem_mgr_release(uprobe_ubuf_mem->umem_mgr);
    uprobe_clean(uprobe);
}

#define ARGS_DECL struct uprobe *next, struct umem_mgr *umem_mgr, uint16_t ubuf_pool_depth, uint16_t shared_pool_depth
#define ARGS next, umem_mgr, ubuf_pool_depth, shared_pool_depth
UPROBE_HELPER_ALLOC(uprobe_ubuf_mem)
#undef ARGS
#undef ARGS_DECL

/** @This changes the umem_mgr used by this probe.
 *
 * @param uprobe pointer to probe
 * @param umem_mgr new umem manager to use
 */
void uprobe_ubuf_mem_set(struct uprobe *uprobe, struct umem_mgr *umem_mgr)
{
    struct uprobe_ubuf_mem *uprobe_ubuf_mem =
        uprobe_ubuf_mem_from_uprobe(uprobe);
    umem_mgr_release(uprobe_ubuf_mem->umem_mgr);
    uprobe_ubuf_mem->umem_mgr = umem_mgr_use(umem_mgr);
}
