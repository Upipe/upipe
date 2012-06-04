/*****************************************************************************
 * upump_common.h: common declarations for event loop handlers
 *****************************************************************************
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
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
 *****************************************************************************/

#ifndef _UPIPE_UPUMP_COMMON_H_
/** @hidden */
#define _UPIPE_UPUMP_COMMON_H_

#include <stdbool.h>
#include <stdarg.h>

#include <upipe/upump.h>

/** @This stores management parameters invisible from modules but usually
 * common.
 */
struct upump_common_mgr {
    /** source watchers internally registered by upipe */
    struct upump **source_watchers;
    /** number of source watchers */
    unsigned int nb_source_watchers;

    /** structure exported to modules */
    struct upump_mgr mgr;
};

/** @This returns the high-level upump_mgr structure.
 *
 * @param common_mgr pointer to the upump_common_mgr structure
 * @return pointer to the upump_mgr structure
 */
static inline struct upump_mgr *upump_common_mgr_to_upump_mgr(struct upump_common_mgr *common_mgr)
{
    return &common_mgr->mgr;
}

/** @This returns the private upump_common_mgr structure.
 *
 * @param mgr pointer to the upump_mgr structure
 * @return pointer to the upump_common_mgr structure
 */
static inline struct upump_common_mgr *upump_common_mgr_from_upump_mgr(struct upump_mgr *mgr)
{
    return container_of(mgr, struct upump_common_mgr, mgr);
}

/** @This adds a source watcher to the list of watchers.
 *
 * @param upump description structure of the watcher
 * @return false in case of failure
 */
bool upump_common_start_source(struct upump *upump);

/** @This removes a source watcher from the list of watchers.
 *
 * @param upump description structure of the watcher
 * @return false in case of failure
 */
bool upump_common_stop_source(struct upump *upump);

/** @This dispatches an event to a watcher.
 *
 * @param upump description structure of the watcher
 */
void upump_common_dispatch(struct upump *upump);

/** @This initializes the common parts of a upump_common_mgr structure.
 *
 * @param mgr pointer to a upump_mgr structure wrapped into a
 * upump_common_mgr structure
 */
void upump_common_mgr_init(struct upump_mgr *mgr);

/** @This cleans up the common parts of a upump_common_mgr structure.
 * Note that all watchers have to be stopped before.
 *
 * @param mgr pointer to a upump_mgr structure wrapped into a
 * upump_common_mgr structure
 */
void upump_common_mgr_clean(struct upump_mgr *mgr);

#endif
