/*
 * Copyright (C) 2013-2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe helper functions for pumps
 */

#ifndef _UPIPE_UPIPE_HELPER_UPUMP_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_UPUMP_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/ubase.h"
#include "upipe/upump.h"
#include "upipe/upipe.h"

#include <stdbool.h>

/** @This declares three functions dealing with a pump which we suppose serves
 * as a worker task for the pipe.
 *
 * You must add one pointer to your private upipe structure, for instance:
 * @code
 *  struct upump *upump;
 * @end code
 *
 * You must also declare @ref #UPIPE_HELPER_UPIPE prior to using this macro.
 * Also we suppose that you have a upump manager available in your structure.
 *
 * Supposing the name of your structure is upipe_foo, and the name of the
 * pointer is upump, it declares:
 * @list
 * @item @code
 *  void upipe_foo_init_upump(struct upipe *upipe)
 * @end code
 * Typically called in your upipe_foo_alloc() function.
 *
 * @item @code
 *  void upipe_foo_set_upump(struct upipe *upipe, struct upump *upump)
 * @end code
 * Called whenever you allocate or free the worker.
 *
 * @item @code
 *  void upipe_foo_wait_upump(struct upipe *upipe, uint64_t delay, upump_cb cb)
 * @end code
 * Creates a time upump waiting for the given delay, and calling the
 * callback cb.
 *
 * @item @code
 *  void upipe_foo_clean_upump(struct upipe *upipe)
 * @end code
 * Typically called from your upipe_foo_free() function.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure
 * @param UPUMP name of the @tt{struct upump *} field of
 * your private upipe structure
 * @param UPUMP_MGR name of the @tt{struct upump_mgr *} field of
 * your private upipe structure
 */
#define UPIPE_HELPER_UPUMP(STRUCTURE, UPUMP, UPUMP_MGR)                     \
/** @internal @This initializes the private members for this helper.        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_init_##UPUMP(struct upipe *upipe)                   \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    s->UPUMP = NULL;                                                        \
}                                                                           \
/** @internal @This sets the upump to use.                                  \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param upump upump structure to use                                      \
 */                                                                         \
static void STRUCTURE##_set_##UPUMP(struct upipe *upipe,                    \
                                    struct upump *upump)                    \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    if (unlikely(s->UPUMP != NULL)) {                                       \
        upump_stop(s->UPUMP);                                               \
        upump_free(s->UPUMP);                                               \
    }                                                                       \
    s->UPUMP = upump;                                                       \
}                                                                           \
/** @internal @This sets the upump to use.                                  \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param timeout time to wait before waking up                             \
 */                                                                         \
static void UBASE_UNUSED STRUCTURE##_wait_##UPUMP(struct upipe *upipe,      \
                                                  uint64_t timeout,         \
                                                  upump_cb cb)              \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    struct upump *watcher = upump_alloc_timer(s->UPUMP_MGR, cb, upipe,      \
                                              upipe->refcount, timeout, 0); \
    if (unlikely(watcher == NULL)) {                                        \
        upipe_err(upipe, "can't create watcher");                           \
        upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);                          \
    } else {                                                                \
        STRUCTURE##_set_##UPUMP(upipe, watcher);                            \
        upump_start(watcher);                                               \
    }                                                                       \
}                                                                           \
/** @internal @This cleans up the private members for this helper.          \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_clean_##UPUMP(struct upipe *upipe)                  \
{                                                                           \
    STRUCTURE##_set_##UPUMP(upipe, NULL);                                   \
}

#ifdef __cplusplus
}
#endif
#endif
