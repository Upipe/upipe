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

#include "upipe/uclock.h"
#include "upipe/uclock_std.h"
#include "upipe/uprobe.h"
#include "upipe/uprobe_stdio.h"
#include "upipe/uprobe_prefix.h"
#include "upipe/uprobe_upump_mgr.h"
#include "upipe/uprobe_uclock.h"
#include "upipe/umem.h"
#include "upipe/umem_alloc.h"
#include "upipe/udict.h"
#include "upipe/udict_inline.h"
#include "upipe/uref.h"
#include "upipe/uref_flow.h"
#include "upipe/uref_clock.h"
#include "upipe/uref_std.h"
#include "upipe/uref_dump.h"
#include "upipe/upipe.h"
#include "upipe-ts/upipe_ts_scte35_probe.h"
#include "upipe-ts/uref_ts_flow.h"
#include "upipe-ts/uref_ts_scte35.h"
#include "upipe-ts/uref_ts_scte35_desc.h"
#include "upipe/upump.h"
#include "upump-ev/upump_ev.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include <bitstream/scte/35.h>

#define UPUMP_POOL 0
#define UPUMP_BLOCKER_POOL 0
#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UBUF_POOL_DEPTH 0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

static int round = 0, subround = 0;
static const char *upid_str = "This is a user defined UPID !";

