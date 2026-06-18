/*
 * Copyright (C) 2026 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short probe catching events and duplicating them to another probe
 */

#include "upipe/uprobe.h"
#include "upipe/uprobe_dup.h"
#include "upipe/uprobe_helper_alloc.h"

/** @internal @This catches events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return an error code
 */
static int uprobe_dup_throw(struct uprobe *uprobe, struct upipe *upipe,
                            int event, va_list args)
{
    struct uprobe_dup *uprobe_dup =
        uprobe_dup_from_uprobe(uprobe);

    if (event == uprobe_dup->event) {
        va_list args_copy;
        va_copy(args_copy, args);
        uprobe_throw_va(uprobe_dup->dup, upipe, event, args_copy);
        va_end(args_copy);
    }

    return uprobe_throw_next(uprobe, upipe, event, args);
}

/** @This initializes an already allocated uprobe_dup structure.
 *
 * @param uprobe_dup pointer to the already allocated structure
 * @param next next probe to test
 * @param dup second probe to duplicate the event to
 * @param event the event to catch and duplicate
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_dup_init(struct uprobe_dup *uprobe_dup,
                               struct uprobe *next,
                               struct uprobe *dup,
                               int event)
{
    assert(uprobe_dup != NULL);
    struct uprobe *uprobe = uprobe_dup_to_uprobe(uprobe_dup);
    uprobe_dup->dup = uprobe_use(dup);
    uprobe_dup->event = event;
    uprobe_init(uprobe, uprobe_dup_throw, next);
    return uprobe;
}

/** @This cleans a uprobe_dup structure.
 *
 * @param uprobe_dup structure to clean
 */
void uprobe_dup_clean(struct uprobe_dup *uprobe_dup)
{
    assert(uprobe_dup != NULL);
    struct uprobe *uprobe = uprobe_dup_to_uprobe(uprobe_dup);
    uprobe_release(uprobe_dup->dup);
    uprobe_clean(uprobe);
}

#define ARGS_DECL struct uprobe *next, struct uprobe *dup, int event
#define ARGS next, dup, event
UPROBE_HELPER_ALLOC(uprobe_dup)
#undef ARGS
#undef ARGS_DECL
