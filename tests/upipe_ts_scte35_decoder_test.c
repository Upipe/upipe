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
 * IN NO TS SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/** @file
 * @short unit tests for TS SCTE35 decoder module
 */

#undef NDEBUG

#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_std.h>
#include <upipe/upipe.h>
#include <upipe-ts/upipe_ts_scte35_decoder.h>
#include <upipe-ts/uref_ts_flow.h>
#include <upipe-ts/uref_ts_scte35.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>

#include <bitstream/scte/35.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UBUF_POOL_DEPTH 0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

static int round;

/** definition of our uprobe */
static int catch(struct uprobe *uprobe, struct upipe *upipe,
                 int ts, va_list args)
{
    switch (ts) {
        default:
            assert(0);
            break;
        case UPROBE_READY:
        case UPROBE_DEAD:
        case UPROBE_NEED_OUTPUT:
            break;
        case UPROBE_NEW_FLOW_DEF: {
            struct uref *uref = va_arg(args, struct uref *);
            assert(uref != NULL);
            const char *def;
            ubase_assert(uref_flow_get_def(uref, &def));
            assert(!strcmp(def, "void.scte35."));
            break;
        }
        case UPROBE_CLOCK_TS: {
            struct uref *uref = va_arg(args, struct uref *);
            assert(uref != NULL);
            uint64_t decaps_pts;
            uref_clock_get_pts_orig(uref, &decaps_pts);
            assert(decaps_pts == 27000000);
            break;
        }
    }
    return UBASE_ERR_NONE;
}

/** helper phony pipe */
static struct upipe *test_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe,
                                uint32_t signature, va_list args)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_init(upipe, mgr, uprobe);
    return upipe;
}

/** helper phony pipe */
static void test_input(struct upipe *upipe, struct uref *uref,
                          struct upump **upump_p)
{
    assert(uref != NULL);
    assert(uref->ubuf == NULL);

    switch (round) {
        case 1: {
            uint64_t event_id;
            ubase_assert(uref_ts_scte35_get_event_id(uref, &event_id));
            assert(event_id == 4242);
            ubase_nassert(uref_ts_scte35_get_cancel(uref));
            ubase_assert(uref_ts_scte35_get_out_of_network(uref));
            uint64_t unique_program_id;
            ubase_assert(uref_ts_scte35_get_unique_program_id(uref,
                        &unique_program_id));
            assert(unique_program_id == 2424);
            ubase_assert(uref_ts_scte35_get_auto_return(uref));
            uint64_t pts, duration;
            ubase_assert(uref_clock_get_pts_orig(uref, &pts));
            assert(pts == 27000000);
            ubase_assert(uref_clock_get_duration(uref, &duration));
            assert(duration == 54000000);
            break;
        }
        case 2: {
            uint64_t event_id;
            ubase_assert(uref_ts_scte35_get_event_id(uref, &event_id));
            assert(event_id == 4242);
            ubase_assert(uref_ts_scte35_get_cancel(uref));
            break;
        }
        case 3: {
            uint64_t event_id;
            ubase_assert(uref_ts_scte35_get_event_id(uref, &event_id));
            assert(event_id == 4243);
            ubase_nassert(uref_ts_scte35_get_cancel(uref));
            ubase_nassert(uref_ts_scte35_get_out_of_network(uref));
            uint64_t unique_program_id;
            ubase_assert(uref_ts_scte35_get_unique_program_id(uref,
                        &unique_program_id));
            assert(unique_program_id == 2425);
            ubase_nassert(uref_ts_scte35_get_auto_return(uref));
            uint64_t pts, duration;
            ubase_nassert(uref_clock_get_pts_orig(uref, &pts));
            ubase_nassert(uref_clock_get_duration(uref, &duration));
            break;
        }
        default:
            assert(0);
    }
    round = 0;
    uref_free(uref);
}

/** helper phony pipe */
static int test_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF:
            return UBASE_ERR_NONE;
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *urequest = va_arg(args, struct urequest *);
            return upipe_throw_provide_request(upipe, urequest);
        }
        case UPIPE_UNREGISTER_REQUEST:
            return UBASE_ERR_NONE;
        default:
            assert(0);
            return UBASE_ERR_UNHANDLED;
    }
}

