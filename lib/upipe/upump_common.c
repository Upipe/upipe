/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
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
 * @short common functions for event loop handlers
 */

#include <upipe/ubase.h>
#include <upipe/upump_common.h>

#include <stdlib.h>
#include <string.h>

/** @This dispatches an event to a watcher.
 *
 * @param upump description structure of the watcher
 */
void upump_common_dispatch(struct upump *upump)
{
    struct upump_common_mgr *common_mgr =
        upump_common_mgr_from_upump_mgr(upump->mgr);
    upump->cb(upump);

    for (unsigned int i = 0; i < common_mgr->nb_source_watchers; i++)
        if (likely(!common_mgr->mgr.nb_blocked_sinks))
            upump_start_source(common_mgr->source_watchers[i]);
        else
            upump_stop_source(common_mgr->source_watchers[i]);
}

/** @This adds a source watcher to the list of watchers.
 *
 * @param upump description structure of the watcher
 * @return false in case of failure
 */
bool upump_common_start_source(struct upump *upump)
{
    struct upump_common_mgr *common_mgr =
        upump_common_mgr_from_upump_mgr(upump->mgr);
    for (unsigned int i = 0; i < common_mgr->nb_source_watchers; i++)
        if (unlikely(common_mgr->source_watchers[i] == upump))
            return true;

    struct upump **source_watchers = realloc(common_mgr->source_watchers,
            (common_mgr->nb_source_watchers + 1) * sizeof(struct upump *));
    if (unlikely(source_watchers == NULL)) return false;
    source_watchers[common_mgr->nb_source_watchers++] = upump;
    common_mgr->source_watchers = source_watchers;
    if (likely(!common_mgr->mgr.nb_blocked_sinks))
        upump_start_source(upump);
    return true;
}

/** @This removes a source watcher from the list of watchers.
 *
 * @param upump description structure of the watcher
 * @return false in case of failure
 */
bool upump_common_stop_source(struct upump *upump)
{
    upump_stop_source(upump);

    struct upump_common_mgr *common_mgr =
        upump_common_mgr_from_upump_mgr(upump->mgr);
    for (unsigned int i = 0; i < common_mgr->nb_source_watchers; i++)
        if (likely(common_mgr->source_watchers[i] == upump)) {
            memmove(&common_mgr->source_watchers[i],
                    &common_mgr->source_watchers[i + 1],
                    (--common_mgr->nb_source_watchers - i)
                      * sizeof(struct upump *));
            return true;
        }

    return false;
}

/** @This initializes the common parts of a upump_common_mgr structure.
 *
 * @param mgr pointer to a upump_mgr structure wrapped into a
 * upump_common_mgr structure
 */
void upump_common_mgr_init(struct upump_mgr *mgr)
{
    struct upump_common_mgr *common_mgr = upump_common_mgr_from_upump_mgr(mgr);
    common_mgr->source_watchers = NULL;
    common_mgr->nb_source_watchers = 0;

    common_mgr->mgr.nb_blocked_sinks = 0;
}

/** @This cleans up the common parts of a upump_common_mgr structure.
 * Note that all watchers have to be stopped before.
 *
 * @param mgr pointer to a upump_mgr structure wrapped into a
 * upump_common_mgr structure
 */
void upump_common_mgr_clean(struct upump_mgr *mgr)
{
    struct upump_common_mgr *common_mgr = upump_common_mgr_from_upump_mgr(mgr);
    free(common_mgr->source_watchers);
}
