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
 * @short Upipe helper functions for sinks
 * It allows to block a source pump, and to bufferize incoming urefs.
 */

#ifndef _UPIPE_UPIPE_HELPER_SINK_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_SINK_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uref.h>
#include <upipe/upump.h>
#include <upipe/upump_blocker.h>
#include <upipe/upipe.h>

#include <stdbool.h>

/** @This declares eight functions helping a sink pipe to block source pumps,
 * and to hold urefs that can't be immediately output.
 *
 * You must add four members to your private upipe structure, for instance:
 * @code
 *  struct uchain urefs;
 *  unsigned int nb_urefs;
 *  unsigned int max_urefs;
 *  struct uchain blockers;
 * @end code
 *
 * You must also declare @ref #UPIPE_HELPER_UPIPE prior to using this macro.
 *
 * Supposing the name of your structure is upipe_foo, it declares:
 * @list
 * @item @code
 *  void upipe_foo_init_sink(struct upipe *upipe)
 * @end code
 * Typically called in your upipe_foo_alloc() function.
 *
 * @item @code
 *  void upipe_foo_block_sink_cb(struct upump_blocker *blocker)
 * @end code
 * Used internally as the callback of the blocker, called when the source pump
 * is released.
 *
 * @item @code
 *  void upipe_foo_block_sink(struct upipe *upipe, struct upump *upump)
 * @end code
 * Called when you need to block a source pump.
 *
 * @item @code
 *  void upipe_foo_unblock_sink(struct upipe *upipe)
 * @end code
 * Called when you want to restart the source pump(s).
 *
 * @item @code
 *  bool upipe_foo_check_sink(struct upipe *upipe)
 * @end code
 * Returns true if no uref has been held.
 *
 * @item @code
 *  void upipe_foo_hold_sink(struct upipe *upipe, struct uref *uref)
 * @end code
 * Holds the given uref that can't be immediately output.
 *
 * @item @code
 *  struct uref *upipe_foo_pop_sink(struct upipe *upipe)
 * @end code
 * Returns the first buffered uref.
 *
 * @item @code
 * @item @code
 *  bool upipe_foo_output_sink(struct upipe *upipe)
 * @end code
 * Outputs urefs that have been held.
 *
 * @item @code
 *  enum ubase_err upipe_foo_get_max_length(struct upipe *upipe,
 *                                          unsigned int *p)
 * @end code
 * Typically called from your upipe_foo_control() handler, such as:
 * @code
 *  case UPIPE_SINK_GET_MAX_LENGTH: {
 *      unsigned int *p = va_arg(args, unsigned int *);
 *      return upipe_foo_get_max_length(upipe, p);
 *  }
 * @end code
 *
 * @item @code
 *  enum ubase_err upipe_foo_set_max_length(struct upipe *upipe,
 *                                          unsigned int max_length)
 * @end code
 * Typically called from your upipe_foo_control() handler, such as:
 * @code
 *  case UPIPE_SINK_SET_MAX_LENGTH: {
 *      unsigned int max_length = va_arg(args, unsigned int);
 *      return upipe_foo_set_max_length(upipe, max_length);
 *  }
 * @end code
 *
 * @item @code
 *  void upipe_foo_clean_sink(struct upipe *upipe)
 * @end code
 * Free all urefs that have been held, and unblocks all pumps.
 *
 * @item @code
 *  bool upipe_foo_flush_sink(struct upipe *upipe)
 * @end code
 * Free all urefs that have been held, unblocks all pumps, and reinitializes
 * the sink. Returns true if the sink was previsouly blocked.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure
 * @param UREFS name of the @tt {struct uchain} field of
 * your private upipe structure, corresponding to a list of urefs
 * @param BLOCKERS name of the @tt {struct uchain} field of
 * your private upipe structure, corresponding to a list of blockers
 * @param OUTPUT function to use to output urefs (struct upipe *, struct uref *,
 * struct upump *), returns false when the uref can't be written
 */
#define UPIPE_HELPER_SINK(STRUCTURE, UREFS, NB_UREFS, MAX_UREFS, BLOCKERS,  \
                          OUTPUT)                                           \
/** @internal @This initializes the private members for this helper.        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_init_sink(struct upipe *upipe)                      \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    ulist_init(&s->UREFS);                                                  \
    s->NB_UREFS = 0;                                                        \
    s->MAX_UREFS = 0;                                                       \
    ulist_init(&s->BLOCKERS);                                               \
}                                                                           \
/** @internal @This is called when the source pump is released by its owner.\
 *                                                                          \
 * @param blocker description structure of the blocker                      \
 */                                                                         \
static void STRUCTURE##_block_sink_cb(struct upump_blocker *blocker)        \
{                                                                           \
    ulist_delete(upump_blocker_to_uchain(blocker));                         \
    upump_blocker_free(blocker);                                            \
}                                                                           \
/** @internal @This blocks the given source pump.                           \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param upump source pump to block                                        \
 */                                                                         \
