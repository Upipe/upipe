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

/** @internal @This converts an error code into a description string.
 *
 * @param errcode error code
 * @return a description string
 */
static const char *uprobe_log_errcode(enum uprobe_error_code errcode)
{
    switch (errcode) {
        case UPROBE_ERR_ALLOC: return "allocation error";
        case UPROBE_ERR_UPUMP: return "upump error";
        case UPROBE_ERR_INVALID: return "invalid argument";
        case UPROBE_ERR_EXTERNAL: return "external error";
        default: return "unknown error";
    }
}

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

    bool handled = false;
    const char *handled_str = "unhandled ";
    if ((event & UPROBE_HANDLED_FLAG)) {
        handled = true;
        handled_str = "";
        event &= ~UPROBE_HANDLED_FLAG;
    }

    switch (event) {
        case UPROBE_READY:
            upipe_log_va(upipe, log->level, "probe caught %sready event",
                         handled_str);
            break;
        case UPROBE_DEAD:
            upipe_log_va(upipe, log->level, "probe caught %sdead event",
                         handled_str);
            break;
        case UPROBE_LOG:
            break;
        case UPROBE_FATAL: {
            enum uprobe_error_code errcode =
                va_arg(args, enum uprobe_error_code);
            upipe_log_va(upipe, log->level,
                         "probe caught %sfatal error: %s (%x)",
                         handled_str, uprobe_log_errcode(errcode), errcode);
            break;
        }
        case UPROBE_ERROR: {
            enum uprobe_error_code errcode =
                va_arg(args, enum uprobe_error_code);
            upipe_log_va(upipe, log->level, "probe caught %serror: %s (%x)",
                         handled_str, uprobe_log_errcode(errcode), errcode);
            break;
        }
        case UPROBE_SOURCE_END:
            upipe_log_va(upipe, log->level, "probe caught %ssource end",
                         handled_str);
            break;
        case UPROBE_SINK_END:
            upipe_log_va(upipe, log->level, "probe caught %ssink end",
                         handled_str);
            break;
        case UPROBE_NEED_UREF_MGR:
            upipe_log_va(upipe, log->level,
                         "probe caught %sneed uref manager", handled_str);
            break;
        case UPROBE_NEED_UPUMP_MGR:
            upipe_log_va(upipe, log->level,
                         "probe caught %sneed upump manager", handled_str);
            break;
        case UPROBE_NEED_UCLOCK:
            upipe_log_va(upipe, log->level, "probe caught %sneed uclock",
                         handled_str);
            break;
        case UPROBE_NEW_FLOW_DEF: {
            struct uref *flow_def = va_arg(args_copy, struct uref *);
            const char *def = "[invalid]";
            if (flow_def != NULL)
                uref_flow_get_def(flow_def, &def);
            upipe_log_va(upipe, log->level,
                         "probe caught %snew flow def \"%s\"",
                         handled_str, def);
            break;
        }
        case UPROBE_NEED_UBUF_MGR: {
            struct uref *flow_def = va_arg(args_copy, struct uref *);
            const char *def = "[invalid]";
            if (flow_def != NULL)
                uref_flow_get_def(flow_def, &def);
            upipe_log_va(upipe, log->level,
                         "probe caught %sneed ubuf manager for flow def \"%s\"",
                         handled_str, def);
            break;
        }
        case UPROBE_NEW_RAP:
            upipe_log_va(upipe, log->level,
                         "probe caught %snew random access point", handled_str);
            break;
        case UPROBE_SPLIT_UPDATE:
            upipe_log_va(upipe, log->level, "probe caught %ssplit update",
                         handled_str);
            break;
        case UPROBE_SYNC_ACQUIRED:
            upipe_log_va(upipe, log->level, "probe caught %ssync acquired",
                         handled_str);
            break;
        case UPROBE_SYNC_LOST:
            upipe_log_va(upipe, log->level, "probe caught %ssync lost",
                         handled_str);
            break;
        case UPROBE_CLOCK_REF:
            if (!handled) {
                struct uref *uref = va_arg(args_copy, struct uref *);
                uint64_t pcr = va_arg(args_copy, uint64_t);
                int discontinuity = va_arg(args_copy, int);
                if (discontinuity == 1)
                    upipe_log_va(upipe, log->level,
                             "probe caught %snew clock ref %"PRIu64" (discontinuity)",
                             handled_str, pcr);
                else
                    upipe_log_va(upipe, log->level,
                                 "probe caught %snew clock ref %"PRIu64,
                                 handled_str, pcr);
            }
            break;
        case UPROBE_CLOCK_TS:
            if (!handled) {
                struct uref *uref = va_arg(args_copy, struct uref *);
                uint64_t date = UINT64_MAX;
                enum uref_date_type type;
                uref_clock_get_date_orig(uref, &date, &type);
                if (unlikely(type == UREF_DATE_NONE))
                    upipe_log_va(upipe, log->level,
                                "probe caught %sinvalid timestamp event",
                                handled_str);
                else
                    upipe_log_va(upipe, log->level,
                            "probe caught %snew date %"PRIu64, handled_str,
                            date);
            }
            break;
        default:
            if (!handled)
                upipe_log_va(upipe, log->level,
                             "probe caught an unknown, unhandled event (0x%x)",
                             event);
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
 * @return next probe
 */
struct uprobe *uprobe_log_free(struct uprobe *uprobe)
{
    struct uprobe *next = uprobe->next;
    struct uprobe_log *log = uprobe_log_from_uprobe(uprobe);
    free(log);
    return next;
}
