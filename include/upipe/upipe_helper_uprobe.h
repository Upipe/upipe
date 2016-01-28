/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
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
 * @short Upipe helper functions for inner pipe probes
 */

#ifndef _UPIPE_UPIPE_HELPER_UPROBE_H_
# define _UPIPE_UPIPE_HELPER_UPROBE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/uprobe.h>

/** @This declares functions dealing with inner pipe probes,
 * which internally catch and forward inner pipeline events.
 *
 * You must add four members to your private upipe structure, for instance:
 * @code
 *  struct uprobe inner_probe;
 * @end code
 *
 * You must also declare @ref #UPIPE_HELPER_UPIPE prior to using this macro.
 *
 * Supposing the name of your structure is upipe_foo and the name of your
 * member is inner_probe, it declares:
 * @list
 * @item @code
 *  struct uprobe *upipe_foo_to_inner_probe(struct upipe_foo *upipe_foo)
 * @end code
 * Returns a pointer to the public uprobe structure.
 *
 * @item @code
 *  struct upipe_foo *upipe_foo_from_inner_probe(struct uprobe *uprobe)
 * @end code
 * Returns a pointer to the private upipe_foo structure.
 *
 * @item @code
 *  int upipe_foo_throw_proxy_inner_probe(struct uprobe *uprobe,
 *                                        struct upipe *inner,
 *                                        int event, va_list args)
 * @end code
 * Used by the helper to attach the event from the inner pipe to
 * the super pipe.
 *
 * @item @code
 *  void upipe_foo_init_inner_probe(struct upipe *upipe)
 * @end code
 * Typically called in your upipe_foo_alloc() function.
 *
 * @item @code
 *  void upipe_foo_clean_inner_probe(struct upipe *upipe)
 * @end code
 * Typically called from your upipe_foo_free() function.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure
 * @param UREFCOUNT the @tt{struct urefcount} field of your private
 * upipe structure.
 * @param UPROBE name of the @tt{struct uprobe} field of
 * your private upipe structure
 * @param THROW the name of the @tt{uprobe_throw_func} function
 * catching the events.
 */
#define UPIPE_HELPER_UPROBE(STRUCTURE, UREFCOUNT, UPROBE, THROW)        \
UBASE_FROM_TO(STRUCTURE, uprobe, UPROBE, UPROBE)                        \
/** @internal @This catches events coming from the inner pipe,          \
 * calls the THROW function if any and attaches them to the super pipe  \
 * if needed.                                                           \
 *                                                                      \
 * @param uprobe pointer to the probe in STRUCTURE                      \
 * @param inner pointer to the inner pipe                               \
 * @param event event triggered by the inner pipe                       \
 * @param args arguments of the event                                   \
 * @return an error code                                                \
 */                                                                     \
static UBASE_UNUSED int                                                 \
STRUCTURE##_throw_proxy_##UPROBE(struct uprobe *uprobe,                 \
                                 struct upipe *inner,                   \
                                 int event, va_list args)               \
{                                                                       \
    uprobe_throw_func throw_func = THROW;                               \
    if (throw_func)                                                     \
        return throw_func(uprobe, inner, event, args);                  \
    return upipe_throw_proxy(STRUCTURE##_to_upipe(                      \
                             STRUCTURE##_from_##UPROBE(uprobe)),        \
                             inner, event, args);                       \
}                                                                       \
                                                                        \
/** @internal @This initializes the private members for this helper.    \
 *                                                                      \
 * @param upipe description structure of the pipe                       \
 */                                                                     \
static void STRUCTURE##_init_##UPROBE(struct upipe *upipe)              \
{                                                                       \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                \
    struct uprobe *uprobe = STRUCTURE##_to_##UPROBE(s);                 \
    uprobe_init(uprobe, STRUCTURE##_throw_proxy_##UPROBE, NULL);        \
    uprobe->refcount = &s->UREFCOUNT;                                   \
}                                                                       \
/** @internal @This cleans up the private members for this helper.      \
 *                                                                      \
 * @param upipe description structure of the pipe                       \
 */                                                                     \
static void STRUCTURE##_clean_##UPROBE(struct upipe *upipe)             \
{                                                                       \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                \
    struct uprobe *uprobe = STRUCTURE##_to_##UPROBE(s);                 \
    uprobe_clean(uprobe);                                               \
}

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_UPIPE_HELPER_UPROBE_H_ */
