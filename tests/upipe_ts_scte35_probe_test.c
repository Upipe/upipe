/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
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
 * @short unit tests for TS SCTE35 probe module
 */

#undef NDEBUG

#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_upump_mgr.h>
#include <upipe/uprobe_uclock.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_std.h>
#include <upipe/upipe.h>
#include <upipe-ts/upipe_ts_scte35_probe.h>
#include <upipe-ts/uref_ts_scte35.h>
#include <upipe-ts/uref_ts_scte35_desc.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>

#include <bitstream/scte/35.h>

#define UPUMP_POOL 0
#define UPUMP_BLOCKER_POOL 0
#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UBUF_POOL_DEPTH 0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

static int round = 0;

/** definition of our uprobe */
static int catch(struct uprobe *uprobe, struct upipe *upipe,
                 int event, va_list args)
{
    switch (event) {
        default:
            assert(0);
            break;
        case UPROBE_READY:
        case UPROBE_DEAD:
        case UPROBE_NEW_FLOW_DEF:
            break;
        case UPROBE_TS_SCTE35P_EVENT: {
            assert(va_arg(args, unsigned int) == UPIPE_TS_SCTE35P_SIGNATURE);
            struct uref *uref = va_arg(args, struct uref *);
            assert(uref != NULL);

            switch (round) {
                case 1: {
                    uint64_t event_id;
                    ubase_assert(uref_ts_scte35_get_event_id(uref, &event_id));
                    assert(event_id == 1);
                    ubase_assert(uref_ts_scte35_get_out_of_network(uref));
                    break;
                }
                case 2: {
                    uint64_t event_id;
                    ubase_assert(uref_ts_scte35_get_event_id(uref, &event_id));
                    assert(event_id == 2);
                    ubase_assert(uref_ts_scte35_get_out_of_network(uref));
                    break;
                }
                case 3: {
                    uint64_t event_id;
                    ubase_assert(uref_ts_scte35_get_event_id(uref, &event_id));
                    assert(event_id == 2);
                    ubase_nassert(uref_ts_scte35_get_out_of_network(uref));
                    break;
                }
                default:
                    assert(0);
            }
            round = 0;
            break;
        }
        case UPROBE_TS_SCTE35P_NULL: {
            assert(va_arg(args, unsigned int) == UPIPE_TS_SCTE35P_SIGNATURE);
            struct uref *uref = va_arg(args, struct uref *);
            assert(uref != NULL);

            switch (round) {
                case 4:
                    break;
                default:
                    abort();
            }
            round = 0;
            break;
        }

        case UPROBE_TS_SCTE35P_SIGNAL: {
            assert(va_arg(args, unsigned int) == UPIPE_TS_SCTE35P_SIGNATURE);
            struct uref *uref = va_arg(args, struct uref *);
            assert(uref != NULL);

            switch (round) {
                case 5: {
                    uint64_t pts_orig;
                    ubase_assert(uref_clock_get_pts_orig(uref, &pts_orig));
                    assert(pts_orig == 1);
                    break;
                }
                default:
                    abort();
            }
            round = 0;
            break;
        }
    }
    return UBASE_ERR_NONE;
}

/** helper uclock */
static uint64_t test_now(struct uclock *uclock)
{
    return 1;
}

