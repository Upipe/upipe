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
 * @short probe transferring events from one thread to another
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_transfer.h>
#include <upipe/uprobe_helper_alloc.h>
#include <upipe/upipe.h>

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/** @This represents an event type that has to be transferred. */
struct uprobe_xfer_sub {
    /** structure for chained list */
    struct uchain uchain;

    /** transferred type of event */
    enum uprobe_xfer_event xfer_event;
    /** real type of event */
    int event;
    /** signature (for local events) */
    uint32_t signature;
};

UBASE_FROM_TO(uprobe_xfer_sub, uchain, uchain, uchain)

/** @internal @This catches events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return an error code
 */
static int uprobe_xfer_throw(struct uprobe *uprobe, struct upipe *upipe,
                             int event, va_list args)
{
    if (upipe == NULL)
        return uprobe_throw_next(uprobe, upipe, event, args);

    struct uprobe_xfer *uprobe_xfer = uprobe_xfer_from_uprobe(uprobe);
    struct uchain *uchain;
    struct uprobe_xfer_sub *found = NULL;
    ulist_foreach (&uprobe_xfer->subs, uchain) {
        struct uprobe_xfer_sub *sub = uprobe_xfer_sub_from_uchain(uchain);
        if (event == sub->event) {
            found = sub;
            break;
        }
    }

    if (found == NULL)
        return uprobe_throw_next(uprobe, upipe, event, args);

    if (found->signature) {
        va_list args_copy;
        va_copy(args_copy, args);
        uint32_t signature = va_arg(args, uint32_t);
        va_end(args_copy);
        if (found->signature != signature)
            return uprobe_throw_next(uprobe, upipe, event, args);
        va_arg(args, uint32_t);
    }

    switch (found->xfer_event) {
        case UPROBE_XFER_VOID:
            return upipe_throw(upipe, found->xfer_event, UPROBE_XFER_SIGNATURE,
                               event);
        case UPROBE_XFER_UINT64_T: {
            uint64_t arg = va_arg(args, uint64_t);
            return upipe_throw(upipe, found->xfer_event, UPROBE_XFER_SIGNATURE,
                               event, arg);
        }
        case UPROBE_XFER_UNSIGNED_LONG_LOCAL: {
            unsigned long arg = va_arg(args, unsigned long);
            return upipe_throw(upipe, found->xfer_event, UPROBE_XFER_SIGNATURE,
                               event, found->signature, arg);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This initializes an already allocated uprobe_xfer structure.
 *
 * @param uprobe_xfer pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_xfer_init(struct uprobe_xfer *uprobe_xfer,
                                struct uprobe *next)
{
    assert(uprobe_xfer != NULL);
    struct uprobe *uprobe = uprobe_xfer_to_uprobe(uprobe_xfer);
    uprobe_init(uprobe, uprobe_xfer_throw, next);
    ulist_init(&uprobe_xfer->subs);
    return uprobe;
}

/** @This cleans a uprobe_xfer structure.
 *
 * @param uprobe_xfer structure to clean
 */
void uprobe_xfer_clean(struct uprobe_xfer *uprobe_xfer)
{
    assert(uprobe_xfer != NULL);
    struct uprobe *uprobe = uprobe_xfer_to_uprobe(uprobe_xfer);
    struct uchain *uchain, *tmp;
    ulist_delete_foreach (&uprobe_xfer->subs, uchain, tmp) {
        struct uprobe_xfer_sub *sub = uprobe_xfer_sub_from_uchain(uchain);
        free(sub);
    }
    uprobe_clean(uprobe);
}

#define ARGS_DECL struct uprobe *next
#define ARGS next
UPROBE_HELPER_ALLOC(uprobe_xfer)
#undef ARGS
#undef ARGS_DECL

/** @This allocates a sub-structure to transfer an event.
 *
 * @param uprobe pointer to probe
 * @param xfer_event type of the event
 * @param event event to be transferred
 * @param signature event signature, or 0 for standard events
 * @return an error code
 */
int uprobe_xfer_add(struct uprobe *uprobe, enum uprobe_xfer_event xfer_event,
                    int event, uint32_t signature)
{
    struct uprobe_xfer *uprobe_xfer = uprobe_xfer_from_uprobe(uprobe);
    struct uprobe_xfer_sub *sub = malloc(sizeof(struct uprobe_xfer_sub));
    if (unlikely(sub == NULL))
        return UBASE_ERR_ALLOC;

    sub->xfer_event = xfer_event;
    sub->event = event;
    sub->signature = signature;
    ulist_add(&uprobe_xfer->subs, uprobe_xfer_sub_to_uchain(sub));
    return UBASE_ERR_NONE;
}

