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
 * @short uprobe helper functions to allocate probes
 */

#ifndef _UPIPE_UPROBE_HELPER_ALLOC_H_
/** @hidden */
#define _UPIPE_UPROBE_HELPER_ALLOC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>
#include <upipe/urefcount.h>

/** @This declares two functions to allocate probes.
 *
 * Supposing the name of your structure is uprobe_foo, it declares:
 * @list
 * @item @code
 *  struct uprobe_foo_alloc;
 * @end code
 * A structure containing the probe and a urefcount.
 *
 * @item @code
 *  struct uprobe *uprobe_foo_alloc(...)
 * @end code
 * A wrapper to uprobe_foo_init() which allocates the structure before.
 *
 * @item @code
 *  static void uprobe_foo_free(struct urefcount *urefcount)
 * @end code
 * Called when the refcount goes down to zero, to deallocate the structure.
 * @end list
 *
 * Please note that you must declare uprobe_foo_init and uprobe_foo_clean
 * before this helper, and the macros ARGS_DECL and ARGS must be filled
 * respectively with the declaration of arguments of uprobe_foo_alloc, and
 * the use of them in the call to uprobe_foo_init.
 *
 * @param STRUCTURE name of your public uprobe super-structure
 * your private uprobe structure
 */
#define UPROBE_HELPER_ALLOC(STRUCTURE)                                      \
/** @This is a super-set of the STRUCTURE with additional urefcount. */     \
struct STRUCTURE##_alloc {                                                  \
    /** refcount management structure */                                    \
    struct urefcount urefcount;                                             \
    /** main structure */                                                   \
    struct STRUCTURE STRUCTURE;                                             \
};                                                                          \
UBASE_FROM_TO(STRUCTURE##_alloc, STRUCTURE, STRUCTURE, STRUCTURE)           \
UBASE_FROM_TO(STRUCTURE##_alloc, urefcount, urefcount, urefcount)           \
/** @internal @This frees the allocated probe.                              \
 *                                                                          \
 * @param urefcount pointer to urefcount structure                          \
 */                                                                         \
static void STRUCTURE##_free(struct urefcount *urefcount)                   \
{                                                                           \
    struct STRUCTURE##_alloc *s =                                           \
        STRUCTURE##_alloc_from_urefcount(urefcount);                        \
    STRUCTURE##_clean(STRUCTURE##_alloc_to_##STRUCTURE(s));                 \
    free(s);                                                                \
}                                                                           \
/** @This allocates a probe with a dedicated urefcount.                     \
 *                                                                          \
 * @return pointer to probe                                                 \
 */                                                                         \
struct uprobe *STRUCTURE##_alloc(ARGS_DECL)                                 \
{                                                                           \
    struct STRUCTURE##_alloc *s = malloc(sizeof(struct STRUCTURE##_alloc)); \
    if (unlikely(s == NULL))                                                \
        return NULL;                                                        \
    struct uprobe *uprobe =                                                 \
        STRUCTURE##_init(STRUCTURE##_alloc_to_##STRUCTURE(s), ARGS);        \
    if (unlikely(uprobe == NULL)) {                                         \
        free(s);                                                            \
        return NULL;                                                        \
    }                                                                       \
    urefcount_init(STRUCTURE##_alloc_to_urefcount(s), STRUCTURE##_free);    \
    uprobe->refcount = STRUCTURE##_alloc_to_urefcount(s);                   \
    return uprobe;                                                          \
}

#ifdef __cplusplus
}
#endif
#endif
