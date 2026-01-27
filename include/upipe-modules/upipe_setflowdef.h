/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module setting arbitrary attributes to flow definitions
 */

#ifndef _UPIPE_MODULES_UPIPE_SETFLOWDEF_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_SETFLOWDEF_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"
#include "upipe/uref.h"

#define UPIPE_SETFLOWDEF_SIGNATURE UBASE_FOURCC('s','f','l','w')

/** @This extends upipe_command with specific commands for setflowdef pipes. */
enum upipe_setflowdef_command {
    UPIPE_SETFLOWDEF_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the current dictionary being set into urefs (struct uref **) */
    UPIPE_SETFLOWDEF_GET_DICT,
    /** sets the dictionary to set into urefs (struct uref *) */
    UPIPE_SETFLOWDEF_SET_DICT
};

/** @This returns the management structure for all setflowdef pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_setflowdef_mgr_alloc(void);

/** @This returns the current dictionary being set into flow definitions.
 *
 * @param upipe description structure of the pipe
 * @param dict_p filled with the current dictionary
 * @return an error code
 */
static inline int upipe_setflowdef_get_dict(struct upipe *upipe,
                                            struct uref **dict_p)
{
    return upipe_control(upipe, UPIPE_SETFLOWDEF_GET_DICT,
                         UPIPE_SETFLOWDEF_SIGNATURE, dict_p);
}

/** @This sets the dictionary to set into flow definitions.
 *
 * @param upipe description structure of the pipe
 * @param dict dictionary to set
 * @return an error code
 */
static inline int upipe_setflowdef_set_dict(struct upipe *upipe,
                                         struct uref *dict)
{
    return upipe_control(upipe, UPIPE_SETFLOWDEF_SET_DICT,
                         UPIPE_SETFLOWDEF_SIGNATURE, dict);
}

#ifdef __cplusplus
}
#endif
#endif