/** helper phony pipe */
static void test_free(struct upipe *upipe)
{
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe */
static struct upipe_mgr test_mgr = {
    .refcount = NULL,
    .upipe_alloc = test_alloc,
    .upipe_input = test_input,
    .upipe_control = test_control
};

int main(int argc, char *argv[])
{
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    assert(uref_mgr != NULL);
    struct ubuf_mgr *ubuf_mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH,
                                                         UBUF_POOL_DEPTH,
                                                         umem_mgr, -1, 0);
    assert(ubuf_mgr != NULL);
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_LEVEL);
    assert(uprobe_stdio != NULL);
    uprobe_stdio = uprobe_ubuf_mem_alloc(uprobe_stdio, umem_mgr,
                                         UBUF_POOL_DEPTH, UBUF_POOL_DEPTH);
    assert(uprobe_stdio != NULL);

    struct uref *uref;
    uref = uref_block_flow_alloc_def(uref_mgr, "mpegtspsi.mpegtsscte35.");
    assert(uref != NULL);

    struct upipe_mgr *upipe_ts_scte35d_mgr = upipe_ts_scte35d_mgr_alloc();
    assert(upipe_ts_scte35d_mgr != NULL);
    struct upipe *upipe_ts_scte35d = upipe_void_alloc(upipe_ts_scte35d_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "ts scte35d"));
    assert(upipe_ts_scte35d != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_scte35d, uref));
    uref_free(uref);

    struct upipe *upipe_sink = upipe_void_alloc_output(upipe_ts_scte35d,
            &test_mgr, uprobe_use(uprobe_stdio));
    assert(upipe_sink != NULL);

    uint8_t *scte35;
    int size;

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, PSI_HEADER_SIZE + PSI_MAX_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &scte35));
    assert(size == PSI_HEADER_SIZE + PSI_MAX_SIZE);
    scte35_init(scte35);
    psi_set_length(scte35, PSI_MAX_SIZE);
    scte35_set_pts_adjustment(scte35, 0);
    scte35_insert_init(scte35,
            SCTE35_INSERT_HEADER2_SIZE +
            SCTE35_SPLICE_TIME_HEADER_SIZE + SCTE35_SPLICE_TIME_TIME_SIZE +
            SCTE35_BREAK_DURATION_HEADER_SIZE + SCTE35_INSERT_FOOTER_SIZE);
    scte35_insert_set_cancel(scte35, false);
    scte35_insert_set_event_id(scte35, 4242);
    scte35_insert_set_out_of_network(scte35, true);
    scte35_insert_set_program_splice(scte35, true);
    scte35_insert_set_duration(scte35, true);
    scte35_insert_set_splice_immediate(scte35, false);

    uint8_t *splice_time = scte35_insert_get_splice_time(scte35);
    scte35_splice_time_init(splice_time);
    scte35_splice_time_set_time_specified(splice_time, true);
    scte35_splice_time_set_pts_time(splice_time, 90000);

    uint8_t *duration = scte35_insert_get_break_duration(scte35);
    scte35_break_duration_init(duration);
    scte35_break_duration_set_auto_return(duration, true);
    scte35_break_duration_set_duration(duration, 180000);

    scte35_insert_set_unique_program_id(scte35, 2424);
    scte35_insert_set_avail_num(scte35, 0);
    scte35_insert_set_avails_expected(scte35, 0);

    scte35_set_desclength(scte35, 0);
    uint16_t length =
        scte35_get_descl(scte35) + PSI_CRC_SIZE - scte35 - PSI_HEADER_SIZE;
    psi_set_length(scte35, length);
    psi_set_crc(scte35);

    uref_block_unmap(uref, 0);
    uref_block_resize(uref, 0, PSI_HEADER_SIZE + length);
    round = 1;
    upipe_input(upipe_ts_scte35d, uref, NULL);
    assert(round == 0);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
            SCTE35_HEADER_SIZE + SCTE35_INSERT_HEADER_SIZE +
            SCTE35_HEADER2_SIZE + PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &scte35));
    assert(size == SCTE35_HEADER_SIZE + SCTE35_INSERT_HEADER_SIZE +
                   SCTE35_HEADER2_SIZE + PSI_CRC_SIZE);
    scte35_init(scte35);
    psi_set_length(scte35, SCTE35_HEADER_SIZE + SCTE35_INSERT_HEADER_SIZE +
            SCTE35_HEADER2_SIZE + PSI_CRC_SIZE - PSI_HEADER_SIZE);
    scte35_set_pts_adjustment(scte35, 0);
    scte35_insert_init(scte35, 0);
    scte35_insert_set_cancel(scte35, true);
    scte35_insert_set_event_id(scte35, 4242);

    scte35_set_desclength(scte35, 0);
    psi_set_crc(scte35);

    uref_block_unmap(uref, 0);
    round = 2;
    upipe_input(upipe_ts_scte35d, uref, NULL);
    assert(round == 0);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
            SCTE35_HEADER_SIZE + SCTE35_INSERT_HEADER_SIZE +
            SCTE35_INSERT_HEADER2_SIZE + SCTE35_INSERT_FOOTER_SIZE +
            SCTE35_HEADER2_SIZE + PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &scte35));
    assert(size == SCTE35_HEADER_SIZE + SCTE35_INSERT_HEADER_SIZE +
            SCTE35_INSERT_HEADER2_SIZE + SCTE35_INSERT_FOOTER_SIZE +
            SCTE35_HEADER2_SIZE + PSI_CRC_SIZE);
    scte35_init(scte35);
    psi_set_length(scte35, SCTE35_HEADER_SIZE + SCTE35_INSERT_HEADER_SIZE +
            SCTE35_INSERT_HEADER2_SIZE + SCTE35_INSERT_FOOTER_SIZE +
            SCTE35_HEADER2_SIZE + PSI_CRC_SIZE - PSI_HEADER_SIZE);
    scte35_set_pts_adjustment(scte35, 0);
    scte35_insert_init(scte35,
            SCTE35_INSERT_HEADER2_SIZE + SCTE35_INSERT_FOOTER_SIZE);
    scte35_insert_set_cancel(scte35, false);
    scte35_insert_set_event_id(scte35, 4243);
    scte35_insert_set_out_of_network(scte35, false);
    scte35_insert_set_program_splice(scte35, true);
    scte35_insert_set_duration(scte35, false);
    scte35_insert_set_splice_immediate(scte35, true);

    scte35_insert_set_unique_program_id(scte35, 2425);
    scte35_insert_set_avail_num(scte35, 0);
    scte35_insert_set_avails_expected(scte35, 0);

    scte35_set_desclength(scte35, 0);
    psi_set_crc(scte35);

    uref_block_unmap(uref, 0);
    round = 3;
    upipe_input(upipe_ts_scte35d, uref, NULL);
    assert(round == 0);

    upipe_release(upipe_ts_scte35d);
    upipe_mgr_release(upipe_ts_scte35d_mgr); // nop

    test_free(upipe_sink);

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(uprobe_stdio);
    uprobe_clean(&uprobe);

    return 0;
}
