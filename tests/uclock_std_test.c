/*****************************************************************************
 * uclock_std_test.c: unit tests for uclock_std
 *****************************************************************************
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen <bencoh@notk.org>
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

#undef NDEBUG

#include <upipe/uclock.h>
#include <upipe/uclock_std.h>

#include <stdio.h>
#include <inttypes.h>
#include <string.h>
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
