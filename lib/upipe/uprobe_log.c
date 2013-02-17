/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
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
 * @short simple probe logging all received events, as a fall-back
 */

#include <upipe/ubase.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_helper_uprobe.h>
#include <upipe/uprobe_log.h>
#include <upipe/upipe.h>

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>

/** @This is a super-set of the uprobe structure with additional local members.
 */
struct uprobe_log {
    /** level at which to log the messages */
    enum uprobe_log_level level;

    /** structure exported to modules */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_log, uprobe)

/** @internal @This catches events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return always false
 */
static bool uprobe_log_throw(struct uprobe *uprobe, struct upipe *upipe,
                               enum uprobe_event event, va_list args)
{
    struct uprobe_log *log = uprobe_log_from_uprobe(uprobe);
    if (upipe == NULL)
        return false;

    va_list args_copy;
    va_copy(args_copy, args);

    switch (event) {
        case UPROBE_READY:
            upipe_log(upipe, log->level, "probe caught ready event");
            break;
        case UPROBE_DEAD:
            upipe_log(upipe, log->level, "probe caught dead event");
            break;
        case UPROBE_LOG:
            break;
        case UPROBE_AERROR:
            upipe_log(upipe, log->level,
                      "probe caught allocation error");
            break;
        case UPROBE_FLOW_DEF_ERROR:
            upipe_log(upipe, log->level, "probe caught flow def error");
            break;
        case UPROBE_UPUMP_ERROR:
            upipe_log(upipe, log->level, "probe caught upump error");
            break;
        case UPROBE_READ_END: {
            const char *location = va_arg(args_copy, const char *);
            if (location != NULL)
                upipe_log_va(upipe, log->level,
                             "probe caught read end on %s", location);
            else
                upipe_log(upipe, log->level, "probe caught read end");
            break;
        }
        case UPROBE_WRITE_END: {
            const char *location = va_arg(args_copy, const char *);
            if (location != NULL)
                upipe_log_va(upipe, log->level,
                             "probe caught write end on %s", location);
            else
                upipe_log(upipe, log->level, "probe caught write end");
            break;
        }
        case UPROBE_NEED_UREF_MGR:
            upipe_log(upipe, log->level,
                      "probe caught need uref manager");
            break;
        case UPROBE_NEED_UPUMP_MGR:
            upipe_log(upipe, log->level,
                      "probe caught need upump manager");
            break;
        case UPROBE_NEED_UBUF_MGR:
            upipe_log(upipe, log->level,
                      "probe caught need ubuf manager");
            break;
        case UPROBE_NEED_OUTPUT: {
            struct uref *flow_def = va_arg(args_copy, struct uref *);
            const char *def = "[invalid]";
            uref_flow_get_def(flow_def, &def);
            upipe_log_va(upipe, log->level,
                         "probe caught need output for flow def \"%s\"", def);
            break;
        }
        case UPROBE_SPLIT_ADD_FLOW: {
            uint64_t flow_id = va_arg(args_copy, uint64_t);
            struct uref *flow_def = va_arg(args_copy, struct uref *);
            const char *def = "[invalid]";
            uref_flow_get_def(flow_def, &def);
            upipe_log_va(upipe, log->level,
                         "probe caught add flow 0x%"PRIx64" def \"%s\"",
                         flow_id, def);
            break;
        }
        case UPROBE_SPLIT_DEL_FLOW: {
            uint64_t flow_id = va_arg(args_copy, uint64_t);
            upipe_log_va(upipe, log->level,
                         "probe caught del flow 0x%"PRIx64"", flow_id);
            break;
        }
        case UPROBE_SYNC_ACQUIRED:
            upipe_log(upipe, log->level, "probe caught sync acquired");
            break;
        case UPROBE_SYNC_LOST:
            upipe_log(upipe, log->level, "probe caught sync lost");
            break;
        case UPROBE_CLOCK_REF: {
            struct uref *uref = va_arg(args_copy, struct uref *);
            uint64_t pcr = va_arg(args_copy, uint64_t);
            upipe_log_va(upipe, log->level,
                         "probe caught new clock ref %"PRIu64, pcr);
            break;
        }
        case UPROBE_CLOCK_TS: {
            struct uref *uref = va_arg(args_copy, struct uref *);
            uint64_t pts = UINT64_MAX, dts = UINT64_MAX;
            uref_clock_get_pts_orig(uref, &pts);
            uref_clock_get_dts_orig(uref, &dts);
            if (pts == UINT64_MAX && dts == UINT64_MAX)
                upipe_log(upipe, log->level,
                          "probe caught an invalid timestamp event");
            else if (pts == UINT64_MAX)
                upipe_log_va(upipe, log->level, "probe caught new DTS %"PRIu64,
                             dts);
            else if (dts == UINT64_MAX)
                upipe_log_va(upipe, log->level, "probe caught new PTS %"PRIu64,
                             pts);
            else
                upipe_log_va(upipe, log->level,
                             "probe caught new PTS %"PRIu64" and DTS %"PRIu64,
                             pts, dts);
            break;
        }
        default:
            upipe_log_va(upipe, log->level,
                     "probe caught an unknown, uncaught event (0x%x)", event);
            break;
    }
    va_end(args_copy);
    return false;
}

/** @This allocates a new uprobe log structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param level level at which to log the messages
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_log_alloc(struct uprobe *next,
                                enum uprobe_log_level level)
{
    struct uprobe_log *log = malloc(sizeof(struct uprobe_log));
    if (unlikely(log == NULL))
        return NULL;
    struct uprobe *uprobe = uprobe_log_to_uprobe(log);
    log->level = level;
    uprobe_init(uprobe, uprobe_log_throw, next);
    return uprobe;
}

/** @This frees a uprobe log structure.
 *
 * @param uprobe structure to free
 */
void uprobe_log_free(struct uprobe *uprobe)
{
    struct uprobe_log *log = uprobe_log_from_uprobe(uprobe);
    free(log);
}
