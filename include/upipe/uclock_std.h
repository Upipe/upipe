/*****************************************************************************
 * uclock_std.h: standard implementation of uclock
 *****************************************************************************
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
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
 *****************************************************************************/

#ifndef _UPIPE_UCLOCK_STD_H_
/** @hidden */
#define _UPIPE_UCLOCK_STD_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uclock.h>

/** flags for the creation of a uclock_t structure */
enum uclock_std_flags {
    /** force using a real-time clock even if a monotonic clock is available */
    UCLOCK_FLAG_REALTIME = 0x1
};

/** @This allocates a new uclock_t structure.
 *
 * @param flags flags for the creation of a uclock structure
 * @return pointer to uclock, or NULL in case of error
 */
struct uclock *uclock_std_alloc(enum uclock_std_flags flags);

#ifdef __cplusplus
}
#endif
#endif
