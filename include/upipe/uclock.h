/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
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
 * @short structure provided by the application to retrieve system time
 */

#ifndef _UPIPE_UCLOCK_H_
/** @hidden */
#define _UPIPE_UCLOCK_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/urefcount.h>

#include <stdint.h>
#include <sys/time.h>
#include <assert.h>

#define UCLOCK_FREQ UINT64_C(27000000)

/** @This is a structure allowing to retrieve system time. */
struct uclock {
    /** pointer to refcount management structure */
    struct urefcount *refcount;

    /** function returning the current system time */
    uint64_t (*uclock_now)(struct uclock *);

    /** function converting a system time to Epoch time_t */
    time_t (*uclock_mktime)(struct uclock *, uint64_t);
};

/** @This returns the current system time.
 *
 * @param uclock pointer to uclock
 * @return current system time in 27 MHz ticks
 */
static inline uint64_t uclock_now(struct uclock *uclock)
{
    return uclock->uclock_now(uclock);
}

/** @This converts a system time to Epoch time_t (seconds from
 * 1970-01-01 00:00:00 +0000)
 *
 * @param uclock pointer to uclock
 * @param systime system time in 27 MHz ticks
 * @param number of seconds since the Epoch, or (time_t)-1 if unsupported
 */
static inline time_t uclock_mktime(struct uclock *uclock, uint64_t systime)
{
    if (uclock->uclock_mktime == NULL)
        return (time_t)-1;
    return uclock->uclock_mktime(uclock, systime);
}

/** @This increments the reference count of a uclock.
 *
 * @param uclock pointer to uclock
 * @return same pointer to uclock
 */
static inline struct uclock *uclock_use(struct uclock *uclock)
{
    if (uclock == NULL)
        return NULL;
    urefcount_use(uclock->refcount);
    return uclock;
}

/** @This decrements the reference count of a uclock or frees it.
 *
 * @param uclock pointer to uclock
 */
static inline void uclock_release(struct uclock *uclock)
{
    if (uclock != NULL)
        urefcount_release(uclock->refcount);
}

#ifdef __cplusplus
}
#endif
#endif
