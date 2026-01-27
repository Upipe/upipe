/*****************************************************************************
 * uclock_std_test.c: unit tests for uclock_std
 *****************************************************************************
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen <bencoh@notk.org>
 *
 * SPDX-License-Identifier: MIT
 *****************************************************************************/

#undef NDEBUG

#include "upipe/uclock.h"
#include "upipe/uclock_std.h"

#include <stdio.h>
#include <inttypes.h>
#include <assert.h>

#define UREF_POOL_DEPTH 1
#define TIME_SAMPLE 1429627742

int main(int argc, char **argv)
{
    uint64_t now, now_cal;
    struct uclock *uclock = uclock_std_alloc(0);
    struct uclock *uclock_cal = uclock_std_alloc(UCLOCK_FLAG_REALTIME);
    assert(uclock);
    assert(uclock_cal);
    now = uclock_now(uclock);
    now_cal = uclock_now(uclock_cal);
    assert(now);
    assert(now_cal);
    printf("Now: %"PRIu64"\n", now);
    printf("Cal: %"PRIu64"\n", now_cal);
    assert(uclock_to_real(uclock_cal, (uint64_t)TIME_SAMPLE * UCLOCK_FREQ) ==
           TIME_SAMPLE * UCLOCK_FREQ);
    assert(uclock_from_real(uclock_cal, (uint64_t)TIME_SAMPLE * UCLOCK_FREQ) ==
           TIME_SAMPLE * UCLOCK_FREQ);
    uclock_release(uclock);
    uclock_release(uclock_cal);
}