int main(int argc, char *argv[])
{
    struct ev_loop *loop = ev_default_loop(0);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop, UPUMP_POOL,
                                                     UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);

    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    assert(uref_mgr != NULL);
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_LEVEL);
    assert(uprobe_stdio != NULL);
    uprobe_stdio = uprobe_upump_mgr_alloc(uprobe_stdio, upump_mgr);
    assert(uprobe_stdio != NULL);

    struct uclock uclock;
    uclock.refcount = NULL;
    uclock.uclock_now = test_now;
    uclock.uclock_to_real = uclock.uclock_from_real = NULL;
    uprobe_stdio = uprobe_uclock_alloc(uprobe_stdio, &uclock);
    assert(uprobe_stdio != NULL);

    struct uref *uref;
    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_flow_set_def(uref, "void.scte35."));

    struct upipe_mgr *upipe_ts_scte35p_mgr = upipe_ts_scte35p_mgr_alloc();
    assert(upipe_ts_scte35p_mgr != NULL);
    struct upipe *upipe_ts_scte35p = upipe_void_alloc(upipe_ts_scte35p_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "ts_scte35p"));
    assert(upipe_ts_scte35p != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_scte35p, uref));
    uref_free(uref);

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_ts_scte35_set_command_type(uref, SCTE35_INSERT_COMMAND));
    ubase_assert(uref_ts_scte35_set_event_id(uref, 1));
    ubase_assert(uref_ts_scte35_set_out_of_network(uref));
    round = 1;
    upipe_input(upipe_ts_scte35p, uref, NULL);
    assert(round == 0);
    assert(!ev_run(loop, EVRUN_NOWAIT));

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_ts_scte35_set_command_type(uref, SCTE35_INSERT_COMMAND));
    ubase_assert(uref_ts_scte35_set_event_id(uref, 2));
    ubase_assert(uref_ts_scte35_set_out_of_network(uref));
    uref_clock_set_pts_sys(uref, UCLOCK_FREQ);
    round = 2;
    upipe_input(upipe_ts_scte35p, uref, NULL);
    assert(ev_run(loop, EVRUN_NOWAIT));

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_ts_scte35_set_command_type(uref, SCTE35_INSERT_COMMAND));
    ubase_assert(uref_ts_scte35_set_event_id(uref, 2));
    ubase_assert(uref_ts_scte35_set_out_of_network(uref));
    uref_clock_set_duration(uref, UCLOCK_FREQ);
    ubase_assert(uref_ts_scte35_set_auto_return(uref));
    upipe_input(upipe_ts_scte35p, uref, NULL);
    assert(round == 0);
    assert(ev_run(loop, EVRUN_NOWAIT));

    round = 3;
    assert(!ev_run(loop, 0));
    assert(round == 0);

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_ts_scte35_set_command_type(uref, SCTE35_NULL_COMMAND));
    uref_clock_set_pts_sys(uref, UCLOCK_FREQ);
    round = 4;
    upipe_input(upipe_ts_scte35p, uref, NULL);
    assert(round == 0);

    round = 5;
    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_ts_scte35_set_command_type(uref,
                                                 SCTE35_TIME_SIGNAL_COMMAND));
    uref_clock_set_pts_sys(uref, UCLOCK_FREQ);
    uref_clock_set_pts_orig(uref, 1);
    upipe_input(upipe_ts_scte35p, uref, NULL);
    assert(!ev_run(loop, 0));
    assert(round == 0);

    round = 6;
    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_ts_scte35_set_command_type(uref,
                                                 SCTE35_TIME_SIGNAL_COMMAND));
    uref_clock_set_pts_sys(uref, UCLOCK_FREQ);
    uref_clock_set_pts_orig(uref, 1);
    upipe_input(upipe_ts_scte35p, uref, NULL);
    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_ts_scte35_set_command_type(uref,
                                                 SCTE35_TIME_SIGNAL_COMMAND));
    uref_clock_set_pts_sys(uref, UCLOCK_FREQ);
    uref_clock_set_pts_orig(uref, 1);
    uref_ts_scte35_desc_seg_set_cancel(uref);
    upipe_input(upipe_ts_scte35p, uref, NULL);
    assert(!ev_run(loop, 0));
    assert(round == 6);

    upipe_release(upipe_ts_scte35p);
    upipe_mgr_release(upipe_ts_scte35p_mgr); // nop

    upump_mgr_release(upump_mgr);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(uprobe_stdio);
    uprobe_clean(&uprobe);

    ev_default_destroy();
    return 0;
}
