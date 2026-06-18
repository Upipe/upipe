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

#ifndef _UPIPE_UPROBE_DUP_H_
/** @hidden */
#define _UPIPE_UPROBE_DUP_H_

#include "upipe/uprobe.h"
#include "upipe/uprobe_helper_uprobe.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @This is a super-set of the uprobe structure with additional local
 * members. */
struct uprobe_dup {
    /** pointer to duplicate probe */
    struct uprobe *dup;

    /** event to duplicate */
    int event;

    /** structure exported to modules */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_dup, uprobe)

/** @This initializes an already allocated uprobe_dup structure.
 *
 * @param uprobe_dup pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @param dup second probe to duplicate the event to
 * @param event the event to catch and duplicate
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_dup_init(struct uprobe_dup *uprobe_dup,
                               struct uprobe *next,
                               struct uprobe *dup,
                               int event);

/** @This cleans a uprobe_dup structure.
 *
 * @param uprobe_dup structure to clean
 */
void uprobe_dup_clean(struct uprobe_dup *uprobe_dup);

/** @This allocates a new uprobe_dup structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param dup second probe to duplicate the event to
 * @param event the event to catch and duplicate
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_dup_alloc(struct uprobe *next, struct uprobe *dup, int event);

#ifdef __cplusplus
}
#endif
#endif
