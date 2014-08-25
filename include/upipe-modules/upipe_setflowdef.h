/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
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

#include <upipe/upipe.h>
#include <upipe/uref.h>

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
