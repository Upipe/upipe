/*****************************************************************************
 * upipe_helper_split_outputs.h: upipe helper functions for split outputs
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

#ifndef _UPIPE_UPIPE_HELPER_SPLIT_OUTPUTS_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_SPLIT_OUTPUTS_H_

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe.h>

#include <stdbool.h>
#include <string.h>
#include <assert.h>

/** @This declares 11 functions dealing with the output-specific substructure
 * of a split pipe. A split pipe allows to configure one output per flow,
 * and each output is stored in a pipe-allocated substructure.
 *
 * You must add five members to your private output-specific structure, for
 * instance:
 * @code
 *  struct uchain uchain;
 *  struct upipe *output;
 *  char *flow_suffix;
 *  struct uref *flow_def:
 *  bool flow_def_sent;
 * @end code
 *
 * You must also declare @ref #UPIPE_HELPER_UREF_MGR prior to using this macro.
 *
 * Supposing the name of your structure is upipe_foo, and the output
 * substructure is upipe_foo_output, it declares:
 * @list
 * @item @code
 *  struct uchain *upipe_foo_output_to_uchain(struct upipe_foo_output *output)
 * @end code
 * Used by @ref #UPIPE_HELPER_SPLIT_OUTPUTS for chained lists.
 *
 * @item @code
 *  struct upipe_foo_output *upipe_foo_output_from_uchain(struct uchain *uchain)
 * @end code
 * Used by @ref #UPIPE_HELPER_SPLIT_OUTPUTS for chained lists.
 *
 * @item @code
 *  bool upipe_foo_output_match(struct upipe_foo_output *output,
 *                              const char *flow_suffix)
 * @end code
 * Returns true if an output matches with a flow suffix. Used by
 * @ref #UPIPE_HELPER_SPLIT_OUTPUTS for list enumeration.
 *
 * @item @code
 *  bool upipe_foo_output_init(struct upipe *upipe,
 *                             struct upipe_foo_output *output,
 *                             const char *flow_suffix)
 * @end code
 * Typically called in your upipe_foo_output_alloc() function. Returns false in
 * case of allocation error.
 *
 * @item @code
 *  void upipe_foo_output_flow_delete(struct upipe *upipe,
 *                                    struct upipe_foo_output *output)
 * @end code
 * Not normally called from your functions. It sends a flow deletion packet
 * to the designated output, and is used by the other declared functions.
 *
 * @item @code
 *  void upipe_foo_output_flow_definition(struct upipe *upipe,
 *                                        struct upipe_foo_output *output)
 * @end code
 * Not normally called from your functions. It sends a flow definition packet
 * to the designated output, and is used by the other declared functions.
 *
 * @item @code
 *  void upipe_foo_output_output(struct upipe *upipe,
 *                               struct upipe_foo_output *output,
 *                               struct uref *uref)
 * @end code
 * Called whenever you need to send a packet to an output. It takes care
 * of sending the flow definition if necessary. It is used by
 * @ref #UPIPE_HELPER_SPLIT_OUTPUTS.
 *
 * @item @code
 *  void upipe_foo_output_set_flow_def(struct upipe *upipe,
 *                                     struct upipe_foo_output *output,
 *                                     struct uref *flow_def)
 * @end code
 * Called whenever you change the flow definition on this output.
 *
 * @item @code
 *  bool upipe_foo_output_get_output(struct upipe *upipe,
 *                                   struct upipe_foo_output *output,
 *                                   struct upipe **p)
 * @end code
 * Used by @ref #UPIPE_HELPER_SPLIT_OUTPUTS.
 *
 * @item @code
 *  bool upipe_foo_output_set_output(struct upipe *upipe,
 *                                   struct upipe_foo_output *output,
 *                                   struct upipe *o)
 * @end code
 * Used by @ref #UPIPE_HELPER_SPLIT_OUTPUTS.
 *
 * @item @code
 *  void upipe_foo_output_clean(struct upipe *upipe,
 *                              struct upipe_foo_output *output)
 * @end code
 * Typically called from your upipe_foo_output_free() function.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure 
 * @param SUBSTRUCT name of the substructure that contains a specific output
 * @param UCHAIN name of the @tt {struct uchain} field of the substructure
 * @param OUTPUT name of the @tt {struct upipe *} field of the substructure
 * @param FLOW_SUFFIX name of the @tt {char *} field of the substructure
 * @param FLOW_DEF name of the @tt {struct uref *} field of the substructure
 * @param FLOW_DEF_SENT name of the @tt {bool} field of the substructure
 * @param UREF_MGR name of the @tt{struct uref_mgr *} field of
 * your private upipe structure, declared in @ref #UPIPE_HELPER_UREF_MGR
 */
