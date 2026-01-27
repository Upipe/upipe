/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

#include "upipe/urefcount_helper.h"

#include "upipe/uprobe.h"

/** @internal @This is the private structure for a simple allocated probe. */
struct uprobe_alloc {
    /** refcount structure */
    struct urefcount urefcount;
    /** probe structure */
    struct uprobe uprobe;
};

/** @hidden */
UREFCOUNT_HELPER(uprobe_alloc, urefcount, uprobe_alloc_free);

/** @internal @This frees the allocated probe.
 *
 * @param uprobe_alloc private allocated structure
 */
static inline void uprobe_alloc_free(struct uprobe_alloc *uprobe_alloc)
{
    uprobe_clean(&uprobe_alloc->uprobe);
    uprobe_alloc_clean_urefcount(uprobe_alloc);
    free(uprobe_alloc);
}

/** @This allocates and initializes a probe.
 *
 * @param func function called when an event is raised
 * @param next next probe to test if this one doesn't catch the event
 * @return an allocated probe
 */
struct uprobe *uprobe_alloc(uprobe_throw_func func, struct uprobe *next)
{
    struct uprobe_alloc *uprobe_alloc = malloc(sizeof (*uprobe_alloc));
    if (unlikely(!uprobe_alloc)) {
        uprobe_release(next);
        return NULL;
    }
    uprobe_init(&uprobe_alloc->uprobe, func, next);
    uprobe_alloc_init_urefcount(uprobe_alloc);
    uprobe_alloc->uprobe.refcount = &uprobe_alloc->urefcount;
    return &uprobe_alloc->uprobe;
}
