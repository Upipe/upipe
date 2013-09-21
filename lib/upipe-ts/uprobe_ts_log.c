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
 * @short simple probe logging all received events from ts pipes
 */

#include <upipe/ubase.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_helper_uprobe.h>
#include <upipe-ts/uprobe_ts_log.h>
#include <upipe/upipe.h>

#include <upipe-ts/upipe_ts_demux.h>
#include <upipe-ts/upipe_ts_split.h>
#include <upipe-ts/upipe_ts_pat_decoder.h>
#include <upipe-ts/upipe_ts_pmt_decoder.h>

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>

/** @This is a super-set of the uprobe structure with additional local members.
 */
struct uprobe_ts_log {
    /** level at which to log the messages */
    enum uprobe_log_level level;

    /** structure exported to modules */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_ts_log, uprobe)

/** @internal @This catches events thrown by ts pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return always false
 */
static bool uprobe_ts_log_throw(struct uprobe *uprobe, struct upipe *upipe,
                                enum uprobe_event event, va_list args)
{
    struct uprobe_ts_log *uprobe_ts_log =
        uprobe_ts_log_from_uprobe(uprobe);
    if (upipe == NULL || event <= UPROBE_LOCAL)
        return false;

    va_list args_copy;
    va_copy(args_copy, args);
    unsigned int signature = va_arg(args_copy, unsigned int);

    switch (event) {
        case UPROBE_TS_SPLIT_ADD_PID: {
            assert(signature == UPIPE_TS_SPLIT_SIGNATURE);
            unsigned int pid = va_arg(args_copy, unsigned int);
            upipe_log_va(upipe, uprobe_ts_log->level,
                         "ts probe caught add PID %u", pid);
            break;
        }
        case UPROBE_TS_SPLIT_DEL_PID: {
            assert(signature == UPIPE_TS_SPLIT_SIGNATURE);
            unsigned int pid = va_arg(args_copy, unsigned int);
            upipe_log_va(upipe, uprobe_ts_log->level,
                         "ts probe caught delete PID %u", pid);
            break;
        }

        default:
            upipe_log_va(upipe, uprobe_ts_log->level,
                         "ts probe caught an unknown, uncaught event (0x%x)",
                         event);
            break;
    }
    va_end(args_copy);
    return false;
}

/** @This allocates a new uprobe ts log structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param level level at which to log the messages
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_ts_log_alloc(struct uprobe *next, enum uprobe_log_level level)
{
    struct uprobe_ts_log *uprobe_ts_log = malloc(sizeof(struct uprobe_ts_log));
    if (unlikely(uprobe_ts_log == NULL))
        return NULL;
    struct uprobe *uprobe = uprobe_ts_log_to_uprobe(uprobe_ts_log);
    uprobe_ts_log->level = level;
    uprobe_init(uprobe, uprobe_ts_log_throw, next);
    return uprobe;
}

/** @This frees a uprobe ts log structure.
 *
 * @param uprobe structure to free
 */
void uprobe_ts_log_free(struct uprobe *uprobe)
{
    struct uprobe_ts_log *uprobe_ts_log = uprobe_ts_log_from_uprobe(uprobe);
    free(uprobe_ts_log);
}