#define UPIPE_HELPER_SPLIT_OUTPUT(STRUCTURE, SUBSTRUCT, UCHAIN, OUTPUT,     \
                                  FLOW_SUFFIX, FLOW_DEF, FLOW_DEF_SENT,     \
                                  UREF_MGR)                                 \
/** @internal @This returns the uchain utility structure.                   \
 *                                                                          \
 * @param s pointer to the output-specific substructure                     \
 * @return pointer to the uchain utility structure                          \
 */                                                                         \
static inline struct uchain *SUBSTRUCT##_to_uchain(struct SUBSTRUCT *s)     \
{                                                                           \
    return &s->UCHAIN;                                                      \
}                                                                           \
/** @internal @This returns the private output-specific substructure.       \
 *                                                                          \
 * @param u uchain utility structure                                        \
 * @return pointer to the private STRUCTURE structure                       \
 */                                                                         \
static inline struct SUBSTRUCT *SUBSTRUCT##_from_uchain(struct uchain *u)   \
{                                                                           \
    return container_of(u, struct SUBSTRUCT, UCHAIN);                       \
}                                                                           \
/** @internal @This checks if an output-specific substructure matches       \
 * a given flow suffix.                                                     \
 *                                                                          \
 * @param output pointer to output-specific substructure                    \
 * @param flow_suffix flow suffix                                           \
 * @return true if the substructure matches                                 \
 */                                                                         \
static inline bool SUBSTRUCT##_match(struct SUBSTRUCT *output,              \
                                     const char *flow_suffix)               \
{                                                                           \
    assert(output != NULL);                                                 \
    assert(flow_suffix != NULL);                                            \
    return !strcmp(output->FLOW_SUFFIX, flow_suffix);                       \
}                                                                           \
/** @internal @This initializes a new output-specific substructure.         \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param output pointer to output-specific substructure                    \
 * @param flow_suffix flow suffix                                           \
 * @return false in case of allocation failure                              \
 */                                                                         \
static bool SUBSTRUCT##_init(struct upipe *upipe, struct SUBSTRUCT *output, \
                             const char *flow_suffix)                       \
{                                                                           \
    assert(output != NULL);                                                 \
    assert(flow_suffix != NULL);                                            \
    uchain_init(&output->UCHAIN);                                           \
    output->FLOW_SUFFIX = strdup(flow_suffix);                              \
    if (unlikely(output->FLOW_SUFFIX == NULL))                              \
        return false;                                                       \
    output->OUTPUT = NULL;                                                  \
    output->FLOW_DEF = NULL;                                                \
    output->FLOW_DEF_SENT = false;                                          \
    return true;                                                            \
}                                                                           \
/** @This outputs a flow deletion control packet on an output substructure. \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param output pointer to output-specific substructure                    \
 */                                                                         \
static void SUBSTRUCT##_flow_delete(struct upipe *upipe,                    \
                                    struct SUBSTRUCT *output)               \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    const char *flow_name;                                                  \
    output->FLOW_DEF_SENT = false;                                          \
    if (unlikely(STRUCTURE->UREF_MGR == NULL ||                             \
                 output->FLOW_DEF == NULL ||                                \
                 !uref_flow_get_name(output->FLOW_DEF, &flow_name)))        \
        return;                                                             \
    struct uref *uref = uref_flow_alloc_delete(STRUCTURE->UREF_MGR,         \
                                               flow_name);                  \
    if (unlikely(uref == NULL)) {                                           \
        ulog_aerror(upipe->ulog);                                           \
        upipe_throw_aerror(upipe);                                          \
        return;                                                             \
    }                                                                       \
    upipe_input(output->OUTPUT, uref);                                      \
}                                                                           \
/** @internal @This outputs a flow definition control packet on an          \
 * output substructure.                                                     \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param output pointer to output-specific substructure                    \
 */                                                                         \
