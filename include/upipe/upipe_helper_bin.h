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
 * @short Upipe helper functions for bin
 */

#ifndef _UPIPE_UPIPE_HELPER_BIN_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_BIN_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe.h>

#include <stdbool.h>

/** @This declares five functions dealing with specials pipes called "bins",
 * which internally implement a sub-pipeline to handle a given task. It also
 * acts as a proxy to the last element of the sub-pipeline.
 *
 * You must add three members to your private upipe structure, for instance:
 * @code
 *  struct uprobe last_subpipe_probe;
 *  struct upipe *last_subpipe;
 *  struct upipe *output;
 * @end code
 *
 * You must also declare @ref #UPIPE_HELPER_UPIPE prior to using this macro.
 *
 * Supposing the name of your structure is upipe_foo, it declares:
 * @list
 * @item @code
 *  bool upipe_foo_probe_bin(struct uprobe *uprobe, struct upipe *subpipe,
 *                           enum uprobe_event event, va_list args)
 * @end @code
 * Probe to set on the last subpipe. It attaches all events (proxy) to the bin
 * pipe. The @tt {struct uprobe} member is set to point to this probe during
 * init.
 *
 * @item @code
 *  void upipe_foo_init_bin(struct upipe *upipe)
 * @end code
 * Typically called in your upipe_foo_alloc() function.
 *
 * @item @code
 *  void upipe_foo_store_last_subpipe(struct upipe *upipe,
 *                                    struct upipe *subpipe)
 * @end code
 * Called whenever you change the last subpipe of this bin.
 *
 * @item @code
 *  bool upipe_foo_control_bin(struct upipe *upipe, enum upipe_command command,
                               va_list args)
 * @end code
 * Typically called from your upipe_foo_control() handler. It handles the
 * set_output commands internally, and then acts as a proxy for other commands.
 *
 * @item @code
 *  void upipe_foo_clean_bin(struct upipe *upipe)
 * @end code
 * Typically called from your upipe_foo_free() function.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure
 * @param LAST_SUBPIPE_PROBE name of the @tt {struct uprobe} field of
 * your private upipe structure
 * @param LAST_SUBPIPE name of the @tt{struct upipe *} field of
 * your private upipe structure, pointing to the last subpipe of the bin
 * @param OUTPUT name of the @tt{struct upipe *} field of
 * your private upipe structure, pointing to the output of the bin
 */
#define UPIPE_HELPER_BIN(STRUCTURE, LAST_SUBPIPE_PROBE, LAST_SUBPIPE,       \
                         OUTPUT)                                            \
/** @internal @This catches events coming from the last subpipe, and        \
 * attaches them to the bin pipe.                                           \
 *                                                                          \
 * @param uprobe pointer to the probe in STRUCTURE                          \
 * @param upipe pointer to the subpipe                                      \
 * @param event event triggered by the subpipe                              \
 * @param args arguments of the event                                       \
 * @return always true                                                      \
 */                                                                         \
static bool STRUCTURE##_probe_bin(struct uprobe *uprobe,                    \
                                  struct upipe *subpipe,                    \
                                  enum uprobe_event event, va_list args)    \
{                                                                           \
    if (event == UPROBE_READY || event == UPROBE_DEAD)                      \
        return true;                                                        \
    struct STRUCTURE *s = container_of(uprobe, struct STRUCTURE,            \
                                       LAST_SUBPIPE_PROBE);                 \
    struct upipe *upipe = STRUCTURE##_to_upipe(s);                          \
    upipe_throw_va(upipe, event, args);                                     \
    return true;                                                            \
}                                                                           \
/** @internal @This initializes the private members for this helper.        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_init_bin(struct upipe *upipe)                       \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    uprobe_init(&s->LAST_SUBPIPE_PROBE, STRUCTURE##_probe_bin, NULL);       \
    s->LAST_SUBPIPE = NULL;                                                 \
    s->OUTPUT = NULL;                                                       \
}                                                                           \
/** @internal @This stores the last subpipe, while releasing the previous   \
 * one, and setting the output.                                             \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param last_subpipe last subpipe                                         \
 */                                                                         \
static void STRUCTURE##_store_last_subpipe(struct upipe *upipe,             \
                                           struct upipe *last_subpipe)      \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    if (s->LAST_SUBPIPE != NULL)                                            \
        upipe_release(s->LAST_SUBPIPE);                                     \
    s->LAST_SUBPIPE = last_subpipe;                                         \
    if (last_subpipe != NULL && s->OUTPUT != NULL)                          \
        upipe_set_output(last_subpipe, s->OUTPUT);                          \
}                                                                           \
/** @internal @This handles the control commands.                           \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param p filled in with the flow definition (to use on the output)       \
 * @return false in case of error                                           \
 */                                                                         \
static bool STRUCTURE##_control_bin(struct upipe *upipe,                    \
                                    enum upipe_command command,             \
                                    va_list args)                           \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    switch (command) {                                                      \
        case UPIPE_GET_OUTPUT: {                                            \
            struct upipe **p = va_arg(args, struct upipe **);               \
            *p = s->OUTPUT;                                                 \
            return true;                                                    \
        }                                                                   \
        case UPIPE_SET_OUTPUT: {                                            \
            struct upipe *output = va_arg(args, struct upipe *);            \
            if (unlikely(s->OUTPUT != NULL)) {                              \
                upipe_release(s->OUTPUT);                                   \
                s->OUTPUT = NULL;                                           \
            }                                                               \
                                                                            \
            if (unlikely(s->LAST_SUBPIPE != NULL &&                         \
                         !upipe_set_output(s->LAST_SUBPIPE, output)))       \
                return false;                                               \
            s->OUTPUT = output;                                             \
            if (likely(output != NULL))                                     \
                upipe_use(output);                                          \
            return true;                                                    \
        }                                                                   \
        default:                                                            \
            if (s->LAST_SUBPIPE == NULL)                                    \
                return false;                                               \
            return upipe_control_va(s->LAST_SUBPIPE, command, args);        \
    }                                                                       \
}                                                                           \
/** @internal @This cleans up the private members for this helper.          \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_clean_bin(struct upipe *upipe)                      \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    if (likely(s->LAST_SUBPIPE != NULL))                                    \
        upipe_release(s->LAST_SUBPIPE);                                     \
    if (likely(s->OUTPUT != NULL))                                          \
        upipe_release(s->OUTPUT);                                           \
}

#ifdef __cplusplus
}
#endif
#endif
