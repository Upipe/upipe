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
 * @short simple probe printing all received events, as a fall-back
 */

#include <upipe/ubase.h>
#include <upipe/uref_flow.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_print.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/** @hidden */
struct upipe;

/** super-set of the uprobe structure with additional local members */
struct uprobe_print {
    /** file stream to write to */
    FILE *stream;
    /** prefix appended to all messages by this probe (informative) */
    char *name;

    /** structure exported to modules */
    struct uprobe uprobe;
};

/** @internal @This returns the high-level uprobe structure.
 *
 * @param uprobe_print pointer to the uprobe_print structure
 * @return pointer to the uprobe structure
 */
static inline struct uprobe *uprobe_print_to_uprobe(struct uprobe_print *uprobe_print)
{
    return &uprobe_print->uprobe;
}

/** @internal @This returns the private uprobe_print structure.
 *
 * @param mgr description structure of the uprobe
 * @return pointer to the uprobe_print structure
 */
static inline struct uprobe_print *uprobe_print_from_uprobe(struct uprobe *uprobe)
{
    return container_of(uprobe, struct uprobe_print, uprobe);
}

/** @internal @This catches events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return always true
 */
static bool uprobe_print_throw(struct uprobe *uprobe, struct upipe *upipe,
                               enum uprobe_event event, va_list args)
{
    struct uprobe_print *uprobe_print = uprobe_print_from_uprobe(uprobe);
    const char *name = likely(uprobe_print->name != NULL) ? uprobe_print->name :
                       "unknown";
    va_list args_copy;
    va_copy(args_copy, args);

    switch (event) {
        case UPROBE_READY:
            fprintf(uprobe_print->stream,
                    "%s probe: received ready event from pipe %p\n",
                    name, upipe);
            break;
        case UPROBE_DEAD:
            fprintf(uprobe_print->stream,
                    "%s probe: received dead event from pipe %p\n",
                    name, upipe);
            break;
        case UPROBE_AERROR:
            fprintf(uprobe_print->stream,
                    "%s probe: received allocation error from pipe %p\n",
                    name, upipe);
            break;
        case UPROBE_FLOW_DEF_ERROR:
            fprintf(uprobe_print->stream,
                    "%s probe: received flow def error from pipe %p\n",
                    name, upipe);
            break;
        case UPROBE_UPUMP_ERROR:
            fprintf(uprobe_print->stream,
                    "%s probe: received upump error from pipe %p\n",
                    name, upipe);
            break;
        case UPROBE_READ_END: {
            const char *location = va_arg(args_copy, const char *);
            fprintf(uprobe_print->stream,
                    "%s probe: received read end from pipe %p on %s\n",
                    name, upipe, location);
            break;
        }
        case UPROBE_WRITE_END: {
            const char *location = va_arg(args_copy, const char *);
            fprintf(uprobe_print->stream,
                    "%s probe: received write end from pipe %p on %s\n",
                    name, upipe, location);
            break;
        }
        case UPROBE_NEED_UREF_MGR:
            fprintf(uprobe_print->stream,
                    "%s probe: pipe %p required a uref manager\n",
                    name, upipe);
            break;
        case UPROBE_NEED_UPUMP_MGR:
            fprintf(uprobe_print->stream,
                    "%s probe: pipe %p required a upump manager\n",
                    name, upipe);
            break;
        case UPROBE_NEED_UBUF_MGR:
            fprintf(uprobe_print->stream,
                    "%s probe: pipe %p required a ubuf manager\n",
                    name, upipe);
            break;
        case UPROBE_NEED_OUTPUT: {
            struct uref *flow_def = va_arg(args_copy, struct uref *);
            const char *def = "[invalid]";
            uref_flow_get_def(flow_def, &def);
            fprintf(uprobe_print->stream,
                    "%s probe: pipe %p required an output for flow definition \"%s\"\n",
                    name, upipe, def);
            break;
        }
        case UPROBE_SPLIT_NEW_FLOW: {
            struct uref *flow_def = va_arg(args_copy, struct uref *);
            const char *def = "[invalid]";
            uref_flow_get_def(flow_def, &def);
            fprintf(uprobe_print->stream,
                    "%s probe: received new flow definition \"%s\" from split pipe %p\n",
                    name, def, upipe);
            break;
        }
        case UPROBE_SYNC_ACQUIRED:
            fprintf(uprobe_print->stream,
                    "%s probe: pipe %p acquired sync\n",
                    name, upipe);
            break;
        case UPROBE_SYNC_LOST:
            fprintf(uprobe_print->stream,
                    "%s probe: pipe %p lost sync\n",
                    name, upipe);
            break;
        default:
            fprintf(uprobe_print->stream,
                    "%s probe: pipe %p threw an unknown, uncaught event (0x%x)\n",
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
void uprobe_print_free(struct uprobe *uprobe)
{
    struct uprobe_print *uprobe_print = uprobe_print_from_uprobe(uprobe);
    free(uprobe_print->name);
    free(uprobe_print);
}

/** @This allocates a new uprobe print structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param stream file stream to write to (eg. stderr)
 * @param name prefix appended to all messages by this probe (informative)
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_print_alloc(struct uprobe *next, FILE *stream,
                                  const char *name)
{
    struct uprobe_print *uprobe_print = malloc(sizeof(struct uprobe_print));
    if (unlikely(uprobe_print == NULL))
        return NULL;
    struct uprobe *uprobe = uprobe_print_to_uprobe(uprobe_print);
    uprobe_print->stream = stream;
    if (likely(name != NULL)) {
        uprobe_print->name = strdup(name);
        if (unlikely(uprobe_print->name == NULL))
            fprintf(stream, "uprobe_print: couldn't allocate a string\n");
    } else
        uprobe_print->name = NULL;
    uprobe_init(uprobe, uprobe_print_throw, next);
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
struct uprobe *uprobe_print_alloc_va(struct uprobe *next, FILE *stream,
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
        return uprobe_print_alloc(next, stream, name);
    }
    return uprobe_print_alloc(next, stream, "unknown");
}