static void SUBSTRUCT##_flow_definition(struct upipe *upipe,                \
                                        struct SUBSTRUCT *output)           \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    if (unlikely(STRUCTURE->UREF_MGR == NULL ||                             \
                 output->FLOW_DEF == NULL))                                 \
        return;                                                             \
    struct uref *uref = uref_dup(STRUCTURE->UREF_MGR, output->FLOW_DEF);    \
    if (unlikely(uref == NULL)) {                                           \
        ulog_aerror(upipe->ulog);                                           \
        upipe_throw_aerror(upipe);                                          \
        return;                                                             \
    }                                                                       \
    upipe_input(output->OUTPUT, uref);                                      \
    output->FLOW_DEF_SENT = true;                                           \
}                                                                           \
/** @internal @This sends a uref to the output of a substructure.           \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param output pointer to output-specific substructure                    \
 * @param uref uref structure to send                                       \
 */                                                                         \
static void SUBSTRUCT##_output(struct upipe *upipe,                         \
                               struct SUBSTRUCT *output, struct uref *uref) \
{                                                                           \
    if (unlikely(!output->FLOW_DEF_SENT))                                   \
        SUBSTRUCT##_flow_definition(upipe, output);                         \
    if (unlikely(!output->FLOW_DEF_SENT)) {                                 \
        uref_release(uref);                                                 \
        return;                                                             \
    }                                                                       \
                                                                            \
    const char *flow_name;                                                  \
    if (unlikely(!output->FLOW_DEF_SENT ||                                  \
                 !uref_flow_get_name(output->FLOW_DEF, &flow_name) ||       \
                 !uref_flow_set_name(&uref, flow_name))) {                  \
        uref_release(uref);                                                 \
        ulog_aerror(upipe->ulog);                                           \
        upipe_throw_aerror(upipe);                                          \
        return;                                                             \
    }                                                                       \
    upipe_input(output->OUTPUT, uref);                                      \
}                                                                           \
/** @internal @This sets the flow definition to use on the output of a      \
 * substructure. If set to NULL, also output a flow deletion packet.        \
 * Otherwise, schedule a flow definition packet next time a packet must be  \
 * output.                                                                  \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param output pointer to output-specific substructure                    \
 * @param flow_def control packet describing the output                     \
 */                                                                         \
static void SUBSTRUCT##_set_flow_def(struct upipe *upipe,                   \
                                     struct SUBSTRUCT *output,              \
                                     struct uref *flow_def)                 \
{                                                                           \
    if (unlikely(output->FLOW_DEF != NULL)) {                               \
        if (unlikely(output->FLOW_DEF_SENT && flow_def == NULL))            \
            SUBSTRUCT##_flow_delete(upipe, output);                         \
        uref_release(output->FLOW_DEF);                                     \
        output->FLOW_DEF_SENT = false;                                      \
    }                                                                       \
    output->FLOW_DEF = flow_def;                                            \
}                                                                           \
/** @internal @This handles the get_output control command on a             \
 * substructure.                                                            \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param output pointer to output-specific substructure                    \
 * @param p filled in with the output                                       \
 * @return false in case of error                                           \
 */                                                                         \
static bool SUBSTRUCT##_get_output(struct upipe *upipe,                     \
                                   struct SUBSTRUCT *output,                \
                                   struct upipe **p)                        \
{                                                                           \
    assert(p != NULL);                                                      \
    *p = output->OUTPUT;                                                    \
    return true;                                                            \
}                                                                           \
/** @internal @This handles the set_output control command on a             \
 * substructure, and properly deletes and replays flows on old and new      \
 * outputs.                                                                 \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param output pointer to output-specific substructure                    \
 * @param o new output pipe                                                 \
 * @return false in case of error                                           \
 */                                                                         \
static bool SUBSTRUCT##_set_output(struct upipe *upipe,                     \
                                   struct SUBSTRUCT *output,                \
                                   struct upipe *o)                         \
{                                                                           \
    if (unlikely(output->OUTPUT != NULL)) {                                 \
        if (likely(output->FLOW_DEF_SENT))                                  \
            SUBSTRUCT##_flow_delete(upipe, output);                         \
        upipe_release(output->OUTPUT);                                      \
    }                                                                       \
                                                                            \
    output->OUTPUT = o;                                                     \
    if (likely(o != NULL))                                                  \
        upipe_use(o);                                                       \
    return true;                                                            \
}                                                                           \
/** @internal @This cleans up an output-specific substructure.              \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param output substructure to clean                                      \
 */                                                                         \
static void SUBSTRUCT##_clean(struct upipe *upipe, struct SUBSTRUCT *output)\
{                                                                           \
    free(output->FLOW_SUFFIX);                                              \
    if (likely(output->OUTPUT != NULL)) {                                   \
        if (likely(output->FLOW_DEF_SENT))                                  \
            SUBSTRUCT##_flow_delete(upipe, output);                         \
        upipe_release(output->OUTPUT);                                      \
    }                                                                       \
    if (likely(output->FLOW_DEF != NULL))                                   \
        uref_release(output->FLOW_DEF);                                     \
}

