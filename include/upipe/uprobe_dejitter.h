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

#ifndef _UPIPE_UPROBE_DEJITTER_H_
/** @hidden */
#define _UPIPE_UPROBE_DEJITTER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uprobe.h>
#include <upipe/uprobe_helper_uprobe.h>

#include <stdint.h>

/** @This is a super-set of the uprobe structure with additional local
 * members. */
struct uprobe_dejitter {
    /** number of offsets to average */
    unsigned int offset_divider;
    /** number of deviations to average */
    unsigned int deviation_divider;

    /** number of references received for offset calculaton */
    unsigned int offset_count;
    /** offset between stream clock and system clock */
    double offset;

    /** number of references received for deviation calculaton */
    unsigned int deviation_count;
    /** average absolute deviation */
    double deviation;
    /** minimum deviation */
    double minimum_deviation;

    /** cr_prog of last clock ref */
    uint64_t last_cr_prog;
    /** cr_sys of last clock ref */
    uint64_t last_cr_sys;
    /** PLL drift rate */
    struct urational drift_rate;

    /** cr_sys of the last debug print */
    uint64_t last_print;

    /** structure exported to modules */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_dejitter, uprobe)

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
                                    uint64_t deviation);

/** @This cleans a uprobe_dejitter structure.
 *
 * @param uprobe_dejitter structure to clean
 */
void uprobe_dejitter_clean(struct uprobe_dejitter *uprobe_dejitter);

/** @This allocates a new uprobe_dejitter structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param enabled true if dejitter is enabled
 * @param deviation initial deviation, if not 0
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_dejitter_alloc(struct uprobe *next, bool enabled,
                                     uint64_t deviation);

/** @This sets the parameters of the dejittering.
 *
 * @param uprobe pointer to probe
 * @param enabled true if dejitter is enabled
 * @param deviation initial deviation, if not 0
 */
void uprobe_dejitter_set(struct uprobe *uprobe, bool enabled,
                         uint64_t deviation);

/** @This sets the minimum deviation of the dejittering probe.
 *
 * @param uprobe pointer to probe
 * @param deviation minimum deviation to set
 */
void uprobe_dejitter_set_minimum_deviation(struct uprobe *uprobe,
                                           double deviation);

#ifdef __cplusplus
}
#endif
#endif
