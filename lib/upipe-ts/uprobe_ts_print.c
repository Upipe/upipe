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
 * @short simple probe printing all received events from ts pipes
 */

#include <upipe/ubase.h>
#include <upipe/uref_flow.h>
#include <upipe/uprobe.h>
#include <upipe-ts/uprobe_ts_print.h>

#include <upipe-ts/upipe_ts_demux.h>
#include <upipe-ts/upipe_ts_decaps.h>
#include <upipe-ts/upipe_ts_split.h>
#include <upipe-ts/upipe_ts_patd.h>
#include <upipe-ts/upipe_ts_pmtd.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>

/** @hidden */
struct upipe;

/** super-set of the uprobe structure with additional local members */
struct uprobe_ts_print {
    /** file stream to write to */
    FILE *stream;
    /** prefix appended to all messages by this probe (informative) */
    char *name;

    /** structure exported to modules */
    struct uprobe uprobe;
};

/** @internal @This returns the high-level uprobe structure.
 *
 * @param uprobe_ts_print pointer to the uprobe_ts_print structure
 * @return pointer to the uprobe structure
 */
static inline struct uprobe *uprobe_ts_print_to_uprobe(struct uprobe_ts_print *uprobe_ts_print)
{
    return &uprobe_ts_print->uprobe;
}

/** @internal @This returns the private uprobe_ts_print structure.
 *
 * @param mgr description structure of the uprobe
 * @return pointer to the uprobe_ts_print structure
 */
static inline struct uprobe_ts_print *uprobe_ts_print_from_uprobe(struct uprobe *uprobe)
{
    return container_of(uprobe, struct uprobe_ts_print, uprobe);
}

/** @internal @This catches events thrown by ts pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return always true
 */
static bool uprobe_ts_print_throw(struct uprobe *uprobe, struct upipe *upipe,
                               enum uprobe_event event, va_list args)
{
    struct uprobe_ts_print *uprobe_ts_print =
        uprobe_ts_print_from_uprobe(uprobe);
    if (event <= UPROBE_LOCAL)
        return false;

    const char *name = likely(uprobe_ts_print->name != NULL) ?
                       uprobe_ts_print->name : "unknown";
    va_list args_copy;
    va_copy(args_copy, args);
    unsigned int signature = va_arg(args_copy, unsigned int);

