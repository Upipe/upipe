/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short probe providing source pipe manager by catching need source pipe
 * manager events
 */

#ifndef _UPIPE_UPROBE_SOURCE_MGR_H_
/** @hidden */
# define _UPIPE_UPROBE_SOURCE_MGR_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uprobe_helper_uprobe.h"
#include "upipe/uprobe.h"

/** @hidden */
struct upipe_mgr;

/** @This defines a source manager probe. */
struct uprobe_source_mgr {
    /** source manager to provide */
    struct upipe_mgr *source_mgr;
    /** public probe structure */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_source_mgr, uprobe);

/** @This initializes an already allocated uprobe_source_mgr structure.
 *
 * @param uprobe_source_mgr pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @param source_mgr source pipe manager to provide to pipes
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *
uprobe_source_mgr_init(struct uprobe_source_mgr *uprobe_source_mgr,
                       struct uprobe *next,
                       struct upipe_mgr *source_mgr);

/** @This cleans a uprobe_source_mgr structure.
 *
 * @param uprobe_source_mgr structure to clean
 */
void uprobe_source_mgr_clean(struct uprobe_source_mgr *uprobe_source_mgr);

/** @This allocates and initializes a new uprobe_source_mgr structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param source_mgr source pipe manager to provide to pipes
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *
uprobe_source_mgr_alloc(struct uprobe *next,
                        struct upipe_mgr *source_mgr);

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_UPROBE_SOURCE_MGR_H_ */
