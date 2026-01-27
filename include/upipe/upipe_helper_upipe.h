/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe helper functions for public upipe structure
 */

#ifndef _UPIPE_UPIPE_HELPER_UPIPE_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_UPIPE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/ubase.h"
#include "upipe/upipe.h"

#include <assert.h>

/** @This declares two functions dealing with public and private parts
 * of the allocated pipe structure.
 *
 * You must add the upipe structure to your private pipe structure:
 * @code
 *  struct upipe upipe;
 * @end code
 *
 * Supposing the name of your structure is upipe_foo, it declares:
 * @list
 * @item @code
 *  struct upipe *upipe_foo_to_upipe(struct upipe_foo *s)
 * @end code
 * Returns a pointer to the public upipe structure.
 *
 * @item @code
 *  struct upipe_foo *upipe_foo_from_upipe(struct upipe *upipe)
 * @end code
 * Returns a pointer to the private upipe_foo structure.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure
 * @param UPIPE name of the @tt{struct upipe} field of
 * your private upipe structure
 * @param SIGNATURE signature of the manager of the upipe
 */
#define UPIPE_HELPER_UPIPE(STRUCTURE, UPIPE, SIGNATURE)                     \
/** @internal @This returns the public upipe structure.                     \
 *                                                                          \
 * @param STRUCTURE pointer to the private STRUCTURE structure              \
 * @return pointer to the public upipe structure                            \
 */                                                                         \
static UBASE_UNUSED inline struct upipe *                                   \
    STRUCTURE##_to_upipe(struct STRUCTURE *s)                               \
{                                                                           \
    return &s->UPIPE;                                                       \
}                                                                           \
/** @internal @This returns the private STRUCTURE structure.                \
 *                                                                          \
 * @param upipe public description structure of the pipe                    \
 * @return pointer to the private STRUCTURE structure                       \
 */                                                                         \
static UBASE_UNUSED inline struct STRUCTURE *                               \
    STRUCTURE##_from_upipe(struct upipe *upipe)                             \
{                                                                           \
    assert(upipe->mgr->signature == SIGNATURE);                             \
    return container_of(upipe, struct STRUCTURE, UPIPE);                    \
}

#ifdef __cplusplus
}
#endif
#endif
