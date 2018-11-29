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
 * @short Upipe helper functions for input and output flow definitions
 */

#ifndef _UPIPE_UPIPE_HELPER_FLOW_DEF_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_FLOW_DEF_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/upipe.h>

/** @This declares six functions dealing with the management of flow definitions
 * in linear pipes.
 *
 * You must add two members to your private upipe structure, for instance:
 * @code
 *  struct uref *flow_def_input; //flow def exactly as it is input
 *  struct uref *flow_def_attr; //attributes added by the pipe to flow def
 * @end code
 *
 * You must also declare @ref #UPIPE_HELPER_UPIPE prior to using this macro.
 *
 * Supposing the name of your structure is upipe_foo, it declares:
 * @list
 * @item @code
 *  void upipe_foo_init_flow_def(struct upipe *upipe)
 * @end code
 * Typically called in your upipe_foo_alloc() function.
 *
 * @item @code
 *  struct uref *upipe_foo_alloc_flow_def_attr(struct upipe *upipe)
 * @end code
 * Called whenever you need to allocate a flow definition attributes packet.
 *
 * @item @code
 *  struct uref *upipe_foo_make_flow_def(struct upipe *upipe)
 * @end code
 * Builds and returns a new flow definition, deduced from the input flow
 * definition and the flow def attributes.
 *
 * @item @code
 *  bool upipe_foo_check_flow_def_attr(struct upipe *upipe)
 * @end code
 * Checks a new flow definitions attributes packet against the stored one.
 *
 * @item @code
 *  struct uref *upipe_foo_store_flow_def_attr(struct upipe *upipe,
 *                                             struct uref *flow_def_attr)
 * @end code
 * Called when new flow definition attributes are calculated (for instance on
 * a new global header). Returns the new flow definition of the pipe.
 *
 * @item @code
 *  struct uref *upipe_foo_store_flow_def_input(struct upipe *upipe,
 *                                              struct uref *flow_def_input)
 * @end code
 * Called when a new flow definition input is received. Returns the new flow
 * definition of the pipe.
 *
 * @item @code
 *  void upipe_foo_clean_flow_def(struct upipe *upipe)
 * @end code
 * Typically called from your upipe_foo_free() function.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure
 * @param FLOW_DEF_INPUT name of the @tt {struct uref *} field of your
 * private upipe structure, pointing to the input flow definition
 * @param FLOW_DEF_ATTR name of the @tt {struct uref *} field of your
 * private upipe structure, pointing to the flow definition attributes
 */
#define UPIPE_HELPER_FLOW_DEF(STRUCTURE, FLOW_DEF_INPUT, FLOW_DEF_ATTR)     \
/** @internal @This initializes the private members for this helper.        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_init_flow_def(struct upipe *upipe)                  \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    s->FLOW_DEF_ATTR = s->FLOW_DEF_INPUT = NULL;                            \
}                                                                           \
/** @internal @This allocates a flow def attributes uref, from the flow     \
 * def input.                                                               \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @return a pointer to a flow def attribute                                \
 */                                                                         \
static UBASE_UNUSED struct uref *                                           \
    STRUCTURE##_alloc_flow_def_attr(struct upipe *upipe)                    \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    if (s->FLOW_DEF_INPUT == NULL)                                          \
        return NULL;                                                        \
    return uref_sibling_alloc_control(s->FLOW_DEF_INPUT);                   \
}                                                                           \
/** @internal @This builds a new flow definition packet from the input      \
 * flow definition, and flow definition attributes.                         \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @return new flow definition                                              \
 */                                                                         \
static struct uref *STRUCTURE##_make_flow_def(struct upipe *upipe)          \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    if (s->FLOW_DEF_INPUT == NULL)                                          \
        return NULL;                                                        \
    struct uref *flow_def = uref_dup(s->FLOW_DEF_INPUT);                    \
    if (unlikely(flow_def == NULL))                                         \
        return NULL;                                                        \
    uref_attr_import(flow_def, s->FLOW_DEF_ATTR);                           \
    return flow_def;                                                        \
}                                                                           \
/** @internal @This checks a flow definition attributes packet against the  \
 * stored flow def attributes uref.                                         \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param flow_def_attr new flow def attributes packet                      \
 * @return false if the flow def attributes packets are different           \
 */                                                                         \
static UBASE_UNUSED bool                                                    \
    STRUCTURE##_check_flow_def_attr(struct upipe *upipe,                    \
                                    struct uref *flow_def_attr)             \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    return s->FLOW_DEF_ATTR != NULL &&                                      \
           !udict_cmp(s->FLOW_DEF_ATTR->udict, flow_def_attr->udict);       \
}                                                                           \
/** @internal @This stores a flow def attributes uref, and returns the new  \
 * flow definition.                                                         \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param flow_def_attr flow def attributes uref                            \
 * @return new flow definition                                              \
 */                                                                         \
static UBASE_UNUSED inline struct uref *                                    \
    STRUCTURE##_store_flow_def_attr(struct upipe *upipe,                    \
                                    struct uref *flow_def_attr)             \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    if (s->FLOW_DEF_ATTR != NULL)                                           \
        uref_free(s->FLOW_DEF_ATTR);                                        \
    s->FLOW_DEF_ATTR = flow_def_attr;                                       \
    return STRUCTURE##_make_flow_def(upipe);                                \
}                                                                           \
/** @internal @This stores a flow def input uref, and returns the new flow  \
 * definition.                                                              \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param flow_def_attr flow def attributes uref                            \
 * @return new flow definition or NULL                                      \
 */                                                                         \
static struct uref *                                                        \
    STRUCTURE##_store_flow_def_input(struct upipe *upipe,                   \
                                     struct uref *flow_def_input)           \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    if (s->FLOW_DEF_INPUT != NULL)                                          \
        uref_free(s->FLOW_DEF_INPUT);                                       \
    s->FLOW_DEF_INPUT = flow_def_input;                                     \
    if (s->FLOW_DEF_ATTR == NULL)                                           \
        return NULL;                                                        \
    return STRUCTURE##_make_flow_def(upipe);                                \
}                                                                           \
/** @internal @This cleans up to private members for this helper.           \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_clean_flow_def(struct upipe *upipe)                 \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    if (s->FLOW_DEF_ATTR != NULL)                                           \
        uref_free(s->FLOW_DEF_ATTR);                                        \
    if (s->FLOW_DEF_INPUT != NULL)                                          \
        uref_free(s->FLOW_DEF_INPUT);                                       \
}

#ifdef __cplusplus
}
#endif
#endif