/** definition of our uprobe */
static int catch(struct uprobe *uprobe, struct upipe *upipe,
                 int event, va_list args)
{
    switch (event) {
        default:
            assert(0);
            break;
        case UPROBE_LOG:
            return uprobe_throw_next(uprobe, upipe, event, args);
        case UPROBE_READY:
        case UPROBE_DEAD:
        case UPROBE_NEW_FLOW_DEF:
            break;
        case UPROBE_TS_SCTE35P_EVENT: {
            assert(va_arg(args, unsigned int) == UPIPE_TS_SCTE35P_SIGNATURE);
            struct uref *uref = va_arg(args, struct uref *);
            assert(uref != NULL);

            uref_dump(uref, uprobe);

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

            uref_dump(uref, uprobe);

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

            uref_dump(uref, uprobe);

            switch (round) {
                case 5: {
                    uint64_t pts_orig;
                    ubase_assert(uref_clock_get_pts_orig(uref, &pts_orig));
                    assert(pts_orig == 1);
                    break;
                }

                case 6: {
                    uint64_t pts_orig;
                    ubase_nassert(uref_clock_get_pts_orig(uref, &pts_orig));
                    break;
                }

                case 7: {
                    switch (subround) {
                        case 0: {
                            uint64_t pts_orig;
                            ubase_nassert(uref_clock_get_pts_orig(uref, &pts_orig));

                            uint64_t nb = 0;
                            uref_ts_flow_get_descriptors(uref, &nb);
                            assert(nb == 1);

                            struct uref *desc = uref_ts_scte35_extract_desc(uref, 0);
                            assert(desc);
                            uint64_t event_id = 0;
                            uref_ts_scte35_desc_seg_get_event_id(desc, &event_id);
                            assert(event_id == 4242);
                            ubase_assert(uref_ts_scte35_desc_seg_get_cancel(desc));
                            uref_free(desc);
                            subround = 1;
                            return UBASE_ERR_NONE;
                        }

                        case 1: {
                            uint64_t pts_orig;
                            ubase_assert(uref_clock_get_pts_orig(uref, &pts_orig));
                            assert(pts_orig == 1);

                            uint64_t nb = 0;
                            uref_ts_flow_get_descriptors(uref, &nb);
                            assert(nb == 1);

                            struct uref *desc = uref_ts_scte35_extract_desc(uref, 0);
                            assert(desc);
                            uint64_t event_id = 0;
                            uref_ts_scte35_desc_seg_get_event_id(desc, &event_id);
                            assert(event_id == 4243);
                            ubase_nassert(uref_ts_scte35_desc_seg_get_cancel(desc));
                            uref_free(desc);
                            subround = 0;
                            break;
                        }

                        default:
                            abort();
                    }
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


    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(NULL, stdout,
                                                     UPROBE_LOG_LEVEL);
    assert(uprobe_stdio != NULL);

    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, uprobe_stdio);

    uprobe_stdio = uprobe_upump_mgr_alloc(&uprobe, upump_mgr);
    assert(uprobe_stdio != NULL);

    struct uclock *uclock = uclock_std_alloc(0);;
    uprobe_stdio = uprobe_uclock_alloc(uprobe_stdio, uclock);
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

    /* immediate splice insert */
    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_ts_scte35_set_command_type(uref, SCTE35_INSERT_COMMAND));
    ubase_assert(uref_ts_scte35_set_event_id(uref, 1));
    ubase_assert(uref_ts_scte35_set_out_of_network(uref));
    round = 1;
    uprobe_notice_va(uprobe_stdio, NULL, "round %i", round);
    upipe_input(upipe_ts_scte35p, uref, NULL);
    assert(round == 0);
    assert(!ev_run(loop, EVRUN_NOWAIT));

    /* scheduled splice insert overwrite to immediate */
    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_ts_scte35_set_command_type(uref, SCTE35_INSERT_COMMAND));
    ubase_assert(uref_ts_scte35_set_event_id(uref, 2));
    ubase_assert(uref_ts_scte35_set_out_of_network(uref));
    uref_clock_set_pts_sys(uref, uclock_now(uclock) + UCLOCK_FREQ);
    round = 2;
    uprobe_notice_va(uprobe_stdio, NULL, "round %i", round);
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
    uprobe_notice_va(uprobe_stdio, NULL, "round %i", round);
    assert(!ev_run(loop, 0));
    assert(round == 0);

    /* null */
    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_ts_scte35_set_command_type(uref, SCTE35_NULL_COMMAND));
    uref_clock_set_pts_sys(uref, uclock_now(uclock) + UCLOCK_FREQ);
    round = 4;
    uprobe_notice_va(uprobe_stdio, NULL, "round %i", round);
    upipe_input(upipe_ts_scte35p, uref, NULL);
    assert(!ev_run(loop, EVRUN_NOWAIT));
    assert(round == 0);

    /* simple schedule time signal */
    round = 5;
    uprobe_notice_va(uprobe_stdio, NULL, "round %i", round);
    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_ts_scte35_set_command_type(uref,
                                                 SCTE35_TIME_SIGNAL_COMMAND));
    uref_clock_set_pts_sys(uref, uclock_now(uclock) + UCLOCK_FREQ);
    uref_clock_set_pts_orig(uref, 1);
    upipe_input(upipe_ts_scte35p, uref, NULL);
    assert(!ev_run(loop, 0));
    assert(round == 0);

    /* duplicate simple schedule time signal */
    round = 5;
    uprobe_notice_va(uprobe_stdio, NULL, "round %i dup", round);
    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_ts_scte35_set_command_type(uref,
                                                 SCTE35_TIME_SIGNAL_COMMAND));
    uref_clock_set_pts_sys(uref, uclock_now(uclock) + UCLOCK_FREQ);
    uref_clock_set_pts_orig(uref, 1);
    upipe_input(upipe_ts_scte35p, uref_dup(uref), NULL);
    upipe_input(upipe_ts_scte35p, uref, NULL);
    assert(!ev_run(loop, 0));
    assert(round == 0);

    /* simple immediate time signal */
    round = 6;
    uprobe_notice_va(uprobe_stdio, NULL, "round %i", round);
    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_ts_scte35_set_command_type(uref,
                                                 SCTE35_TIME_SIGNAL_COMMAND));
    upipe_input(upipe_ts_scte35p, uref, NULL);
    assert(!ev_run(loop, 0));
    assert(round == 0);

    /* time signal with segmentation descriptors */
    round = 7;
    uprobe_notice_va(uprobe_stdio, NULL, "round %i", round);
    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_ts_scte35_set_command_type(uref,
                                                 SCTE35_TIME_SIGNAL_COMMAND));
    uref_clock_set_pts_sys(uref, uclock_now(uclock) + UCLOCK_FREQ);
    uref_clock_set_pts_orig(uref, 1);
    {
        struct uref *desc = uref_alloc_control(uref_mgr);
        assert(desc != NULL);
        uref_ts_scte35_desc_set_tag(desc, SCTE35_SPLICE_DESC_TAG_SEG);
        uref_ts_scte35_desc_set_identifier(desc, 0x43554549);
        uref_ts_scte35_desc_seg_set_event_id(desc, 4242);
        uref_ts_scte35_desc_seg_set_delivery_not_restricted(desc);
        uref_ts_scte35_desc_seg_set_upid_type(desc, SCTE35_SEG_DESC_UPID_TYPE_MPU);
        uref_ts_scte35_desc_seg_set_upid(desc,
                                         (uint8_t *)upid_str, strlen(upid_str) + 1);
        uref_ts_scte35_desc_seg_set_type_id(desc,
                                            SCTE35_SEG_DESC_TYPE_ID_PROG_END);
        uref_ts_scte35_desc_seg_set_num(desc, 0);
        uref_ts_scte35_desc_seg_set_expected(desc, 0);
        ubase_assert(uref_ts_scte35_add_desc(uref, desc));
        uref_free(desc);
    }
    {
        struct uref *desc = uref_alloc_control(uref_mgr);
        assert(desc != NULL);
        uref_ts_scte35_desc_set_tag(desc, SCTE35_SPLICE_DESC_TAG_SEG);
        uref_ts_scte35_desc_set_identifier(desc, 0x43554549);
        uref_ts_scte35_desc_seg_set_event_id(desc, 4243);
        uref_ts_scte35_desc_seg_set_delivery_not_restricted(desc);
        uref_ts_scte35_desc_seg_set_upid_type(desc, SCTE35_SEG_DESC_UPID_TYPE_MPU);
        uref_ts_scte35_desc_seg_set_upid(desc,
                                         (uint8_t *)upid_str, strlen(upid_str) + 1);
        uref_ts_scte35_desc_seg_set_type_id(desc,
                                            SCTE35_SEG_DESC_TYPE_ID_BREAK_START);
        uref_ts_scte35_desc_seg_set_num(desc, 0);
        uref_ts_scte35_desc_seg_set_expected(desc, 0);
        ubase_assert(uref_ts_scte35_add_desc(uref, desc));
        uref_free(desc);
    }
    upipe_input(upipe_ts_scte35p, uref_dup(uref), NULL);
    upipe_input(upipe_ts_scte35p, uref, NULL);
    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_ts_scte35_set_command_type(uref,
                                                 SCTE35_TIME_SIGNAL_COMMAND));
    {
        struct uref *desc = uref_alloc_control(uref_mgr);
        assert(desc != NULL);
        uref_ts_scte35_desc_set_tag(desc, SCTE35_SPLICE_DESC_TAG_SEG);
        uref_ts_scte35_desc_set_identifier(desc, 0x43554549);
        uref_ts_scte35_desc_seg_set_event_id(desc, 4242);
        uref_ts_scte35_desc_seg_set_cancel(desc);
        ubase_assert(uref_ts_scte35_add_desc(uref, desc));
        uref_free(desc);
    }

    upipe_input(upipe_ts_scte35p, uref, NULL);
    assert(!ev_run(loop, 0));
    assert(round == 0);

    upipe_release(upipe_ts_scte35p);
    upipe_mgr_release(upipe_ts_scte35p_mgr); // nop

    uclock_release(uclock);
    upump_mgr_release(upump_mgr);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(uprobe_stdio);
    uprobe_clean(&uprobe);

    ev_default_destroy();
    return 0;
}