    switch (event) {
        case UPROBE_TS_DEMUX_NEW_PSI_FLOW: {
            assert(signature == UPIPE_TS_DEMUX_SIGNATURE);
            struct uref *flow_def = va_arg(args_copy, struct uref *);
            const char *flow_suffix = va_arg(args_copy, const char *);
            const char *def = "[invalid]";
            uref_flow_get_def(flow_def, &def);
            fprintf(uprobe_ts_print->stream,
                    "%s probe: received new PSI flow definition \"%s\" from ts_demux pipe %p on output %s\n",
                    name, def, upipe, flow_suffix);
            break;
        }

        case UPROBE_TS_DECAPS_PCR: {
            assert(signature == UPIPE_TS_DECAPS_SIGNATURE);
            struct uref *uref = va_arg(args_copy, struct uref *);
            uint64_t pcr = va_arg(args_copy, uint64_t);
            fprintf(uprobe_ts_print->stream,
                    "%s probe: received new PCR %"PRIu64" from ts_decaps pipe %p\n",
                    name, pcr, upipe);
            break;
        }

        case UPROBE_TS_SPLIT_SET_PID: {
            assert(signature == UPIPE_TS_SPLIT_SIGNATURE);
            unsigned int pid = va_arg(args_copy, unsigned int);
            fprintf(uprobe_ts_print->stream,
                    "%s probe: ts_split pipe %p required PID %u\n",
                    name, upipe, pid);
            break;
        }
        case UPROBE_TS_SPLIT_UNSET_PID: {
            assert(signature == UPIPE_TS_SPLIT_SIGNATURE);
            unsigned int pid = va_arg(args_copy, unsigned int);
            fprintf(uprobe_ts_print->stream,
                    "%s probe: ts_split pipe %p released PID %u\n",
                    name, upipe, pid);
            break;
        }

        case UPROBE_TS_PATD_TSID: {
            assert(signature == UPIPE_TS_PATD_SIGNATURE);
            struct uref *uref = va_arg(args_copy, struct uref *);
            unsigned int tsid = va_arg(args_copy, unsigned int);
            fprintf(uprobe_ts_print->stream,
                    "%s probe: ts_patd pipe %p reported new TSID %u\n",
                    name, upipe, tsid);
            break;
        }
        case UPROBE_TS_PATD_ADD_PROGRAM: {
            assert(signature == UPIPE_TS_PATD_SIGNATURE);
            struct uref *uref = va_arg(args_copy, struct uref *);
            unsigned int program = va_arg(args_copy, unsigned int);
            unsigned int pid = va_arg(args_copy, unsigned int);
            fprintf(uprobe_ts_print->stream,
                    "%s probe: ts_patd pipe %p added program %u on PID %u\n",
                    name, upipe, program, pid);
            break;
        }
        case UPROBE_TS_PATD_DEL_PROGRAM: {
            assert(signature == UPIPE_TS_PATD_SIGNATURE);
            struct uref *uref = va_arg(args_copy, struct uref *);
            unsigned int program = va_arg(args_copy, unsigned int);
            unsigned int pid = va_arg(args_copy, unsigned int);
            fprintf(uprobe_ts_print->stream,
                    "%s probe: ts_patd pipe %p deleted program %u on PID %u\n",
                    name, upipe, program, pid);
            break;
        }

        case UPROBE_TS_PMTD_HEADER: {
            assert(signature == UPIPE_TS_PMTD_SIGNATURE);
            struct uref *uref = va_arg(args_copy, struct uref *);
            unsigned int pcrpid = va_arg(args_copy, unsigned int);
            fprintf(uprobe_ts_print->stream,
                    "%s probe: ts_pmtd pipe %p reported new PCR PID %u\n",
                    name, upipe, pcrpid);
            break;
        }
        case UPROBE_TS_PMTD_ADD_ES: {
            assert(signature == UPIPE_TS_PMTD_SIGNATURE);
            struct uref *uref = va_arg(args_copy, struct uref *);
            unsigned int pid = va_arg(args_copy, unsigned int);
            unsigned int streamtype = va_arg(args_copy, unsigned int);
            fprintf(uprobe_ts_print->stream,
                    "%s probe: ts_pmtd pipe %p added ES PID %u, stream type %u\n",
                    name, upipe, pid, streamtype);
            break;
        }
        case UPROBE_TS_PMTD_DEL_ES: {
            assert(signature == UPIPE_TS_PMTD_SIGNATURE);
            struct uref *uref = va_arg(args_copy, struct uref *);
            unsigned int pid = va_arg(args_copy, unsigned int);
            fprintf(uprobe_ts_print->stream,
                    "%s probe: ts_patd pipe %p deleted ES PID %u\n",
                    name, upipe, pid);
            break;
        }

        default:
            fprintf(uprobe_ts_print->stream,
                    "%s probe: ts pipe %p threw an unknown, uncaught event (0x%x)\n",
                    name, upipe, event);
            break;
    }
    va_end(args_copy);
    return false;
}

/** @This frees a uprobe print structure.
 *
 * @param uprobe print structure to free
 */
void uprobe_ts_print_free(struct uprobe *uprobe)
{
    struct uprobe_ts_print *uprobe_ts_print =
        uprobe_ts_print_from_uprobe(uprobe);
    free(uprobe_ts_print->name);
    free(uprobe_ts_print);
}

/** @This allocates a new uprobe print structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param stream file stream to write to (eg. stderr)
 * @param name prefix appended to all messages by this probe (informative)
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_ts_print_alloc(struct uprobe *next, FILE *stream,
                                     const char *name)
{
    struct uprobe_ts_print *uprobe_ts_print =
        malloc(sizeof(struct uprobe_ts_print));
    if (unlikely(uprobe_ts_print == NULL))
        return NULL;
    struct uprobe *uprobe = uprobe_ts_print_to_uprobe(uprobe_ts_print);
    uprobe_ts_print->stream = stream;
    if (likely(name != NULL)) {
        uprobe_ts_print->name = strdup(name);
        if (unlikely(uprobe_ts_print->name == NULL))
            fprintf(stream, "uprobe_ts_print: couldn't allocate a string\n");
    } else
        uprobe_ts_print->name = NULL;
    uprobe_init(uprobe, uprobe_ts_print_throw, next);
    return uprobe;
}

/** @This allocates a new uprobe print structure, with composite name.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param stream file stream to write to (eg. stderr)
 * @param format printf-format string used for the prefix appended to all
 * messages by this probe, followed by optional arguments
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_ts_print_alloc_va(struct uprobe *next, FILE *stream,
                                        const char *format, ...)
{
    size_t len;
    va_list args;
    va_start(args, format);
    len = vsnprintf(NULL, 0, format, args);
    va_end(args);
    if (len > 0) {
        char name[len + 1];
        va_start(args, format);
        vsnprintf(name, len + 1, format, args);
        va_end(args);
        return uprobe_ts_print_alloc(next, stream, name);
    }
    return uprobe_ts_print_alloc(next, stream, "unknown");
}
