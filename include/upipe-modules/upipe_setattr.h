/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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
 * @short Upipe module setting arbitrary attributes to urefs
 */

#ifndef _UPIPE_MODULES_UPIPE_SETATTR_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_SETATTR_H_

#include <upipe/upipe.h>
#include <upipe/uref.h>

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
 * @return false in case of error
 */
static inline bool upipe_setattr_get_dict(struct upipe *upipe,
                                          struct uref **dict_p)
{
    return upipe_control(upipe, UPIPE_SETATTR_GET_DICT,
                         UPIPE_SETATTR_SIGNATURE, dict_p);
}

/** @This sets the dictionary to set into urefs.
 *
 * @param upipe description structure of the pipe
 * @param dict dictionary to set
 * @return false in case of error
 */
static inline bool upipe_setattr_set_dict(struct upipe *upipe,
                                          struct uref *dict)
{
    return upipe_control(upipe, UPIPE_SETATTR_SET_DICT,
                         UPIPE_SETATTR_SIGNATURE, dict);
}

#endif
