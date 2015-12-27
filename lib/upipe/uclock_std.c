/*
 * Copyright (C) 2012-2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *          Benjamin Cohen
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
 * @short standard common toolbox provided by the application to modules
 */

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/uclock.h>
#include <upipe/uclock_std.h>

#include <stdlib.h>
#include <time.h>

#ifdef __MACH__
#include <string.h>
#include <mach/clock.h>
#include <mach/mach.h>
#endif

/** super-set of the uclock structure with additional local members */
struct uclock_std {
    /** refcount management structure */
    struct urefcount urefcount;

    /** flags at the creation of this clock */
    enum uclock_std_flags flags;
#ifdef __MACH__
    /** mach cclock structure */
    clock_serv_t cclock;
#endif

    /** structure exported to modules */
    struct uclock uclock;
};

UBASE_FROM_TO(uclock_std, uclock, uclock, uclock)
UBASE_FROM_TO(uclock_std, urefcount, urefcount, urefcount)

/** @This returns the current time in the given clock.
 *
 * @param uclock utility structure passed to the module
 * @param flags type of clock
 * @return current system time in 27 MHz ticks
 */
static uint64_t uclock_std_now_inner(struct uclock *uclock,
                                     enum uclock_std_flags flags)
{
#ifdef __MACH__ // OS X does not have clock_gettime, use clock_get_time
    struct uclock_std *std = uclock_std_from_uclock(uclock);
    mach_timespec_t ts;
    clock_get_time((std->cclock), &ts);

#else
    struct timespec ts;
    if (unlikely(clock_gettime((flags & UCLOCK_FLAG_REALTIME) ?
                               CLOCK_REALTIME : CLOCK_MONOTONIC, &ts) == -1))
        /* this should not happen as we have checked the clock existed
         * in alloc */
        return UINT64_MAX;
#endif

    uint64_t now = ts.tv_sec * UCLOCK_FREQ +
                   ts.tv_nsec * UCLOCK_FREQ / UINT64_C(1000000000);
    return now;
}

/** @This returns the current system time.
 *
 * @param uclock utility structure passed to the module
 * @return current system time in 27 MHz ticks
 */
static uint64_t uclock_std_now(struct uclock *uclock)
{
    struct uclock_std *std = uclock_std_from_uclock(uclock);
    return uclock_std_now_inner(uclock, std->flags);
}

/** @This converts a system time to Epoch-based real time (from
 * 1970-01-01 00:00:00 +0000). The scale is in units of @ref #UCLOCK_FREQ,
 * divide by it to get standard time_t.
 *
 * @param uclock pointer to uclock
 * @param systime system time in 27 MHz ticks
 * @return number of ticks since the Epoch, or UINT64_MAX if unsupported
 */
static uint64_t uclock_std_to_real(struct uclock *uclock, uint64_t systime)
{
    struct uclock_std *std = uclock_std_from_uclock(uclock);

    if (std->flags & UCLOCK_FLAG_REALTIME)
        return systime;

    uint64_t now = uclock_std_now(uclock);
    uint64_t ref = uclock_std_now_inner(uclock, UCLOCK_FLAG_REALTIME);
    return ref + systime - now;
}

/** @This converts Epoch-based real time (from * 1970-01-01 00:00:00 +0000)
 * to real time. The scale has to be passed in units of @ref #UCLOCK_FREQ.
 *
 * @param uclock pointer to uclock
 * @param real number of ticks since the Epoch
 * @return system time in 27 MHz ticks, or UINT64_MAX if unsupported
 */
static uint64_t uclock_std_from_real(struct uclock *uclock, uint64_t real)
{
    struct uclock_std *std = uclock_std_from_uclock(uclock);

    if (std->flags & UCLOCK_FLAG_REALTIME)
        return real;

    uint64_t now = uclock_std_now(uclock);
    uint64_t ref = uclock_std_now_inner(uclock, UCLOCK_FLAG_REALTIME);
    return now + real - ref;
}

/** @This frees a uclock.
 *
 * @param urefcount pointer to urefcount
 */
static void uclock_std_free(struct urefcount *urefcount)
{
    struct uclock_std *uclock_std = uclock_std_from_urefcount(urefcount);
#ifdef __MACH__
    mach_port_deallocate(mach_task_self(), uclock_std->cclock);
#endif
    urefcount_clean(urefcount);
    free(uclock_std);
}

/** @This allocates a new uclock structure.
 *
 * @param flags flags for the creation of a uclock structure
 * @return pointer to uclock, or NULL in case of error
 */
struct uclock *uclock_std_alloc(enum uclock_std_flags flags)
{
#ifdef __MACH__
    clock_serv_t cclock;
    mach_timespec_t ts;
    if (unlikely(host_get_clock_service(mach_host_self(), unlikely(flags & UCLOCK_FLAG_REALTIME) ? CALENDAR_CLOCK : REALTIME_CLOCK, &cclock) < 0)) {
        return NULL;
    }
    if(unlikely(clock_get_time(cclock, &ts) < 0)) {
        mach_port_deallocate(mach_task_self(), cclock);
        return NULL;
    }
#else
    struct timespec ts;
    if (unlikely(clock_gettime(unlikely(flags & UCLOCK_FLAG_REALTIME) ?
                               CLOCK_REALTIME : CLOCK_MONOTONIC, &ts) == -1))
        return NULL;
#endif

    struct uclock_std *uclock_std = malloc(sizeof(struct uclock_std));
    if (unlikely(uclock_std == NULL))
        return NULL;
    uclock_std->flags = flags;
    urefcount_init(uclock_std_to_urefcount(uclock_std), uclock_std_free);
    uclock_std->uclock.refcount = uclock_std_to_urefcount(uclock_std);
    uclock_std->uclock.uclock_now = uclock_std_now;
    uclock_std->uclock.uclock_to_real = uclock_std_to_real;
    uclock_std->uclock.uclock_from_real = uclock_std_from_real;
#ifdef __MACH__
    memcpy(&uclock_std->cclock, &cclock, sizeof(cclock));
#endif
    return uclock_std_to_uclock(uclock_std);
}
