/*
 * Copyright (C) 2013-2015 OpenHeadend S.A.R.L.
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
 * @short probe catching clock_ref and clock_ts events for dejittering
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_clock.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_dejitter.h>
#include <upipe/uprobe_helper_alloc.h>
#include <upipe/upipe.h>

#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#include <assert.h>

/** dejitter divider */
#define DEJITTER_DIVIDER 1000
/** default initial deviation */
#define DEFAULT_INITIAL_DEVIATION (UCLOCK_FREQ / 150)
/** max allowed jitter */
#define MAX_JITTER (UCLOCK_FREQ / 10)
/** max allowed positive offset without drifting with the PLL (5 ms) */
#define MAX_OFFSET (UCLOCK_FREQ / 200)
/** max PLL drift (20 ppm) */
#define MAX_DRIFT_RATE (UCLOCK_FREQ * 2 / 100000)
/** PLL drift increment */
#define DRIFT_INCREMENT 1

/** @internal @This catches clock_ref events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return an error code
 */
static int uprobe_dejitter_clock_ref(struct uprobe *uprobe, struct upipe *upipe,
                                     enum uprobe_event event, va_list args)
{
    struct uprobe_dejitter *uprobe_dejitter =
        uprobe_dejitter_from_uprobe(uprobe);
    struct uref *uref = va_arg(args, struct uref *);
    uint64_t cr_prog = va_arg(args, uint64_t);
    int discontinuity = va_arg(args, int);
    if (unlikely(uref == NULL))
        return UBASE_ERR_INVALID;
    uint64_t cr_sys;
    if (unlikely(!ubase_check(uref_clock_get_cr_sys(uref, &cr_sys)))) {
        upipe_warn(upipe, "[dejitter] no clock ref in packet");
        return UBASE_ERR_INVALID;
    }

    double offset = (double)((int64_t)cr_sys - (int64_t)cr_prog);
    if (unlikely(discontinuity))
        upipe_warn(upipe, "[dejitter] discontinuity");
    else if (unlikely(fabs(offset - uprobe_dejitter->offset) > MAX_JITTER)) {
        upipe_warn_va(upipe, "[dejitter] max jitter reached (%f ms)",
                      (offset - uprobe_dejitter->offset) * 1000 / UCLOCK_FREQ);
        discontinuity = 1;
    }
    if (unlikely(discontinuity)) {
        uprobe_dejitter->offset_count = 0;
        uprobe_dejitter->offset = 0;
        /* but do not reset the deviation */
    }

    /* low-pass filter */
    uprobe_dejitter->offset =
        (uprobe_dejitter->offset * uprobe_dejitter->offset_count + offset) /
        (uprobe_dejitter->offset_count + 1);
    if (uprobe_dejitter->offset_count < uprobe_dejitter->divider)
        uprobe_dejitter->offset_count++;

    double deviation = offset - uprobe_dejitter->offset;
    uprobe_dejitter->deviation =
        sqrt((uprobe_dejitter->deviation * uprobe_dejitter->deviation *
              uprobe_dejitter->deviation_count + deviation * deviation) /
             (uprobe_dejitter->deviation_count + 1));
    if (uprobe_dejitter->deviation_count < uprobe_dejitter->divider)
        uprobe_dejitter->deviation_count++;

    int64_t wanted_offset = uprobe_dejitter->offset +
                            3 * uprobe_dejitter->deviation;
    if (uprobe_dejitter->offset_count == 1) {
        uprobe_dejitter->last_cr_prog = cr_prog;
        uprobe_dejitter->last_cr_sys = cr_prog + wanted_offset;
        uprobe_dejitter->drift_rate.num = uprobe_dejitter->drift_rate.den = 1;
    }

    /* phase-locked loop */
    uint64_t real_cr_sys = uprobe_dejitter->last_cr_sys +
                           (cr_prog - uprobe_dejitter->last_cr_prog) *
                           uprobe_dejitter->drift_rate.num /
                           uprobe_dejitter->drift_rate.den;
    int64_t real_offset = (int64_t)real_cr_sys - (int64_t)cr_prog;
    if (uprobe_dejitter->offset_count > 1) {
        uprobe_dejitter->last_cr_prog = cr_prog;
        uprobe_dejitter->last_cr_sys = real_cr_sys;

        uint64_t target_drift = UCLOCK_FREQ;
        uint64_t current_drift = uprobe_dejitter->drift_rate.num * UCLOCK_FREQ /
                                 uprobe_dejitter->drift_rate.den;
        if (wanted_offset > real_offset)
            target_drift = UCLOCK_FREQ + MAX_DRIFT_RATE;
        else if (real_offset - wanted_offset > MAX_OFFSET)
            target_drift = UCLOCK_FREQ - MAX_DRIFT_RATE;

        if (target_drift > current_drift)
            uprobe_dejitter->drift_rate.num = current_drift + DRIFT_INCREMENT;
        else if (target_drift < current_drift)
            uprobe_dejitter->drift_rate.num = current_drift - DRIFT_INCREMENT;
        else
            uprobe_dejitter->drift_rate.num = current_drift;
        uprobe_dejitter->drift_rate.den = UCLOCK_FREQ;
        urational_simplify(&uprobe_dejitter->drift_rate);
    }

