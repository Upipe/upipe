/*
 * Copyright (C) 2018 OpenHeadend S.A.R.L.
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module trying inner pipes to handle input flow def.
 */

#ifndef _UPIPE_MODULES_UPIPE_AUTO_INNER_H_
#define _UPIPE_MODULES_UPIPE_AUTO_INNER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/ubase.h"
#include "upipe/upipe.h"

#define UPIPE_AUTOIN_SIGNATURE  UBASE_FOURCC('a','u','t','i')

/** @This enumerates the auto inner pipe manager private commands. */
enum upipe_autoin_mgr_command {
    /** sentinel */
    UPIPE_AUTOIN_MGR_SENTINEL = UPIPE_MGR_CONTROL_LOCAL,

    /** add an inner manager to try (struct upipe_mgr *) */
    UPIPE_AUTOIN_MGR_ADD_MGR,
    /** delete an inner manager from the list (struct upipe_mgr *) */
    UPIPE_AUTOIN_MGR_DEL_MGR,
};

/** @This adds an inner manager to try.
 *
 * @param mgr auto inner pipe manager
 * @param name name use for inner uprobe prefix
 * @param inner_mgr inner manager to add
 * @return an error code
 */
static inline int upipe_autoin_mgr_add_mgr(struct upipe_mgr *mgr,
                                           const char *name,
                                           struct upipe_mgr *inner_mgr)
{
    return upipe_mgr_control(mgr, UPIPE_AUTOIN_MGR_ADD_MGR,
                             UPIPE_AUTOIN_SIGNATURE, name, inner_mgr);
}

/** @This deletes an inner manager pipe from the auto inner manager list.
 *
 * @param mgr auto inner pipe manager
 * @param inner_mgr inner pipe manager to delete
 */
static inline int upipe_autoin_mgr_del_mgr(struct upipe_mgr *mgr,
                                           struct upipe_mgr *inner_mgr)
{
    return upipe_mgr_control(mgr, UPIPE_AUTOIN_MGR_DEL_MGR,
                             UPIPE_AUTOIN_SIGNATURE, inner_mgr);
}

/** @This allocates and initializes a pipe manager.
 *
 * @return an alloocated and initialized pipe manager or NULL
 */
struct upipe_mgr *upipe_autoin_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
