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
 * @short uprobe helper functions for ad-hoc mode
 * Ad-hoc mode allows probes to be allocated on-the-fly, and deallocated when
 * the underlying pipe dies. This works by intercepting the first UPROBE_READY
 * event, storing the pointer to the pipe that emetted the event, and waiting
 * for it to send UPROBE_DEAD. Of course the probe may only be used for one
 * pipe.
 */

#ifndef _UPIPE_UPROBE_HELPER_ADHOC_H_
/** @hidden */
#define _UPIPE_UPROBE_HELPER_ADHOC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

/** @This declares three functions helping probes to work in ad-hoc mode.
 *
 * You must add one pointer to your private uprobe structure, for instance:
 * @code
 *  struct upipe *adhoc_pipe;
 * @end code
 *
 * Supposing the name of your structure is uprobe_foo, it declares:
 * @list
 * @item @code
 *  void uprobe_foo_init_adhoc(struct uprobe *uprobe)
 * @end code
 * Typically called in your uprobe_foo_alloc() function.
 *
 * @item @code
 *  bool uprobe_foo_throw_adhoc(struct uprobe *uprobe, struct upipe *upipe,
 *                              enum uprobe_event event, va_list args)
 * @end code
 * Typically called from your uprobe_foo_throw() handler, in the default case.
 *
 * @item @code
 *  void uprobe_foo_clean_adhoc(struct uprobe *uprobe)
 * @end code
 * Typically called from your uprobe_foo_free() function.
 * @end list
 *
 * @param STRUCTURE name of your private uprobe structure 
 * @param UPIPE name of the @tt {struct upipe *} field of
 * your private uprobe structure
 */
#define UPROBE_HELPER_ADHOC(STRUCTURE, UPIPE)                               \
/** @internal @This initializes the private members for this helper.        \
 *                                                                          \
 * @param uprobe pointer to probe                                           \
 */                                                                         \
static void STRUCTURE##_init_adhoc(struct uprobe *uprobe)                   \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_uprobe(uprobe);          \
    STRUCTURE->UPIPE = NULL;                                                \
}                                                                           \
/** @internal @This catches events that may be relevant to ad-hoc probes.   \
 *                                                                          \
 * @param uprobe pointer to probe                                           \
 * @param upipe pointer to pipe throwing the event                          \
 * @param event event thrown                                                \
 * @param args optional event-specific parameters                           \
 * @return true if the event has been handled                               \
 */                                                                         \
static bool STRUCTURE##_throw_adhoc(struct uprobe *uprobe,                  \
                                    struct upipe *upipe,                    \
                                    enum uprobe_event event, va_list args)  \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_uprobe(uprobe);          \
    switch (event) {                                                        \
        case UPROBE_READY:                                                  \
            if (STRUCTURE->UPIPE == NULL) {                                 \
                /* Keep a pointer to the pipe we're attaching to. There is  \
                 * no need to @ref upipe_use here, as the pipe cannot       \
                 * disappear without sending the @ref UPROBE_DEAD event,    \
                 * and we do not actually access the pipe's members, but    \
                 * match the pointers. Besides, using @ref upipe_use would  \
                 * make the pipe unkillable. */                             \
                STRUCTURE->UPIPE = upipe;                                   \
                uprobe_throw(uprobe->next, upipe, event, args);             \
                return true;                                                \
            }                                                               \
            break;                                                          \
        case UPROBE_DEAD:                                                   \
            if (STRUCTURE->UPIPE == upipe) {                                \
                /* The pipe we're attached to is dying, let's deallocate. */\
                uprobe_throw(uprobe->next, upipe, event, args);             \
                upipe_delete_probe(upipe, uprobe);                          \
                STRUCTURE##_free(uprobe);                                   \
                return true;                                                \
            }                                                               \
            break;                                                          \
        case UPROBE_FATAL:                                                  \
            if (STRUCTURE->UPIPE == NULL && upipe == NULL) {                \
                /* The pipe couldn't be created, let's deallocate. */       \
                uprobe_throw(uprobe->next, upipe, event, args);             \
                STRUCTURE##_free(uprobe);                                   \
                return true;                                                \
            }                                                               \
            break;                                                          \
        default:                                                            \
            break;                                                          \
    }                                                                       \
    return false;                                                           \
}                                                                           \
/** @internal @This cleans up the private members for this helper.          \
 *                                                                          \
 * @param uprobe pointer to probe                                           \
 */                                                                         \
static void STRUCTURE##_clean_adhoc(struct uprobe *uprobe)                  \
{                                                                           \
}

#ifdef __cplusplus
}
#endif
#endif
