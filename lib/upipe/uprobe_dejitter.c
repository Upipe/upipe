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
#include <upipe/uref.h>
#include <upipe/uref_clock.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_helper_uprobe.h>
#include <upipe/uprobe_dejitter.h>
#include <upipe/upipe.h>

#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>

/** @This is a super-set of the uprobe structure with additional local
 * members. */
struct uprobe_dejitter {
    /** number of references to average */
    unsigned int divider;

    /** number of references received for offset calculaton */
    unsigned int offset_count;
    /** offset between stream clock and system clock */
    int64_t offset;
    /** residue */
    int64_t offset_residue;

    /** number of references received for deviation calculaton */
    unsigned int deviation_count;
    /** average absolute deviation */
    uint64_t deviation;
    /** residue */
    uint64_t deviation_residue;

    /** structure exported to modules */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_dejitter, uprobe)

/** @internal @This catches clock_ref events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return true if the event was caught and handled
 */
static bool uprobe_dejitter_clock_ref(struct uprobe *uprobe,
                                      struct upipe *upipe,
                                      enum uprobe_event event, va_list args)
{
    struct uprobe_dejitter *uprobe_dejitter =
        uprobe_dejitter_from_uprobe(uprobe);
    struct uref *uref = va_arg(args, struct uref *);
    uint64_t clock_ref = va_arg(args, uint64_t);
    int discontinuity = va_arg(args, int);
    uint64_t sys_ref;
    if (unlikely(uref == NULL || !uref_clock_get_systime(uref, &sys_ref)))
        return false;

    int64_t offset = sys_ref - clock_ref;
    lldiv_t q;

    if (discontinuity) {
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
              abs(offset - uprobe_dejitter->offset),
              uprobe_dejitter->deviation_count + 1);
    uprobe_dejitter->deviation = q.quot;
    uprobe_dejitter->deviation_residue = q.rem;
    if (uprobe_dejitter->deviation_count < uprobe_dejitter->divider)
        uprobe_dejitter->deviation_count++;

#if 0
    upipe_dbg_va(upipe, "new ref %"PRId64" %u %"PRId64" %u %"PRIu64, offset,
                 uprobe_dejitter->offset_count, uprobe_dejitter->offset,
                 uprobe_dejitter->deviation_count, uprobe_dejitter->deviation);
#endif
    return true;
}

/** @internal @This catches clock_ts events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return true if the event was caught and handled
 */
static bool uprobe_dejitter_clock_ts(struct uprobe *uprobe, struct upipe *upipe,
                                     enum uprobe_event event, va_list args)
{
    struct uprobe_dejitter *uprobe_dejitter =
        uprobe_dejitter_from_uprobe(uprobe);
    struct uref *uref = va_arg(args, struct uref *);
    if (unlikely(uref == NULL || !uprobe_dejitter->offset_count))
        return false;

    uint64_t clock_pts;
    if (uref_clock_get_pts(uref, &clock_pts)) {
        if (unlikely(!uref_clock_set_pts_sys(uref,
                            clock_pts + uprobe_dejitter->offset +
                            uprobe_dejitter->deviation))) {
            uprobe_throw_aerror(uprobe, upipe);
            return true;
        }
    }

    uint64_t clock_dts;
    if (uref_clock_get_dts(uref, &clock_dts)) {
        if (unlikely(!uref_clock_set_dts_sys(uref,
                            clock_dts + uprobe_dejitter->offset +
                            uprobe_dejitter->deviation))) {
            uprobe_throw_aerror(uprobe, upipe);
            return true;
        }
    }
    return true;
}

/** @internal @This catches events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return true if the event was caught and handled
 */
static bool uprobe_dejitter_throw(struct uprobe *uprobe, struct upipe *upipe,
                                  enum uprobe_event event, va_list args)
{
    switch (event) {
        case UPROBE_CLOCK_REF:
            return uprobe_dejitter_clock_ref(uprobe, upipe, event, args);
        case UPROBE_CLOCK_TS:
            return uprobe_dejitter_clock_ts(uprobe, upipe, event, args);
        default:
            return false;
    }
}

/** @This allocates a new uprobe_dejitter structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param divider number of reference clocks to keep for dejittering
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_dejitter_alloc(struct uprobe *next,
                                     unsigned int divider)
{
    assert(divider);
    struct uprobe_dejitter *uprobe_dejitter =
        malloc(sizeof(struct uprobe_dejitter));
    if (unlikely(uprobe_dejitter == NULL))
        return NULL;
    struct uprobe *uprobe = uprobe_dejitter_to_uprobe(uprobe_dejitter);
    uprobe_dejitter->divider = divider;
    uprobe_dejitter->offset_count = 0;
    uprobe_dejitter->offset = 0;
    uprobe_dejitter->offset_residue = 0;
    uprobe_dejitter->deviation_count = 0;
    uprobe_dejitter->deviation = 0;
    uprobe_dejitter->deviation_residue = 0;
    uprobe_init(uprobe, uprobe_dejitter_throw, next);
    return uprobe;
}

/** @This frees a uprobe_dejitter structure.
 *
 * @param uprobe structure to free
 * @return next probe
 */
struct uprobe *uprobe_dejitter_free(struct uprobe *uprobe)
{
    struct uprobe *next = uprobe->next;
    struct uprobe_dejitter *uprobe_dejitter =
        uprobe_dejitter_from_uprobe(uprobe);
    free(uprobe_dejitter);
    return next;
}