/** @This declares nine functions dealing with the outputs list of a split pipe.
 *
 * You must add one member to your private upipe structure, for instance:
 * @code
 *  struct ulist outputs;
 * @end code
 *
 * You must also declare @ref #UPIPE_HELPER_SPLIT_OUTPUT prior to using this
 * macro, and its substructure describing an output.
 *
 * Supposing the name of your structure is upipe_foo, and the output
 * substructure is upipe_foo_output, it declares:
 * @list
 * @item @code
 *  void upipe_foo_init_outputs(struct upipe *upipe)
 * @end code
 * Typically called in your upipe_foo_alloc() function.
 *
 * @item @code
 *  struct upipe_foo_output *upipe_foo_find_output(struct upipe *upipe,
 *                                                 const char *flow_suffix)
 * @end code
 * Finds an output given by its flow suffix.
 *
 * @item @code
 *  bool upipe_foo_delete_output(struct upipe *upipe, const char *flow_suffix)
 * @end code
 * Deletes an output given by its flow suffix, and returns true if it was found.
 *
 * @item @code
 *  struct upipe_foo_output *upipe_foo_add_output(struct upipe *upipe,
 *                                                const char *flow_suffix)
 * @end code
 * Add a new output for a flow suffix.
 *
 * @item @code
 *  void upipe_foo_output(struct upipe *upipe, struct uref *uref,
 *                        const char *flow_suffix)
 * @end code
 * Called whenever you need to send a packet to an output.
 *
 * @item @code
 *  void upipe_foo_output_va(struct upipe *upipe, struct uref *uref,
 *                           const char *format, ...)
 * @end code
 * Same function as upipe_foo_output(), with printf-style suffix generation.
 *
 * @item @code
 *  bool upipe_foo_get_output(struct upipe *upipe, struct upipe **p,
 *                            const char *flow_suffix)
 * @end code
 * Typically called from your upipe_foo_control() handler, such as:
 * @code
 *  case UPIPE_SPLIT_GET_OUTPUT: {
 *      struct upipe **p = va_arg(args, struct upipe **);
 *      const char *flow_suffix = va_arg(args, const char *);
 *      return upipe_foo_get_output(upipe, p, flow_suffix);
 *  }
 * @end code
 *
 * @item @code
 *  bool upipe_foo_set_output(struct upipe *upipe, struct upipe *o,
 *                            const char *flow_suffix)
 * @end code
 * Typically called from your upipe_foo_control() handler, such as:
 * @code
 *  case UPIPE_SPLIT_SET_OUTPUT: {
 *      struct upipe *o = va_arg(args, struct upipe *);
 *      const char *flow_suffix = va_arg(args, const char *);
 *      return upipe_foo_set_output(upipe, o, flow_suffix);
 *  }
 * @end code
 *
 * @item @code
 *  void upipe_foo_clean_outputs(struct upipe *upipe,
                void (*output_free)(struct upipe *, struct upipe_foo_output *))
 * @end code
 * Typically called from your upipe_foo_free() function. You must pass a
 * function pointer releasing upipe_foo_output substructures.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure 
 * @param LIST name of the @tt {struct ulist} field of
 * your private upipe structure
 * @param SUBSTRUCT name of the substructure that contains a specific output,
 * declared in @ref #UPIPE_HELPER_SPLIT_OUTPUT
 */
#define UPIPE_HELPER_SPLIT_OUTPUTS(STRUCTURE, LIST, SUBSTRUCT)              \
/** @internal @This initializes the private members for this helper.        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_init_outputs(struct upipe *upipe)                   \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    ulist_init(&STRUCTURE->LIST);                                           \
}                                                                           \
/** @internal @This returns the output substructure for a given flow suffix.\
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param flow_suffix flow suffix                                           \
 * @return pointer to the output substructure, or NULL if not found         \
 */                                                                         \
