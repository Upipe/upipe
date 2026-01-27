/*
 * Copyright (C) 2012-2014 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe helper functions for upump manager
 */

#ifndef _UPIPE_UPIPE_HELPER_UPUMP_MGR_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_UPUMP_MGR_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/ubase.h"
#include "upipe/upump.h"
#include "upipe/upipe.h"

#include <stdbool.h>

/** @This declares three functions dealing with the upump manager.
 *
 * You must add one pointer to your private upipe structure, for instance:
 * @code
 *  struct upump_mgr *upump_mgr;
 * @end code
 *
 * You must also declare @ref #UPIPE_HELPER_UPIPE prior to using this macro.
 *
 * Supposing the name of your structure is upipe_foo, it declares:
 * @list
 * @item @code
 *  void upipe_foo_init_upump_mgr(struct upipe *upipe)
 * @end code
 * Typically called in your upipe_foo_alloc() function.
 *
 * @item @code
 *  int upipe_foo_attach_upump_mgr(struct upipe *upipe)
 * @end code
 * Typically called from your upipe_foo_control() handler, such as:
 * @code
 *  case UPIPE_ATTACH_UPUMP_MGR: {
 *      return upipe_foo_attach_upump_mgr(upipe);
 *  }
 * @end code
 *
 * @item @code
 *  int upipe_foo_check_upump_mgr(struct upipe *upipe)
 * @end code
 * Checks if the upump manager is available, and asks for it otherwise.
 *
 * @item @code
 *  void upipe_foo_clean_upump_mgr(struct upipe *upipe)
 * @end code
 * Typically called from your upipe_foo_free() function.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure
 * @param UPUMP_MGR name of the @tt {struct upump_mgr *} field of
 * your private upipe structure
 */
#define UPIPE_HELPER_UPUMP_MGR(STRUCTURE, UPUMP_MGR)                        \
/** @internal @This initializes the private members for this helper.        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_init_upump_mgr(struct upipe *upipe)                 \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    s->UPUMP_MGR = NULL;                                                    \
}                                                                           \
/** @internal @This sends a probe to attach a uref manager.                 \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @return an error code                                                    \
 */                                                                         \
static int STRUCTURE##_attach_upump_mgr(struct upipe *upipe)                \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    upump_mgr_release(s->UPUMP_MGR);                                        \
    s->UPUMP_MGR = NULL;                                                    \
    return upipe_throw_need_upump_mgr(upipe, &s->UPUMP_MGR);                \
}                                                                           \
/** @internal @This checks if the upump manager is available, and asks      \
 * for it otherwise.                                                        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @return an error code                                                    \
 */                                                                         \
static UBASE_UNUSED int STRUCTURE##_check_upump_mgr(struct upipe *upipe)    \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    if (unlikely(s->UPUMP_MGR == NULL))                                     \
        return upipe_throw_need_upump_mgr(upipe, &s->UPUMP_MGR);            \
    return UBASE_ERR_NONE;                                                  \
}                                                                           \
/** @internal @This cleans up the private members for this helper.          \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_clean_upump_mgr(struct upipe *upipe)                \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    upump_mgr_release(s->UPUMP_MGR);                                        \
}

#ifdef __cplusplus
}
#endif
#endif
