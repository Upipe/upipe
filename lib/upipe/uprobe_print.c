/*****************************************************************************
 * uprobe_print.c: simple probe printing all received events, as a fall-back
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

#include <upipe/ubase.h>
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

    switch (event) {
        case UPROBE_AERROR:
            fprintf(uprobe_print->stream,
                    "%s probe: received allocation error from pipe %p\n",
                    uprobe_print->name, upipe);
            break;
        case UPROBE_UPUMP_ERROR:
            fprintf(uprobe_print->stream,
                    "%s probe: received upump error from pipe %p\n",
                    uprobe_print->name, upipe);
            break;
        case UPROBE_READ_END: {
            const char *location = va_arg(args, const char *);
            fprintf(uprobe_print->stream,
                    "%s probe: received read end from pipe %p on %s\n",
                    uprobe_print->name, upipe, location);
            break;
        }
        case UPROBE_WRITE_END: {
            const char *location = va_arg(args, const char *);
            fprintf(uprobe_print->stream,
                    "%s probe: received write end from pipe %p on %s\n",
                    uprobe_print->name, upipe, location);
            break;
        }
        case UPROBE_NEW_FLOW: {
            const char *flow_name = va_arg(args, const char *);
            fprintf(uprobe_print->stream,
                    "%s probe: received new flow from pipe %p on output %s\n",
                    uprobe_print->name, upipe, flow_name);
            break;
        }
        case UPROBE_NEED_UREF_MGR:
            fprintf(uprobe_print->stream,
                    "%s probe: pipe %p required a uref manager\n",
                    uprobe_print->name, upipe);
            break;
        case UPROBE_NEED_UPUMP_MGR:
            fprintf(uprobe_print->stream,
                    "%s probe: pipe %p required a upump manager\n",
                    uprobe_print->name, upipe);
            break;
        default:
            fprintf(uprobe_print->stream,
                    "%s probe: pipe %p threw an unknown, uncaught event (%d)\n",
                    uprobe_print->name, upipe, event);
            break;
    }
    return true;
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
 * @param stream file stream to write to (eg. stderr)
 * @param name prefix appended to all messages by this probe (informative)
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_print_alloc(FILE *stream, const char *name)
{
    struct uprobe_print *uprobe_print = malloc(sizeof(struct uprobe_print));
    if (unlikely(uprobe_print == NULL)) return NULL;
    struct uprobe *uprobe = uprobe_print_to_uprobe(uprobe_print);
    uprobe_print->stream = stream;
    uprobe_print->name = strdup(name);
    uprobe_init(uprobe, uprobe_print_throw, NULL);
    return uprobe;
}

/** @This allocates a new uprobe print structure, with composite name.
 *
 * @param stream file stream to write to (eg. stderr)
 * @param format printf-format string used for the prefix appended to all
 * messages by this probe, followed by optional arguments
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_print_alloc_va(FILE *stream, const char *format, ...)
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
        return uprobe_print_alloc(stream, name);
    }
    return uprobe_print_alloc(stream, "unknown");
}