static void STRUCTURE##_block_sink(struct upipe *upipe, struct upump *upump)\
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    if (s->NB_UREFS <= s->MAX_UREFS)                                        \
        return;                                                             \
    if (upump != NULL &&                                                    \
        upump_blocker_find(&s->BLOCKERS, upump) == NULL) {                  \
        struct upump_blocker *blocker =                                     \
            upump_blocker_alloc(upump, STRUCTURE##_block_sink_cb, upipe);   \
        ulist_add(&s->BLOCKERS, upump_blocker_to_uchain(blocker));          \
    }                                                                       \
}                                                                           \
/** @internal @This unblocks all source pumps.                              \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_unblock_sink(struct upipe *upipe)                   \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    if (s->NB_UREFS > s->MAX_UREFS)                                         \
        return;                                                             \
    struct uchain *uchain, *uchain_tmp;                                     \
    ulist_delete_foreach (&s->BLOCKERS, uchain, uchain_tmp) {               \
        ulist_delete(uchain);                                               \
        upump_blocker_free(upump_blocker_from_uchain(uchain));              \
    }                                                                       \
}                                                                           \
/** @internal @This checks if the sink is currently writable, or holds      \
 * packets.                                                                 \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @return true if the sink holds urefs                                     \
 */                                                                         \
static bool STRUCTURE##_check_sink(struct upipe *upipe)                     \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    return ulist_empty(&s->UREFS);                                          \
}                                                                           \
/** @internal @This holds the given uref for the time necessary for the     \
 * sink to become writable again.                                           \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param uref uref to hold                                                 \
 */                                                                         \
static void STRUCTURE##_hold_sink(struct upipe *upipe, struct uref *uref)   \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    ulist_add(&s->UREFS, uref_to_uchain(uref));                             \
    s->NB_UREFS++;                                                          \
}                                                                           \
/** @internal @This pops an uref from the buffered urefs.                   \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @return buffered uref                                                    \
 */                                                                         \
static struct uref *STRUCTURE##_pop_sink(struct upipe *upipe)               \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    struct uchain *uchain = ulist_pop(&s->UREFS);                           \
    if (uchain != NULL)                                                     \
        s->NB_UREFS--;                                                      \
    return uref_from_uchain(uchain);                                        \
}                                                                           \
/** @internal @This outputs all urefs that have been held.                  \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @return true if all urefs could be output                                \
 */                                                                         \
static bool STRUCTURE##_output_sink(struct upipe *upipe)                    \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    struct uchain *uchain;                                                  \
    while ((uchain = ulist_pop(&s->UREFS)) != NULL) {                       \
        s->NB_UREFS--;                                                      \
        bool (*output)(struct upipe *, struct uref *, struct upump *) =     \
            OUTPUT;                                                         \
        if (output != NULL &&                                               \
            !output(upipe, uref_from_uchain(uchain), NULL)) {               \
            ulist_unshift(&s->UREFS, uchain);                               \
            s->NB_UREFS++;                                                  \
            return false;                                                   \
        }                                                                   \
    }                                                                       \
    return true;                                                            \
}                                                                           \
/** @internal @This gets the current max length of the internal queue.      \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param p filled in with the length                                       \
 * @return an error code                                                    \
 */                                                                         \
static enum ubase_err STRUCTURE##_get_max_length(struct upipe *upipe,       \
                                                 unsigned int *p)           \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    assert(p != NULL);                                                      \
    *p = STRUCTURE->MAX_UREFS;                                              \
    return UBASE_ERR_NONE;                                                  \
}                                                                           \
/** @internal @This sets the max length of the internal queue.              \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param length new length                                                 \
 * @return an error code                                                    \
 */                                                                         \
static enum ubase_err STRUCTURE##_set_max_length(struct upipe *upipe,       \
                                                 unsigned int length)       \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    STRUCTURE->MAX_UREFS = length;                                          \
    return UBASE_ERR_NONE;                                                  \
}                                                                           \
/** @internal @This frees all urefs that have been held, and unblocks       \
 * all source pumps.                                                        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_clean_sink(struct upipe *upipe)                     \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    STRUCTURE##_unblock_sink(upipe);                                        \
    struct uchain *uchain, *uchain_tmp;                                     \
    ulist_delete_foreach (&s->UREFS, uchain, uchain_tmp) {                  \
        ulist_delete(uchain);                                               \
        uref_free(uref_from_uchain(uchain));                                \
    }                                                                       \
}                                                                           \
/** @internal @This flushes all currently held buffers, and unblocks the    \
 * sources.                                                                 \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @return false if the sink was previously blocked                         \
 */                                                                         \
static bool STRUCTURE##_flush_sink(struct upipe *upipe)                     \
{                                                                           \
    if (STRUCTURE##_check_sink(upipe))                                      \
        return false;                                                       \
    STRUCTURE##_clean_sink(upipe);                                          \
    STRUCTURE##_init_sink(upipe);                                           \
    return true;                                                            \
}

#ifdef __cplusplus
}
#endif
#endif
