/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _UPIPE_MODULES_UPIPE_VOID_SOURCE_H_
# define _UPIPE_MODULES_UPIPE_VOID_SOURCE_H_
#ifdef __cplusplus
extern "C" {
#endif

/** @This defines the void source pipe signature. */
# define UPIPE_VOIDSRC_SIGNATURE    UBASE_FOURCC('v','o','i','d')

/** This returns the void source pipe manager.
 *
 * @return the void source pipe manager
 */
struct upipe_mgr *upipe_voidsrc_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
