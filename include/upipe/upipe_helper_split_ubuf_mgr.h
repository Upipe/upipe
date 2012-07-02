/*****************************************************************************
 * upipe_helper_split_ubuf_mgr.h: upipe helper functions for split outputs
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

#ifndef _UPIPE_UPIPE_HELPER_SPLIT_UBUF_MGR_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_SPLIT_UBUF_MGR_H_

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe.h>

#include <stdbool.h>
#include <string.h>
#include <assert.h>

/** @This declares four functions dealing with the ubuf_mgr field of an
 * output-specific substructure of a split pipe.
 *
 * You must add one pointer to your private output-specific structure, for
 * instance:
 * @code
 *  struct ubuf_mgr *ubuf_mgr;
 * @end code
 *
 * You must also declare @ref #UPIPE_HELPER_SPLIT_OUTPUT prior to using this
 * macro.
 *
 * Supposing the name of your structure is upipe_foo, and the output
 * substructure is upipe_foo_output, it declares:
 * @list
 * @item @code
 *  void upipe_foo_output_init_ubuf_mgr(struct upipe *upipe,
 *                                      struct upipe_foo_output *output)
 * @end code
 * Typically called in your upipe_foo_output_alloc() function.
 *
 * @item @code
 *  bool upipe_foo_output_get_ubuf_mgr(struct upipe *upipe,
 *                                     struct upipe_foo_output *output,
 *                                     struct ubuf_mgr **p)
 * @end code
 * Used by @ref #UPIPE_HELPER_SPLIT_UBUF_MGRS.
 *
 * @item @code
 *  bool upipe_foo_output_set_ubuf_mgr(struct upipe *upipe,
 *                                     struct upipe_foo_output *output,
 *                                     struct ubuf_mgr *ubuf_mgr)
 * @end code
 * Used by @ref #UPIPE_HELPER_SPLIT_UBUF_MGRS.
 *
 * @item @code
 *  void upipe_foo_output_clean_ubuf_mgr(struct upipe *upipe,
 *                                       struct upipe_foo_output *output)
 * @end code
 * Typically called from your upipe_foo_output_free() function.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure 
 * @param SUBSTRUCT name of the substructure that contains a specific output,
 * declared in @ref #UPIPE_HELPER_SPLIT_OUTPUT
 * @param UBUF_MGR name of the @tt{struct ubuf_mgr *} field of
 * the output-specific substructure
 */
#define UPIPE_HELPER_SPLIT_UBUF_MGR(STRUCTURE, SUBSTRUCT, UBUF_MGR)         \
/** @internal @This initializes the ubuf_mgr field of a new output-specific \
 * substructure.                                                            \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param output pointer to output-specific substructure                    \
 * @param flow_suffix flow suffix                                           \
 * @return false in case of allocation failure                              \
 */                                                                         \
static void SUBSTRUCT##_init_ubuf_mgr(struct upipe *upipe,                  \
                                      struct SUBSTRUCT *output)             \
{                                                                           \
    output->UBUF_MGR = NULL;                                                \
}                                                                           \
/** @internal @This handles the get_ubuf_mgr control command on a           \
 * substructure.                                                            \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param output pointer to output-specific substructure                    \
 * @param p filled in with the ubuf_mgr                                     \
 * @return false in case of error                                           \
 */                                                                         \
static bool SUBSTRUCT##_get_ubuf_mgr(struct upipe *upipe,                   \
                                     struct SUBSTRUCT *output,              \
                                     struct ubuf_mgr **p)                   \
{                                                                           \
    assert(p != NULL);                                                      \
    *p = output->UBUF_MGR;                                                  \
    return true;                                                            \
}                                                                           \
/** @internal @This handles the set_ubuf_mgr control command on a           \
 * substructure.                                                            \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param output pointer to output-specific substructure                    \
 * @param ubuf_mgr new ubuf management structure                            \
 * @return false in case of error                                           \
 */                                                                         \
static bool SUBSTRUCT##_set_ubuf_mgr(struct upipe *upipe,                   \
                                     struct SUBSTRUCT *output,              \
                                     struct ubuf_mgr *ubuf_mgr)             \
{                                                                           \
    if (unlikely(output->UBUF_MGR != NULL))                                 \
        ubuf_mgr_release(output->UBUF_MGR);                                 \
    output->UBUF_MGR = ubuf_mgr;                                            \
    if (likely(ubuf_mgr != NULL))                                           \
        ubuf_mgr_use(ubuf_mgr);                                             \
    return true;                                                            \
}                                                                           \
/** @internal @This cleans up the ubuf_mgr field of an output-specific      \
 * substructure.                                                            \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param output substructure to clean                                      \
 */                                                                         \
