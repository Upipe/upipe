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
 * @short probe catching need_uref_mgr events and providing a given uref manager
 */

#include <upipe/ubase.h>
#include <upipe/uref.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/upipe.h>
#include <upipe/ulog.h>

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/** @This is a super-set of the uprobe structure with additional local
 * members. */
struct uprobe_uref_mgr {
    /** pointer to uref_mgr to provide */
    struct uref_mgr *uref_mgr;

    /** structure exported to modules */
    struct uprobe uprobe;
};

/** @internal @This returns the high-level uprobe structure.
 *
 * @param uprobe_uref_mgr pointer to the uprobe_uref_mgr structure
 * @return pointer to the uprobe structure
 */
static inline struct uprobe *
    uprobe_uref_mgr_to_uprobe(struct uprobe_uref_mgr *uprobe_uref_mgr)
{
    return &uprobe_uref_mgr->uprobe;
}

/** @internal @This returns the private uprobe_uref_mgr structure.
 *
 * @param mgr description structure of the uprobe
 * @return pointer to the uprobe_uref_mgr structure
 */
static inline struct uprobe_uref_mgr *
    uprobe_uref_mgr_from_uprobe(struct uprobe *uprobe)
{
    return container_of(uprobe, struct uprobe_uref_mgr, uprobe);
}

/** @internal @This catches events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return true if the event was caught and handled
 */
static bool uprobe_uref_mgr_throw(struct uprobe *uprobe, struct upipe *upipe,
                                  enum uprobe_event event, va_list args)
{
    struct uprobe_uref_mgr *uprobe_uref_mgr =
        uprobe_uref_mgr_from_uprobe(uprobe);

    if (event == UPROBE_NEED_UREF_MGR && uprobe_uref_mgr->uref_mgr != NULL) {
        if (unlikely(!upipe_set_uref_mgr(upipe, uprobe_uref_mgr->uref_mgr))) {
            ulog_warning(upipe->ulog, "probe couldn't set uref manager");
            return false;
        }
        return true;
    }

    return false;
}

/** @This frees a uprobe_uref_mgr structure.
 *
 * @param uprobe structure to free
 */
void uprobe_uref_mgr_free(struct uprobe *uprobe)
{
    struct uprobe_uref_mgr *uprobe_uref_mgr =
        uprobe_uref_mgr_from_uprobe(uprobe);
    uref_mgr_release(uprobe_uref_mgr->uref_mgr);
    free(uprobe_uref_mgr);
}

/** @This allocates a new uprobe_uref_mgr structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param uref_mgr uref manager to provide to pipes
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_uref_mgr_alloc(struct uprobe *next,
                                     struct uref_mgr *uref_mgr)
{
    struct uprobe_uref_mgr *uprobe_uref_mgr =
        malloc(sizeof(struct uprobe_uref_mgr));
    if (unlikely(uprobe_uref_mgr == NULL))
        return NULL;
    struct uprobe *uprobe = uprobe_uref_mgr_to_uprobe(uprobe_uref_mgr);
    uprobe_uref_mgr->uref_mgr = uref_mgr;
    if (uref_mgr != NULL)
        uref_mgr_use(uref_mgr);
    uprobe_init(uprobe, uprobe_uref_mgr_throw, next);
    return uprobe;
}

/** @This changes the uref_mgr set by this probe.
 *
 * @param uprobe pointer to probe
 * @param uref_mgr new uref manager to provide to pipes
 */
void uprobe_uref_mgr_set(struct uprobe *uprobe, struct uref_mgr *uref_mgr)
{
    struct uprobe_uref_mgr *uprobe_uref_mgr =
        uprobe_uref_mgr_from_uprobe(uprobe);
    if (uprobe_uref_mgr->uref_mgr != NULL)
        uref_mgr_release(uprobe_uref_mgr->uref_mgr);
    uprobe_uref_mgr->uref_mgr = uref_mgr;
    if (uref_mgr != NULL)
        uref_mgr_use(uref_mgr);
}
