/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module setting arbitrary attributes to urefs
 */

#ifndef _UPIPE_MODULES_UPIPE_SETATTR_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_SETATTR_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"
#include "upipe/uref.h"

#define UPIPE_SETATTR_SIGNATURE UBASE_FOURCC('s','a','t','t')

/** @This extends upipe_command with specific commands for setattr pipes. */
enum upipe_setattr_command {
    UPIPE_SETATTR_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the current dictionary being set into urefs (struct uref **) */
    UPIPE_SETATTR_GET_DICT,
    /** sets the dictionary to set into urefs (struct uref *) */
    UPIPE_SETATTR_SET_DICT
};

/** @This returns the management structure for all setattr pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_setattr_mgr_alloc(void);

/** @This returns the current dictionary being set into urefs.
 *
 * @param upipe description structure of the pipe
 * @param dict_p filled with the current dictionary
 * @return an error code
 */
static inline int upipe_setattr_get_dict(struct upipe *upipe,
                                         struct uref **dict_p)
{
    return upipe_control(upipe, UPIPE_SETATTR_GET_DICT,
                         UPIPE_SETATTR_SIGNATURE, dict_p);
}

/** @This sets the dictionary to set into urefs.
 *
 * @param upipe description structure of the pipe
 * @param dict dictionary to set
 * @return an error code
 */
static inline int upipe_setattr_set_dict(struct upipe *upipe,
                                         struct uref *dict)
{
    return upipe_control(upipe, UPIPE_SETATTR_SET_DICT,
                         UPIPE_SETATTR_SIGNATURE, dict);
}

#ifdef __cplusplus
}
#endif
#endif