static struct SUBSTRUCT *STRUCTURE##_find_output(struct upipe *upipe,       \
                                                 const char *flow_suffix)   \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    struct uchain *uchain;                                                  \
    ulist_foreach (&STRUCTURE->LIST, uchain) {                              \
        struct SUBSTRUCT *output = SUBSTRUCT##_from_uchain(uchain);         \
        if (unlikely(SUBSTRUCT##_match(output, flow_suffix)))               \
            return output;                                                  \
    }                                                                       \
    return NULL;                                                            \
}                                                                           \
/** @internal @This deletes the output for a given flow suffix.             \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param flow_suffix flow suffix                                           \
 * @param output_free function to call to free an output substructure       \
 * @return true if the output was found and deleted                         \
 */                                                                         \
static bool STRUCTURE##_delete_output(struct upipe *upipe,                  \
                const char *flow_suffix,                                    \
                void (*output_free)(struct upipe *, struct SUBSTRUCT *))    \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    struct uchain *uchain;                                                  \
    ulist_delete_foreach (&STRUCTURE->LIST, uchain) {                       \
        struct SUBSTRUCT *output = SUBSTRUCT##_from_uchain(uchain);         \
        if (unlikely(SUBSTRUCT##_match(output, flow_suffix))) {             \
            ulist_delete(&STRUCTURE->LIST, uchain);                         \
            output_free(upipe, output);                                     \
            return true;                                                    \
        }                                                                   \
    }                                                                       \
    return false;                                                           \
}                                                                           \
/** @internal @This adds a new output.                                      \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param output pointer to the output to add to the list                   \
 */                                                                         \
static void STRUCTURE##_add_output(struct upipe *upipe,                     \
                                   struct SUBSTRUCT *output)                \
{                                                                           \
    assert(output != NULL);                                                 \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    ulist_add(&STRUCTURE->LIST, SUBSTRUCT##_to_uchain(output));             \
}                                                                           \
/** @internal @This sends a uref to the output for the given flow suffix.   \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param uref uref structure to send                                       \
 * @param flow_suffix flow suffix to add                                    \
 * @return false in case the uref couldn't be sent                          \
 */                                                                         \
static void STRUCTURE##_output(struct upipe *upipe, struct uref *uref,      \
                               const char *flow_suffix)                     \
{                                                                           \
    struct SUBSTRUCT *SUBSTRUCT = STRUCTURE##_find_output(upipe,            \
                                                          flow_suffix);     \
    if (unlikely(SUBSTRUCT == NULL)) {                                      \
        uref_release(uref);                                                 \
        return;                                                             \
    }                                                                       \
    SUBSTRUCT##_output(upipe, SUBSTRUCT, uref);                             \
}                                                                           \
/** @internal @This gets a pointer to the output for the given flow suffix. \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param flow_suffix flow suffix                                           \
 * @param p filled in with the output                                       \
 * @return false in case of error                                           \
 */                                                                         \
static bool STRUCTURE##_get_output(struct upipe *upipe, struct upipe **p,   \
                                   const char *flow_suffix)                 \
{                                                                           \
    assert(p != NULL);                                                      \
    struct SUBSTRUCT *SUBSTRUCT = STRUCTURE##_find_output(upipe,            \
                                                          flow_suffix);     \
    if (unlikely(SUBSTRUCT == NULL))                                        \
        return false;                                                       \
    return SUBSTRUCT##_get_output(upipe, SUBSTRUCT, p);                     \
}                                                                           \
/** @internal @This sets the output for the given flow suffix.              \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param flow_suffix flow suffix                                           \
 * @param o new output                                                      \
 * @return false in case of error                                           \
 */                                                                         \
static bool STRUCTURE##_set_output(struct upipe *upipe, struct upipe *o,    \
                                   const char *flow_suffix)                 \
{                                                                           \
    struct SUBSTRUCT *SUBSTRUCT = STRUCTURE##_find_output(upipe,            \
                                                          flow_suffix);     \
    if (unlikely(SUBSTRUCT == NULL))                                        \
        return false;                                                       \
    return SUBSTRUCT##_set_output(upipe, SUBSTRUCT, o);                     \
}                                                                           \
/** @internal @This cleans up the private members for this helper.          \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param output_free function to call to free an output substructure       \
 */                                                                         \
static void STRUCTURE##_clean_outputs(struct upipe *upipe,                  \
                void (*output_free)(struct upipe *, struct SUBSTRUCT *))    \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    struct uchain *uchain;                                                  \
    ulist_delete_foreach (&STRUCTURE->LIST, uchain) {                       \
        struct SUBSTRUCT *output = SUBSTRUCT##_from_uchain(uchain);         \
        ulist_delete(&STRUCTURE->LIST, uchain);                             \
        output_free(upipe, output);                                         \
    }                                                                       \
}

#endif