static void SUBSTRUCT##_clean_ubuf_mgr(struct upipe *upipe,                 \
                                       struct SUBSTRUCT *output)            \
{                                                                           \
    if (likely(output->UBUF_MGR != NULL))                                   \
        ubuf_mgr_release(output->UBUF_MGR);                                 \
}

/** @This declares two functions dealing with the outputs list of a split pipe,
 * and the associated ubuf mangers.
 *
 * You must declare @ref #UPIPE_HELPER_SPLIT_OUTPUTS and
 * @ref #UPIPE_HELPER_SPLIT_UBUF_MGR prior to using this
 * macro, and its substructure describing an output.
 *
 * Supposing the name of your structure is upipe_foo, and the output
 * substructure is upipe_foo_output, it declares:
 * @list
 * @item @code
 *  bool upipe_foo_get_ubuf_mgr(struct upipe *upipe, struct ubuf_mgr **p,
 *                              const char *flow_suffix)
 * @end code
 * Typically called from your upipe_foo_control() handler, such as:
 * @code
 *  case UPIPE_SPLIT_GET_UBUF_MGR: {
 *      struct ubuf_mgr **p = va_arg(args, struct ubuf_mgr **);
 *      const char *flow_suffix = va_arg(args, const char *);
 *      return upipe_foo_get_ubuf_mgr(upipe, p, flow_suffix);
 *  }
 * @end code
 *
 * @item @code
 *  bool upipe_foo_set_ubuf_mgr(struct upipe *upipe, struct ubuf_mgr *ubuf_mgr,
 *                              const char *flow_suffix)
 * @end code
 * Typically called from your upipe_foo_control() handler, such as:
 * @code
 *  case UPIPE_SPLIT_SET_UBUF_MGR: {
 *      struct ubuf_mgr *ubuf_mgr = va_arg(args, struct ubuf_mgr *);
 *      const char *flow_suffix = va_arg(args, const char *);
 *      return upipe_foo_set_ubuf_mgr(upipe, ubuf_mgr, flow_suffix);
 *  }
 * @end code
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure 
 * @param SUBSTRUCT name of the substructure that contains a specific output,
 * declared in @ref #UPIPE_HELPER_SPLIT_OUTPUT
 */
#define UPIPE_HELPER_SPLIT_UBUF_MGRS(STRUCTURE, SUBSTRUCT)                  \
/** @internal @This gets a pointer to the ubuf management structure for the \
 * given flow suffix.                                                       \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param p filled in with the ubuf management structure                    \
 * @param flow_suffix flow suffix                                           \
 * @return false in case of error                                           \
 */                                                                         \
static bool STRUCTURE##_get_ubuf_mgr(struct upipe *upipe,                   \
                                     struct ubuf_mgr **p,                   \
                                     const char *flow_suffix)               \
{                                                                           \
    assert(p != NULL);                                                      \
    struct SUBSTRUCT *SUBSTRUCT = STRUCTURE##_find_output(upipe,            \
                                                          flow_suffix);     \
    if (unlikely(SUBSTRUCT == NULL))                                        \
        return false;                                                       \
    return SUBSTRUCT##_get_ubuf_mgr(upipe, SUBSTRUCT, p);                   \
}                                                                           \
/** @internal @This sets the ubuf manager for the given flow suffix.        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param ubuf_mgr new ubuf management structure                            \
 * @param flow_suffix flow suffix                                           \
 * @return false in case of error                                           \
 */                                                                         \
static bool STRUCTURE##_set_ubuf_mgr(struct upipe *upipe,                   \
                                     struct ubuf_mgr *ubuf_mgr,             \
                                     const char *flow_suffix)               \
{                                                                           \
    struct SUBSTRUCT *SUBSTRUCT = STRUCTURE##_find_output(upipe,            \
                                                          flow_suffix);     \
    if (unlikely(SUBSTRUCT == NULL))                                        \
        return false;                                                       \
    return SUBSTRUCT##_set_ubuf_mgr(upipe, SUBSTRUCT, ubuf_mgr);            \
}

#endif
