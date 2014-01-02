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
 * @short Upipe helper functions for pipes taking an output flow in upipe_alloc
 */

#ifndef _UPIPE_UPIPE_HELPER_FLOW_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_FLOW_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe.h>

/** @This declares two functions dealing with the allocation of a pipe
 * requiring an output flow definition.
 *
 * You must declare @ref #UPIPE_HELPER_UPIPE prior to using this macro.
 *
 * Supposing the name of your structure is upipe_foo, it declares:
 * @list
 * @item @code
 *  struct upipe *upipe_foo_alloc_flow(struct upipe_mgr *mgr,
 *                                     struct uprobe *uprobe,
 *                                     uint32_t signature, va_list args,
 *                                     struct uref **flow_def_p)
 * @end code
 * Allocates and initializes the private structure upipe_foo, checks the flow
 * definition, and duplicates it to flow_def_p if not NULL.
 *
 * @item @code
 *  void upipe_foo_free_flow(struct upipe *upipe)
 * @end code
 * Frees the private structure upipe_foo.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure
 * @param EXPECTED_FLOW_DEF wanted prefix of the flow definition, or NULL
 */
#define UPIPE_HELPER_FLOW(STRUCTURE, EXPECTED_FLOW_DEF)                     \
/** @internal @This allocates and initializes the private structure, checks \
 * the flow definition, and duplicates it to flow_def_p.                    \
 *                                                                          \
 * @param mgr common management structure                                   \
 * @param uprobe structure used to raise events                             \
 * @param signature signature of the pipe allocator                         \
 * @param args optional arguments (flow definition)                         \
 * @param flow_def_p filled in with a duplicate of the flow definition      \
 * packet                                                                   \
 * @return pointer to allocated upipe, or NULL                              \
 */                                                                         \
static struct upipe *STRUCTURE##_alloc_flow(struct upipe_mgr *mgr,          \
                                            struct uprobe *uprobe,          \
                                            uint32_t signature,             \
                                            va_list args,                   \
                                            struct uref **flow_def_p)       \
{                                                                           \
    if (signature != UPIPE_FLOW_SIGNATURE)                                  \
        return NULL;                                                        \
    if (EXPECTED_FLOW_DEF != NULL || flow_def_p != NULL) {                  \
        struct uref *flow_def = va_arg(args, struct uref *);                \
        if (unlikely(flow_def == NULL))                                     \
            return NULL;                                                    \
        if (EXPECTED_FLOW_DEF != NULL) {                                    \
            const char *def;                                                \
            if (unlikely(!ubase_check(uref_flow_get_def(flow_def, &def)) || \
                         ubase_ncmp(def, EXPECTED_FLOW_DEF)))               \
                return NULL;                                                \
        }                                                                   \
        if (flow_def_p != NULL) {                                           \
            *flow_def_p = uref_dup(flow_def);                               \
            if (unlikely(*flow_def_p == NULL))                              \
                return NULL;                                                \
        }                                                                   \
    }                                                                       \
    struct STRUCTURE *s = malloc(sizeof(struct STRUCTURE));                 \
    if (unlikely(s == NULL)) {                                              \
        if (flow_def_p != NULL)                                             \
            uref_free(*flow_def_p);                                         \
        return NULL;                                                        \
    }                                                                       \
    struct upipe *upipe = STRUCTURE##_to_upipe(s);                          \
    upipe_init(upipe, mgr, uprobe);                                         \
    return upipe;                                                           \
}                                                                           \
/** @internal @This frees the private structure.                            \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_free_flow(struct upipe *upipe)                      \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    upipe_clean(upipe);                                                     \
    free(s);                                                                \
}

#ifdef __cplusplus
}
#endif
#endif
