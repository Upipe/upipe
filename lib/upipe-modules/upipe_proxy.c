/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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
 * @short Upipe module - acts as a proxy to another module
 * This is particularly helpful for split pipe, where you would need a proxy
 * as an input pipe, to detect end of streams.
 */

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe-modules/upipe_proxy.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <assert.h>

/** @internal @This is the private context of a proxy pipe manager. */
struct upipe_proxy_mgr {
    /** pointer to the superpipe manager */
    struct upipe_mgr *super_mgr;
    /** function called when the proxy is released */
    upipe_proxy_released proxy_released;

    /** public upipe manager structure */
    struct upipe_mgr mgr;
};

/** @internal @This returns the high-level upipe_mgr structure.
 *
 * @param proxy_mgr pointer to the upipe_proxy_mgr structure
 * @return pointer to the upipe_mgr structure
 */
static inline struct upipe_mgr *
    upipe_proxy_mgr_to_upipe_mgr(struct upipe_proxy_mgr *proxy_mgr)
{
    return &proxy_mgr->mgr;
}

/** @internal @This returns the private upipe_proxy_mgr structure.
 *
 * @param mgr description structure of the upipe manager
 * @return pointer to the upipe_proxy_mgr structure
 */
static inline struct upipe_proxy_mgr *
    upipe_proxy_mgr_from_upipe_mgr(struct upipe_mgr *mgr)
{
    return container_of(mgr, struct upipe_proxy_mgr, mgr);
}

/** @internal @This is the private context of a proxy pipe. */
struct upipe_proxy {
    /** pointer to the superpipe */
    struct upipe *upipe_super;
    /** probe to reroute events */
    struct uprobe uprobe;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_proxy, upipe, UPIPE_PROXY_SIGNATURE)

/** @internal @This catches event from the super pipe to reroute them to us.
 *
 * @param uprobe pointer to probe
 * @param upipe_super pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return true if the event was caught and handled
 */
static bool upipe_proxy_probe(struct uprobe *uprobe, struct upipe *upipe_super,
                              enum uprobe_event event, va_list args)
{
    struct upipe_proxy *upipe_proxy = container_of(uprobe, struct upipe_proxy,
                                                   uprobe);
    struct upipe *upipe = upipe_proxy_to_upipe(upipe_proxy);

    if (event == UPROBE_READY && upipe_proxy->upipe_super == NULL) {
        upipe_proxy->upipe_super = upipe_super;
        upipe->sub_mgr = upipe_super->sub_mgr;
    }
    if (upipe_super != upipe_proxy->upipe_super) {
        uprobe_throw_va(upipe->uprobe, upipe_super, event, args);
        return true;
    }

    upipe_throw_va(upipe, event, args);
    if (event == UPROBE_DEAD && upipe_super == upipe_proxy->upipe_super) {
        upipe_clean(upipe);
        free(upipe_proxy);
    }
    return true;
}

/** @internal @This allocates a proxy input pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_proxy_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    struct upipe_proxy *upipe_proxy = malloc(sizeof(struct upipe_proxy));
    if (unlikely(upipe_proxy == NULL))
        return NULL;
    struct upipe *upipe = upipe_proxy_to_upipe(upipe_proxy);
    upipe_init(upipe, mgr, uprobe);
    uprobe_init(&upipe_proxy->uprobe, upipe_proxy_probe, NULL);
    upipe_proxy->upipe_super = NULL;

    struct upipe_proxy_mgr *proxy_mgr = upipe_proxy_mgr_from_upipe_mgr(mgr);
    struct upipe *upipe_super = upipe_alloc_va(proxy_mgr->super_mgr,
                                               &upipe_proxy->uprobe,
                                               signature, args);
    if (unlikely(upipe_super == NULL)) {
        upipe_clean(upipe);
        free(upipe_proxy);
        return NULL;
    }
    /* Defer initialization to catching ready event */

    return upipe;
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_proxy_input(struct upipe *upipe, struct uref *uref,
                              struct upump *upump)
{
    struct upipe_proxy *upipe_proxy = upipe_proxy_from_upipe(upipe);
    upipe_input(upipe_proxy->upipe_super, uref, upump);
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_proxy_control(struct upipe *upipe,
                                enum upipe_command command, va_list args)
{
    struct upipe_proxy *upipe_proxy = upipe_proxy_from_upipe(upipe);
    return upipe_control_va(upipe_proxy->upipe_super, command, args);
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_proxy_free(struct upipe *upipe)
{
    struct upipe_proxy_mgr *proxy_mgr =
        upipe_proxy_mgr_from_upipe_mgr(upipe->mgr);
    struct upipe_proxy *upipe_proxy = upipe_proxy_from_upipe(upipe);
    proxy_mgr->proxy_released(upipe_proxy->upipe_super);
    upipe_release(upipe_proxy->upipe_super);
    /* Defer deletion to catching dead event */
}

/** @This frees a upipe manager.
 *
 * @param mgr pointer to a upipe manager
 */
static void upipe_proxy_mgr_free(struct upipe_mgr *mgr)
{
    struct upipe_proxy_mgr *proxy_mgr = upipe_proxy_mgr_from_upipe_mgr(mgr);
    struct upipe_mgr *super_mgr = proxy_mgr->super_mgr;
    urefcount_clean(&proxy_mgr->mgr.refcount);
    free(proxy_mgr);

    upipe_mgr_release(super_mgr);
}

/** @This returns the management structure for proxy pipes. Please note that
 * the refcount for super_mgr is not incremented, so super_mgr belongs to the
 * callee.
 *
 * @param super_mgr management structures for pipes we're proxying for
 * @param proxy_released function called when a proxy pipe is released
 * @return pointer to manager
 */
struct upipe_mgr *upipe_proxy_mgr_alloc(struct upipe_mgr *super_mgr,
                                        upipe_proxy_released proxy_released)
{
    assert(super_mgr != NULL);
    assert(proxy_released != NULL);
    struct upipe_proxy_mgr *proxy_mgr = malloc(sizeof(struct upipe_proxy_mgr));
    if (unlikely(proxy_mgr == NULL)) {
        upipe_mgr_release(super_mgr);
        return NULL;
    }

    proxy_mgr->super_mgr = super_mgr;
    proxy_mgr->proxy_released = proxy_released;

    struct upipe_mgr *mgr = upipe_proxy_mgr_to_upipe_mgr(proxy_mgr);
    urefcount_init(&mgr->refcount);
    mgr->signature = UPIPE_PROXY_SIGNATURE;
    mgr->upipe_alloc = upipe_proxy_alloc;
    mgr->upipe_input = upipe_proxy_input;
    mgr->upipe_control = upipe_proxy_control;
    mgr->upipe_free = upipe_proxy_free;
    mgr->upipe_mgr_free = upipe_proxy_mgr_free;
    return mgr;
}

/** @This returns the superpipe manager.
 *
 * @param mgr proxy_mgr structure
 * @return pointer to superpipe manager
 */
struct upipe_mgr *upipe_proxy_mgr_get_super_mgr(struct upipe_mgr *mgr)
{
    struct upipe_proxy_mgr *proxy_mgr = upipe_proxy_mgr_from_upipe_mgr(mgr);
    return proxy_mgr->super_mgr;
}