    upipe_verbose_va(upipe,
            "new ref drift %f offset %"PRId64" target %"PRId64" deviation %f",
            (double)uprobe_dejitter->drift_rate.num /
            uprobe_dejitter->drift_rate.den, real_offset,
            wanted_offset - real_offset, uprobe_dejitter->deviation);
    return UBASE_ERR_NONE;
}

/** @internal @This catches clock_ts events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return an error code
 */
static int uprobe_dejitter_clock_ts(struct uprobe *uprobe, struct upipe *upipe,
                                    enum uprobe_event event, va_list args)
{
    struct uprobe_dejitter *uprobe_dejitter =
        uprobe_dejitter_from_uprobe(uprobe);
    struct uref *uref = va_arg(args, struct uref *);
    if (unlikely(uref == NULL || !uprobe_dejitter->offset_count ||
                 !uprobe_dejitter->drift_rate.den))
        return UBASE_ERR_INVALID;

    uint64_t date;
    int type;
    uref_clock_get_date_prog(uref, &date, &type);
    if (type == UREF_DATE_NONE)
        return UBASE_ERR_INVALID;

    uref_clock_set_date_sys(uref,
            uprobe_dejitter->last_cr_sys +
            (date - uprobe_dejitter->last_cr_prog) *
            uprobe_dejitter->drift_rate.num / uprobe_dejitter->drift_rate.den,
            type);
    uref_clock_set_rate(uref, uprobe_dejitter->drift_rate);
    return UBASE_ERR_NONE;
}

/** @internal @This catches events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return an error code
 */
static int uprobe_dejitter_throw(struct uprobe *uprobe, struct upipe *upipe,
                                 int event, va_list args)
{
    struct uprobe_dejitter *uprobe_dejitter =
        uprobe_dejitter_from_uprobe(uprobe);

    if (uprobe_dejitter->divider) {
        switch (event) {
            case UPROBE_CLOCK_REF:
                return uprobe_dejitter_clock_ref(uprobe, upipe, event, args);
            case UPROBE_CLOCK_TS:
                return uprobe_dejitter_clock_ts(uprobe, upipe, event, args);
            default:
                break;
        }
    }

    return uprobe_throw_next(uprobe, upipe, event, args);
}

/** @This sets a different divider. If set to 0, dejittering is disabled.
 *
 * @param uprobe pointer to probe
 * @param enabled true if dejitter is enabled
 * @param deviation initial deviation, if not 0
 */
void uprobe_dejitter_set(struct uprobe *uprobe, bool enabled,
                         uint64_t deviation)
{
    struct uprobe_dejitter *uprobe_dejitter =
        uprobe_dejitter_from_uprobe(uprobe);
    uprobe_dejitter->divider = enabled ? DEJITTER_DIVIDER : 0;
    uprobe_dejitter->offset_count = 0;
    uprobe_dejitter->offset = 0;
    uprobe_dejitter->deviation_count = 1;
    if (deviation)
        uprobe_dejitter->deviation = deviation;
    else
        uprobe_dejitter->deviation = DEFAULT_INITIAL_DEVIATION;
}

/** @This initializes an already allocated uprobe_dejitter structure.
 *
 * @param uprobe_pfx pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @param enabled true if dejitter is enabled
 * @param deviation initial deviation, if not 0
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_dejitter_init(struct uprobe_dejitter *uprobe_dejitter,
                                    struct uprobe *next, bool enabled,
                                    uint64_t deviation)
{
    assert(uprobe_dejitter != NULL);
    struct uprobe *uprobe = uprobe_dejitter_to_uprobe(uprobe_dejitter);
    uprobe_dejitter->drift_rate.num = uprobe_dejitter->drift_rate.den = 1;
    uprobe_dejitter_set(uprobe, enabled, deviation);
    uprobe_init(uprobe, uprobe_dejitter_throw, next);
    return uprobe;
}

/** @This cleans a uprobe_dejitter structure.
 *
 * @param uprobe_dejitter structure to clean
 */
void uprobe_dejitter_clean(struct uprobe_dejitter *uprobe_dejitter)
{
    assert(uprobe_dejitter != NULL);
    struct uprobe *uprobe = uprobe_dejitter_to_uprobe(uprobe_dejitter);
    uprobe_clean(uprobe);
}

#define ARGS_DECL struct uprobe *next, bool enabled, uint64_t deviation
#define ARGS next, enabled, deviation
UPROBE_HELPER_ALLOC(uprobe_dejitter)
#undef ARGS
#undef ARGS_DECL
