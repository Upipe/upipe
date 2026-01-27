/*
 * Copyright (C) 2013-2014 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short probe catching need_upump_mgr events and providing a given upump manager
 */

#include "upipe/ubase.h"
#include "upipe/upump.h"
#include "upipe/uprobe.h"
#include "upipe/uprobe_upump_mgr.h"
#include "upipe/uprobe_helper_alloc.h"

#include <stdlib.h>
#include <stdarg.h>

/** @internal @This catches events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return an error code
 */
static int uprobe_upump_mgr_throw(struct uprobe *uprobe, struct upipe *upipe,
                                  int event, va_list args)
{
    struct uprobe_upump_mgr *uprobe_upump_mgr =
        uprobe_upump_mgr_from_uprobe(uprobe);
    switch (event) {
        default:
            return uprobe_throw_next(uprobe, upipe, event, args);

        case UPROBE_FREEZE_UPUMP_MGR:
        case UPROBE_THAW_UPUMP_MGR:
            uprobe_upump_mgr->frozen = event == UPROBE_FREEZE_UPUMP_MGR;
            return UBASE_ERR_NONE;

        case UPROBE_NEED_UPUMP_MGR:
            break;
    }

    if (uprobe_upump_mgr->frozen || uprobe_upump_mgr->upump_mgr == NULL)
        return uprobe_throw_next(uprobe, upipe, event, args);

    struct upump_mgr **upump_mgr_p = va_arg(args, struct upump_mgr **);
    *upump_mgr_p = upump_mgr_use(uprobe_upump_mgr->upump_mgr);
    return UBASE_ERR_NONE;
}

/** @This initializes an already allocated uprobe_upump_mgr structure.
 *
 * @param uprobe_upump_mgr pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @param upump_mgr upump manager to provide to pipes
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_upump_mgr_init(struct uprobe_upump_mgr *uprobe_upump_mgr,
                                     struct uprobe *next,
                                     struct upump_mgr *upump_mgr)
{
    assert(uprobe_upump_mgr != NULL);
    struct uprobe *uprobe = uprobe_upump_mgr_to_uprobe(uprobe_upump_mgr);
    uprobe_upump_mgr->upump_mgr = upump_mgr_use(upump_mgr);
    uprobe_upump_mgr->frozen = false;
    uprobe_init(uprobe, uprobe_upump_mgr_throw, next);
    return uprobe;
}

/** @This cleans a uprobe_upump_mgr structure.
 *
 * @param uprobe_upump_mgr structure to clean
 */
void uprobe_upump_mgr_clean(struct uprobe_upump_mgr *uprobe_upump_mgr)
{
    assert(uprobe_upump_mgr != NULL);
    struct uprobe *uprobe = uprobe_upump_mgr_to_uprobe(uprobe_upump_mgr);
    upump_mgr_release(uprobe_upump_mgr->upump_mgr);
    uprobe_clean(uprobe);
}

#define ARGS_DECL struct uprobe *next, struct upump_mgr *upump_mgr
#define ARGS next, upump_mgr
UPROBE_HELPER_ALLOC(uprobe_upump_mgr)
#undef ARGS
#undef ARGS_DECL

/** @This changes the upump_mgr set by this probe.
 *
 * @param uprobe pointer to probe
 * @param upump_mgr new upump manager to provide to pipes
 */
void uprobe_upump_mgr_set(struct uprobe *uprobe, struct upump_mgr *upump_mgr)
{
    struct uprobe_upump_mgr *uprobe_upump_mgr =
        uprobe_upump_mgr_from_uprobe(uprobe);
    upump_mgr_release(uprobe_upump_mgr->upump_mgr);
    uprobe_upump_mgr->upump_mgr = upump_mgr_use(upump_mgr);
}
