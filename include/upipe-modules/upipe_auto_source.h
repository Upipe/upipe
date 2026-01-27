/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _UPIPE_MODULES_UPIPE_AUTO_SOURCE_H_
# define _UPIPE_MODULES_UPIPE_AUTO_SOURCE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_AUTO_SRC_SIGNATURE UBASE_FOURCC('a','s','r','c')

/** @This extends @ref upipe_command with specific auto source commands. */
enum upipe_auto_src_mgr_command {
    UPIPE_AUTO_SRC_MGR_SENTINEL = UPIPE_MGR_CONTROL_LOCAL,

    /** set the source manager for the given scheme */
    UPIPE_AUTO_SRC_MGR_SET_MGR,
    /** get the source manager of the given scheme */
    UPIPE_AUTO_SRC_MGR_GET_MGR,
};

/** @This sets the @tt {struct upipe_mgr *} to use for the scheme.
 *
 * @param mgr pointer to upipe auto source manager
 * @param scheme an URI scheme (ex: "http", "file", "https", ...)
 * @param mgr_src the @tt {struct upipe_mgr *} to use for the URI beginning
 * with scheme
 * @return an error code
 */
static inline int upipe_auto_src_mgr_set_mgr(struct upipe_mgr *mgr,
                                             const char *scheme,
                                             struct upipe_mgr *mgr_src)
{
    return upipe_mgr_control(mgr, UPIPE_AUTO_SRC_MGR_SET_MGR,
                             UPIPE_AUTO_SRC_SIGNATURE, scheme, mgr_src);
}

/** @This gets the @tt {struct upipe_mgr *} used for a given scheme.
 *
 * @param mgr pointer to upipe auto source manager
 * @param scheme an URI scheme (ex: "http", "file", "https", ...)
 * @param mgr_src_p a pointer filled with the @tt {struct upipe_mgr *}
 * used for the scheme
 * @return an error code
 */
static inline int upipe_auto_src_mgr_get_mgr(struct upipe_mgr *mgr,
                                             const char *scheme,
                                             struct upipe_mgr **mgr_src_p)
{
    return upipe_mgr_control(mgr, UPIPE_AUTO_SRC_MGR_GET_MGR,
                             UPIPE_AUTO_SRC_SIGNATURE, scheme, mgr_src_p);
}

/** @This returns the management structure for auto source.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_auto_src_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_MODULES_UPIPE_AUTO_SOURCE_H_ */
