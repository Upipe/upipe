/*
 * Copyright (C) 2013-2017 OpenHeadend S.A.R.L.
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
 *
 * This implementation uses a low-pass filter to filter out the sampling noise,
 * and a phase-locked loop to catch up with the clock of the transmitter. We
 * try to avoid changing the drift of the PLL too often, because in TS mux
 * this will trigger PCR inaccuracies, so only five thresholds are allowed:
 * -desperate, -standard, 0, +standard, and +desperate.
 * The desperate modes are not compliant with ISO MPEG, but we have to use them
 * in desperate situations.
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

/** offset divider */
#define OFFSET_DIVIDER 1000
/** deviation divider */
#define DEVIATION_DIVIDER 100
/** default initial deviation */
#define DEFAULT_INITIAL_DEVIATION (UCLOCK_FREQ / 150)
/** max allowed jitter */
#define MAX_JITTER (UCLOCK_FREQ / 10)
/** additional drift to avoid bouncing from one rate to another (5 ms) */
#define DRIFT_SLIDE (UCLOCK_FREQ / 200)
/** threshold below which the PLL is set to desperate low (20 ms) */
#define DRIFT_DESPERATE_LOW (-(int64_t)UCLOCK_FREQ / 50)
/** threshold below which the PLL is set to standard low (0 ms) */
#define DRIFT_STANDARD_LOW 0
/** threshold above which the PLL is set to standard high (20 ms) */
#define DRIFT_STANDARD_HIGH (UCLOCK_FREQ / 50)
/** threshold above which the PLL is set to desperate high (100 ms) */
#define DRIFT_DESPERATE_HIGH (UCLOCK_FREQ / 10)
/** standard PLL drift (25 ppm - ISO compliant) */
#define PLL_STANDARD (UCLOCK_FREQ * 5 / 200000)
/** desperate PLL drift (1000 ppm - not compliant) */
#define PLL_DESPERATE (UCLOCK_FREQ / 1000)
/** debug print periodicity */
#define PRINT_PERIODICITY (60 * UCLOCK_FREQ)

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
    else if (unlikely(fabs(offset - uprobe_dejitter->offset) >
                      MAX_JITTER + 3 * uprobe_dejitter->deviation)) {
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
    if (uprobe_dejitter->offset_count < uprobe_dejitter->offset_divider)
        uprobe_dejitter->offset_count++;

    double deviation = offset - uprobe_dejitter->offset;
    uprobe_dejitter->deviation =
        sqrt((uprobe_dejitter->deviation * uprobe_dejitter->deviation *
              uprobe_dejitter->deviation_count + deviation * deviation) /
             (uprobe_dejitter->deviation_count + 1));
    if (uprobe_dejitter->deviation_count < uprobe_dejitter->deviation_divider)
        uprobe_dejitter->deviation_count++;

    if (uprobe_dejitter->deviation < uprobe_dejitter->minimum_deviation)
        uprobe_dejitter->deviation = uprobe_dejitter->minimum_deviation;

    int64_t wanted_offset = uprobe_dejitter->offset +
                            3 * uprobe_dejitter->deviation;
    if (uprobe_dejitter->offset_count == 1) {
        uprobe_dejitter->last_cr_prog = cr_prog;
        uprobe_dejitter->last_cr_sys = cr_prog + wanted_offset;
        uprobe_dejitter->drift_rate.num =
            uprobe_dejitter->drift_rate.den = 1;
    }

    /* phase-locked loop */
    uint64_t real_cr_sys = uprobe_dejitter->last_cr_sys +
                           (cr_prog - uprobe_dejitter->last_cr_prog) *
                           uprobe_dejitter->drift_rate.num /
                           uprobe_dejitter->drift_rate.den;
    int64_t real_offset = (int64_t)real_cr_sys - (int64_t)cr_prog;
    int64_t error_offset = real_offset - wanted_offset;

    if (uprobe_dejitter->offset_count > 1) {
        uprobe_dejitter->last_cr_prog = cr_prog;
        uprobe_dejitter->last_cr_sys = real_cr_sys;
        struct urational drift_rate;
        drift_rate.num = uprobe_dejitter->drift_rate.num * UCLOCK_FREQ /
                         uprobe_dejitter->drift_rate.den;
        drift_rate.den = UCLOCK_FREQ;

        /* calculate thresholds for drift changes, with optional slide */
        int64_t desperate_low = DRIFT_DESPERATE_LOW;
        if (drift_rate.num > UCLOCK_FREQ + PLL_STANDARD)
            desperate_low += DRIFT_SLIDE;
        int64_t standard_low = DRIFT_STANDARD_LOW;
        if (drift_rate.num > UCLOCK_FREQ)
            standard_low += DRIFT_SLIDE;
        int64_t standard_high = DRIFT_STANDARD_HIGH;
        if (drift_rate.num < UCLOCK_FREQ)
            standard_high -= DRIFT_SLIDE;
        int64_t desperate_high = DRIFT_DESPERATE_HIGH;
        if (drift_rate.num < UCLOCK_FREQ - PLL_STANDARD)
            desperate_high -= DRIFT_SLIDE;

        /* calculate wanted drift rate */
        if (error_offset < desperate_low)
            drift_rate.num = UCLOCK_FREQ + PLL_DESPERATE;
        else if (error_offset < standard_low)
            drift_rate.num = UCLOCK_FREQ + PLL_STANDARD;
        else if (error_offset > desperate_high)
            drift_rate.num = UCLOCK_FREQ - PLL_DESPERATE;
        else if (error_offset > standard_high)
            drift_rate.num = UCLOCK_FREQ - PLL_STANDARD;
        else
            drift_rate.num = UCLOCK_FREQ;
        urational_simplify(&drift_rate);

        if (drift_rate.num != uprobe_dejitter->drift_rate.num ||
            drift_rate.den != uprobe_dejitter->drift_rate.den)
            upipe_dbg_va(upipe, "changing drift rate from %f to %f",
                         (double)uprobe_dejitter->drift_rate.num /
                         uprobe_dejitter->drift_rate.den,
                         (double)drift_rate.num / drift_rate.den);
        uprobe_dejitter->drift_rate = drift_rate;
    }

    if (cr_sys > uprobe_dejitter->last_print + PRINT_PERIODICITY) {
        upipe_dbg_va(upipe,
                "dejitter drift %f error %"PRId64" deviation %g",
                (double)uprobe_dejitter->drift_rate.num /
                uprobe_dejitter->drift_rate.den,
                error_offset, uprobe_dejitter->deviation);
        uprobe_dejitter->last_print = cr_sys;
    }

    upipe_verbose_va(upipe,
            "new ref offset %"PRId64" error %"PRId64" deviation %g",
            real_offset, error_offset, uprobe_dejitter->deviation);
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

    uint64_t date_sys = (int64_t)uprobe_dejitter->last_cr_sys +
        ((int64_t)date - (int64_t)uprobe_dejitter->last_cr_prog) *
        uprobe_dejitter->drift_rate.num /
        (int64_t)uprobe_dejitter->drift_rate.den;
    uref_clock_set_date_sys(uref, date_sys, type);
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

    if (uprobe_dejitter->offset_divider) {
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

/** @This sets the parameters of the dejittering.
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
    uprobe_dejitter->offset_divider = enabled ? OFFSET_DIVIDER : 0;
    uprobe_dejitter->deviation_divider = enabled ? DEVIATION_DIVIDER : 0;
    uprobe_dejitter->offset_count = 0;
    uprobe_dejitter->deviation_count = 1;
    uprobe_dejitter->offset = 0;
    if (deviation)
        uprobe_dejitter->deviation = deviation;
    else
        uprobe_dejitter->deviation = DEFAULT_INITIAL_DEVIATION;

    if (uprobe_dejitter->deviation < uprobe_dejitter->minimum_deviation)
        uprobe_dejitter->deviation = uprobe_dejitter->minimum_deviation;
}

/** @This sets the minimum deviation of the dejittering probe.
 *
 * @param uprobe pointer to probe
 * @param deviation minimum deviation to set
 */
void uprobe_dejitter_set_minimum_deviation(struct uprobe *uprobe,
                                           double deviation)
{
    struct uprobe_dejitter *uprobe_dejitter =
        uprobe_dejitter_from_uprobe(uprobe);
    uprobe_dejitter->minimum_deviation = deviation;
    if (uprobe_dejitter->deviation < deviation)
        uprobe_dejitter->deviation = deviation;
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
    uprobe_dejitter->last_print = 0;
    uprobe_dejitter->minimum_deviation = 0;
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
