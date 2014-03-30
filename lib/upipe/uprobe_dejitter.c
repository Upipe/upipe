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
#include <assert.h>

/** max allowed jitter */
#define MAX_JITTER (UCLOCK_FREQ / 10)

/** @internal @This catches clock_ref events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return an error code
 */
static enum ubase_err uprobe_dejitter_clock_ref(struct uprobe *uprobe,
                                                struct upipe *upipe,
                                                enum uprobe_event event,
                                                va_list args)
{
    struct uprobe_dejitter *uprobe_dejitter =
        uprobe_dejitter_from_uprobe(uprobe);
    struct uref *uref = va_arg(args, struct uref *);
    uint64_t clock_ref = va_arg(args, uint64_t);
    int discontinuity = va_arg(args, int);
    if (unlikely(uref == NULL))
        return UBASE_ERR_INVALID;
    uint64_t sys_ref;
    if (unlikely(!ubase_check(uref_clock_get_cr_sys(uref, &sys_ref)))) {
        upipe_warn(upipe, "[dejitter] no clock ref in packet");
        return UBASE_ERR_INVALID;
    }

    int64_t offset = sys_ref - clock_ref;
    lldiv_t q;

    if (unlikely(llabs(offset - uprobe_dejitter->offset) > MAX_JITTER)) {
        upipe_dbg_va(upipe, "[dejitter] max jitter reached (%"PRId64")",
                     offset - uprobe_dejitter->offset);
        discontinuity = 1;
    }

    if (unlikely(discontinuity)) {
        upipe_dbg(upipe, "[dejitter] discontinuity");
        uprobe_dejitter->offset_count = 0;
        uprobe_dejitter->offset = 0;
        uprobe_dejitter->offset_residue = 0;
        /* but do not reset the deviation */
    }

    q = lldiv(uprobe_dejitter->offset * uprobe_dejitter->offset_count +
              uprobe_dejitter->offset_residue + offset,
              uprobe_dejitter->offset_count + 1);
    uprobe_dejitter->offset = q.quot;
    uprobe_dejitter->offset_residue = q.rem;
    if (uprobe_dejitter->offset_count < uprobe_dejitter->divider)
        uprobe_dejitter->offset_count++;

    q = lldiv(uprobe_dejitter->deviation * uprobe_dejitter->deviation_count +
              uprobe_dejitter->deviation_residue +
              llabs(offset - uprobe_dejitter->offset),
              uprobe_dejitter->deviation_count + 1);
    uprobe_dejitter->deviation = q.quot;
    uprobe_dejitter->deviation_residue = q.rem;
    if (uprobe_dejitter->deviation_count < uprobe_dejitter->divider)
        uprobe_dejitter->deviation_count++;

    upipe_verbose_va(upipe, "new ref %"PRId64" %u %"PRId64" %u %"PRIu64, offset,
                     uprobe_dejitter->offset_count, uprobe_dejitter->offset,
                     uprobe_dejitter->deviation_count,
                     uprobe_dejitter->deviation);
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
static enum ubase_err uprobe_dejitter_clock_ts(struct uprobe *uprobe,
                                               struct upipe *upipe,
                                               enum uprobe_event event,
                                               va_list args)
{
    struct uprobe_dejitter *uprobe_dejitter =
        uprobe_dejitter_from_uprobe(uprobe);
    struct uref *uref = va_arg(args, struct uref *);
    if (unlikely(uref == NULL || !uprobe_dejitter->offset_count))
        return UBASE_ERR_INVALID;

    uint64_t date;
    int type;
    uref_clock_get_date_prog(uref, &date, &type);
    if (type == UREF_DATE_NONE)
        return UBASE_ERR_INVALID;

    uref_clock_set_date_sys(uref,
            date + uprobe_dejitter->offset + uprobe_dejitter->deviation,
            type);
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
static enum ubase_err uprobe_dejitter_throw(struct uprobe *uprobe,
                                            struct upipe *upipe,
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
 * @param divider number of reference clocks to keep for dejittering
 */
void uprobe_dejitter_set(struct uprobe *uprobe, unsigned int divider)
{
    struct uprobe_dejitter *uprobe_dejitter =
        uprobe_dejitter_from_uprobe(uprobe);
    uprobe_dejitter->divider = divider;
    uprobe_dejitter->offset_count = 0;
    uprobe_dejitter->offset = 0;
    uprobe_dejitter->offset_residue = 0;
    uprobe_dejitter->deviation_count = 0;
    uprobe_dejitter->deviation = 0;
    uprobe_dejitter->deviation_residue = 0;
}

/** @This initializes an already allocated uprobe_dejitter structure.
 *
 * @param uprobe_pfx pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @param divider number of reference clocks to keep for dejittering
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_dejitter_init(struct uprobe_dejitter *uprobe_dejitter,
                                    struct uprobe *next, unsigned int divider)
{
    assert(uprobe_dejitter != NULL);
    struct uprobe *uprobe = uprobe_dejitter_to_uprobe(uprobe_dejitter);
    uprobe_dejitter_set(uprobe, divider);
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

#define ARGS_DECL struct uprobe *next, unsigned int divider
#define ARGS next, divider
UPROBE_HELPER_ALLOC(uprobe_dejitter)
#undef ARGS
#undef ARGS_DECL
