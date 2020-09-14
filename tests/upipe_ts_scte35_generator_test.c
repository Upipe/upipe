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
 * @short unit tests for TS scte35 generator module
 */

#undef NDEBUG

#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/uprobe_uclock.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/uclock.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_event.h>
#include <upipe/uref_block.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_std.h>
#include <upipe/upipe.h>
#include <upipe-ts/uref_ts_scte35.h>
#include <upipe-ts/uref_ts_scte35_desc.h>
#include <upipe-ts/upipe_ts_scte35_generator.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <iconv.h>
#include <assert.h>

#include <bitstream/scte/35.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UBUF_POOL_DEPTH 0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

static const char *upid_str = "This is a user defined UPID !";
static int round;

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
        case UPROBE_NEED_OUTPUT:
            break;
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
    const uint8_t *buffer;
    int size = -1;
    ubase_assert(uref_block_read(uref, 0, &size, &buffer));
    upipe_dbg_va(upipe, "received command %"PRIu8,
                 scte35_get_command_type(buffer));
    assert(psi_validate(buffer));
    assert(scte35_validate(buffer));
    assert(scte35_get_pts_adjustment(buffer) == 0);

    switch (round) {
        case 1:
            assert(scte35_get_command_type(buffer) == SCTE35_NULL_COMMAND);
            break;
        case 2: {
            assert(scte35_get_command_type(buffer) == SCTE35_INSERT_COMMAND);
            assert(scte35_insert_get_event_id(buffer) == 4242);
            assert(!scte35_insert_has_cancel(buffer));
            assert(scte35_insert_has_program_splice(buffer));
            assert(scte35_insert_has_out_of_network(buffer));
            assert(!scte35_insert_has_splice_immediate(buffer));
            assert(scte35_insert_has_duration(buffer));
            assert(scte35_insert_get_unique_program_id(buffer) == 1212);

            const uint8_t *splice_time = scte35_insert_get_splice_time(buffer);
            assert(scte35_splice_time_has_time_specified(splice_time));
            assert(scte35_splice_time_get_pts_time(splice_time) ==
                    (UCLOCK_FREQ * 4) / 300);

            const uint8_t *duration = scte35_insert_get_break_duration(buffer);
            assert(scte35_break_duration_has_auto_return(duration));
            assert(scte35_break_duration_get_duration(duration) ==
                    (UCLOCK_FREQ * 2) / 300);
            break;
        }
        case 3: {
            assert(scte35_get_command_type(buffer) ==
                   SCTE35_TIME_SIGNAL_COMMAND);
            uint8_t *splice_time = scte35_time_signal_get_splice_time(buffer);
            assert(splice_time);
            assert(scte35_splice_time_get_pts_time(splice_time) ==
                   8 * 90000);
            uint8_t *descl = scte35_get_descl(buffer);
            uint16_t descl_length = scte35_get_desclength(buffer);
            assert(descl);
            const uint8_t *desc;
            unsigned i = 0;
            for (; descl && (desc = descl_get_desc(descl, descl_length, i));
                 i++) {
                assert(scte35_splice_desc_get_tag(desc) ==
                       SCTE35_SPLICE_DESC_TAG_SEG);
                assert(scte35_splice_desc_get_identifier(desc) == 0x43554549);
                assert(scte35_seg_desc_get_event_id(desc) == 4242 + i);
                assert(!scte35_seg_desc_has_delivery_not_restricted(desc));
                assert(scte35_seg_desc_has_web_delivery_allowed(desc));
                assert(scte35_seg_desc_has_no_regional_blackout(desc));
                assert(scte35_seg_desc_get_device_restrictions(desc) ==
                       SCTE35_SEG_DESC_DEVICE_RESTRICTION_NONE);
                assert(scte35_seg_desc_has_duration(desc));
                assert(scte35_seg_desc_get_duration(desc) ==
                       2 * 90000);
                assert(scte35_seg_desc_get_upid_type(desc) ==
                       SCTE35_SEG_DESC_UPID_TYPE_MPU);
                assert(!strcmp((const char *)scte35_seg_desc_get_upid(desc),
                               upid_str));
                if (i)
                    assert(scte35_seg_desc_get_type_id(desc) ==
                           SCTE35_SEG_DESC_TYPE_ID_PROVIDER_PO_START);
                else
                    assert(scte35_seg_desc_get_type_id(desc) ==
                           SCTE35_SEG_DESC_TYPE_ID_BREAK_START);
                assert(scte35_seg_desc_get_num(desc) == 42 + i);
                assert(scte35_seg_desc_get_expected(desc) == 242);
            }
            assert(i == 2);
            break;
        }
        case 4: {
            assert(scte35_get_command_type(buffer) ==
                   SCTE35_TIME_SIGNAL_COMMAND);
            uint8_t *splice_time = scte35_time_signal_get_splice_time(buffer);
            assert(splice_time);
            assert(scte35_splice_time_get_pts_time(splice_time) ==
                   12 * 90000);
            uint8_t *descl = scte35_get_descl(buffer);
            uint16_t descl_length = scte35_get_desclength(buffer);
            assert(descl);
            const uint8_t *desc;
            unsigned i = 0;
            for (; descl && (desc = descl_get_desc(descl, descl_length, i));
                 i++) {
                assert(scte35_splice_desc_get_tag(desc) ==
                       SCTE35_SPLICE_DESC_TAG_SEG);
                assert(scte35_splice_desc_get_identifier(desc) == 0x43554549);
                assert(scte35_seg_desc_get_event_id(desc) == 4242 + i);
                assert(scte35_seg_desc_has_delivery_not_restricted(desc));
                assert(!scte35_seg_desc_has_duration(desc));
                assert(scte35_seg_desc_get_upid_type(desc) ==
                       SCTE35_SEG_DESC_UPID_TYPE_MPU);
                assert(!strcmp((const char *)scte35_seg_desc_get_upid(desc),
                               upid_str));
                if (i)
                    assert(scte35_seg_desc_get_type_id(desc) ==
                           SCTE35_SEG_DESC_TYPE_ID_PROVIDER_PO_START);
                else
                    assert(scte35_seg_desc_get_type_id(desc) ==
                           SCTE35_SEG_DESC_TYPE_ID_BREAK_START);
                assert(scte35_seg_desc_get_num(desc) == 42 + i);
                assert(scte35_seg_desc_get_expected(desc) == 242);
            }
            assert(i == 2);
            break;
        }
        default:
            assert(0);
    }

    uref_block_unmap(uref, 0);
    uref_free(uref);
    round = 0;
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
static struct upipe_mgr ts_test_mgr = {
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
                                                         umem_mgr, 0, 0, -1, 0);
    assert(ubuf_mgr != NULL);
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_LEVEL);
    assert(logger != NULL);
    logger = uprobe_uref_mgr_alloc(logger, uref_mgr);
    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_POOL_DEPTH);
    assert(logger != NULL);

    struct uref *uref;
    uref = uref_alloc_control(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_flow_set_def(uref, "void.scte35."));

    struct upipe_mgr *upipe_ts_scte35g_mgr = upipe_ts_scte35g_mgr_alloc();
    assert(upipe_ts_scte35g_mgr != NULL);
    struct upipe *upipe_ts_scte35g = upipe_void_alloc(upipe_ts_scte35g_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "ts scte35g"));
    assert(upipe_ts_scte35g != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_scte35g, uref));
    ubase_assert(upipe_ts_mux_set_scte35_interval(upipe_ts_scte35g, UCLOCK_FREQ));
    uref_free(uref);

    struct upipe *upipe_sink = upipe_void_alloc_output(upipe_ts_scte35g,
            &ts_test_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "sink"));
    assert(upipe_sink != NULL);

    round = 1;
    ubase_assert(upipe_ts_mux_prepare(upipe_ts_scte35g, UCLOCK_FREQ, 0));
    assert(!round);

    uref = uref_alloc_control(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_ts_scte35_set_command_type(uref, SCTE35_INSERT_COMMAND));
    ubase_assert(uref_ts_scte35_set_event_id(uref, 4242));
    ubase_assert(uref_ts_scte35_set_out_of_network(uref));
    ubase_assert(uref_ts_scte35_set_auto_return(uref));
    ubase_assert(uref_ts_scte35_set_unique_program_id(uref, 1212));
    uref_clock_set_pts_sys(uref, UCLOCK_FREQ * 4);
    uref_clock_set_pts_prog(uref, UCLOCK_FREQ * 4);
    ubase_assert(uref_clock_set_duration(uref, UCLOCK_FREQ * 2));
    upipe_input(upipe_ts_scte35g, uref, NULL);

    round = 2;
    ubase_assert(upipe_ts_mux_prepare(upipe_ts_scte35g, UCLOCK_FREQ * 2, 0));
    assert(!round);

    round = 2;
    ubase_assert(upipe_ts_mux_prepare(upipe_ts_scte35g, UCLOCK_FREQ * 3, 0));
    assert(!round);

    round = 2;
    ubase_assert(upipe_ts_mux_prepare(upipe_ts_scte35g, UCLOCK_FREQ * 4, 0));
    assert(!round);

    round = 1;
    ubase_assert(upipe_ts_mux_prepare(upipe_ts_scte35g, UCLOCK_FREQ * 5, 0));
    assert(!round);

    uref = uref_alloc_control(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_ts_scte35_set_command_type(
            uref, SCTE35_TIME_SIGNAL_COMMAND));
    uref_clock_set_pts_sys(uref, UCLOCK_FREQ * 8);
    uref_clock_set_pts_prog(uref, UCLOCK_FREQ * 8);
    uref_clock_set_pts_orig(uref, UCLOCK_FREQ * 8);
    uref_block_set_start(uref);
    upipe_input(upipe_ts_scte35g, uref, NULL);

    uref = uref_alloc_control(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_ts_scte35_set_command_type(
            uref, SCTE35_TIME_SIGNAL_COMMAND));
    uref_clock_set_pts_sys(uref, UCLOCK_FREQ * 8);
    uref_clock_set_pts_prog(uref, UCLOCK_FREQ * 8);
    ubase_assert(uref_clock_set_duration(uref, UCLOCK_FREQ * 2));
    uref_ts_scte35_desc_set_tag(uref, SCTE35_SPLICE_DESC_TAG_SEG);
    uref_ts_scte35_desc_set_identifier(uref, 0x43554549);
    uref_ts_scte35_desc_seg_set_event_id(uref, 4242);
    uref_ts_scte35_desc_seg_set_web(uref);
    uref_ts_scte35_desc_seg_set_no_regional_blackout(uref);
    uref_ts_scte35_desc_seg_set_device(uref,
                                       SCTE35_SEG_DESC_DEVICE_RESTRICTION_NONE);
    uref_ts_scte35_desc_seg_set_upid_type(uref, SCTE35_SEG_DESC_UPID_TYPE_MPU);
    uref_ts_scte35_desc_seg_set_upid(uref,
                                     (uint8_t *)upid_str, strlen(upid_str) + 1);
    uref_ts_scte35_desc_seg_set_type_id(uref,
                                        SCTE35_SEG_DESC_TYPE_ID_BREAK_START);
    uref_ts_scte35_desc_seg_set_num(uref, 42);
    uref_ts_scte35_desc_seg_set_expected(uref, 242);
    upipe_input(upipe_ts_scte35g, uref, NULL);

    uref = uref_alloc_control(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_ts_scte35_set_command_type(
            uref, SCTE35_TIME_SIGNAL_COMMAND));
    uref_clock_set_pts_sys(uref, UCLOCK_FREQ * 8);
    uref_clock_set_pts_prog(uref, UCLOCK_FREQ * 8);
    ubase_assert(uref_clock_set_duration(uref, UCLOCK_FREQ * 2));
    uref_ts_scte35_desc_set_tag(uref, SCTE35_SPLICE_DESC_TAG_SEG);
    uref_ts_scte35_desc_set_identifier(uref, 0x43554549);
    uref_ts_scte35_desc_seg_set_event_id(uref, 4242 + 1);
    uref_ts_scte35_desc_seg_set_web(uref);
    uref_ts_scte35_desc_seg_set_no_regional_blackout(uref);
    uref_ts_scte35_desc_seg_set_device(uref,
                                       SCTE35_SEG_DESC_DEVICE_RESTRICTION_NONE);
    uref_ts_scte35_desc_seg_set_upid_type(uref, SCTE35_SEG_DESC_UPID_TYPE_MPU);
    uref_ts_scte35_desc_seg_set_upid(uref,
                                     (uint8_t *)upid_str, strlen(upid_str) + 1);
    uref_ts_scte35_desc_seg_set_type_id(
        uref, SCTE35_SEG_DESC_TYPE_ID_PROVIDER_PO_START);
    uref_ts_scte35_desc_seg_set_num(uref, 42 + 1);
    uref_ts_scte35_desc_seg_set_expected(uref, 242);
    uref_block_set_end(uref);
    upipe_input(upipe_ts_scte35g, uref, NULL);

    round = 3;
    ubase_assert(upipe_ts_mux_prepare(upipe_ts_scte35g, UCLOCK_FREQ * 6, 0));
    assert(!round);

    round = 3;
    ubase_assert(upipe_ts_mux_prepare(upipe_ts_scte35g, UCLOCK_FREQ * 7, 0));
    assert(!round);

    round = 3;
    ubase_assert(upipe_ts_mux_prepare(upipe_ts_scte35g, UCLOCK_FREQ * 8, 0));
    assert(!round);

    round = 1;
    ubase_assert(upipe_ts_mux_prepare(upipe_ts_scte35g, UCLOCK_FREQ * 9, 0));
    assert(!round);

    uref = uref_alloc_control(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_ts_scte35_set_command_type(
            uref, SCTE35_TIME_SIGNAL_COMMAND));
    uref_clock_set_pts_sys(uref, UCLOCK_FREQ * 12);
    uref_clock_set_pts_prog(uref, UCLOCK_FREQ * 12);
    uref_clock_set_pts_orig(uref, UCLOCK_FREQ * 12);
    uref_block_set_start(uref);
    upipe_input(upipe_ts_scte35g, uref, NULL);

    uref = uref_alloc_control(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_ts_scte35_set_command_type(
            uref, SCTE35_TIME_SIGNAL_COMMAND));
    uref_clock_set_pts_sys(uref, UCLOCK_FREQ * 12);
    uref_clock_set_pts_prog(uref, UCLOCK_FREQ * 12);
    uref_ts_scte35_desc_set_tag(uref, SCTE35_SPLICE_DESC_TAG_SEG);
    uref_ts_scte35_desc_set_identifier(uref, 0x43554549);
    uref_ts_scte35_desc_seg_set_event_id(uref, 4242);
    uref_ts_scte35_desc_seg_set_delivery_not_restricted(uref);
    uref_ts_scte35_desc_seg_set_upid_type(uref, SCTE35_SEG_DESC_UPID_TYPE_MPU);
    uref_ts_scte35_desc_seg_set_upid(uref,
                                     (uint8_t *)upid_str, strlen(upid_str) + 1);
    uref_ts_scte35_desc_seg_set_type_id(uref,
                                        SCTE35_SEG_DESC_TYPE_ID_BREAK_START);
    uref_ts_scte35_desc_seg_set_num(uref, 42);
    uref_ts_scte35_desc_seg_set_expected(uref, 242);
    upipe_input(upipe_ts_scte35g, uref, NULL);

    uref = uref_alloc_control(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_ts_scte35_set_command_type(
            uref, SCTE35_TIME_SIGNAL_COMMAND));
    uref_clock_set_pts_sys(uref, UCLOCK_FREQ * 12);
    uref_clock_set_pts_prog(uref, UCLOCK_FREQ * 12);
    uref_ts_scte35_desc_set_tag(uref, SCTE35_SPLICE_DESC_TAG_SEG);
    uref_ts_scte35_desc_set_identifier(uref, 0x43554549);
    uref_ts_scte35_desc_seg_set_event_id(uref, 4242 + 1);
    uref_ts_scte35_desc_seg_set_delivery_not_restricted(uref);
    uref_ts_scte35_desc_seg_set_upid_type(uref, SCTE35_SEG_DESC_UPID_TYPE_MPU);
    uref_ts_scte35_desc_seg_set_upid(uref,
                                     (uint8_t *)upid_str, strlen(upid_str) + 1);
    uref_ts_scte35_desc_seg_set_type_id(
        uref, SCTE35_SEG_DESC_TYPE_ID_PROVIDER_PO_START);
    uref_ts_scte35_desc_seg_set_num(uref, 42 + 1);
    uref_ts_scte35_desc_seg_set_expected(uref, 242);
    uref_ts_scte35_desc_seg_set_nb_comp(uref, 2);
    uref_ts_scte35_desc_seg_comp_set_tag(uref, 42, 0);
    uref_ts_scte35_desc_seg_comp_set_pts_off(uref, 1, 0);
    uref_ts_scte35_desc_seg_comp_set_tag(uref, 24, 1);
    uref_ts_scte35_desc_seg_comp_set_pts_off(uref, 2, 1);
    uref_block_set_end(uref);
    upipe_input(upipe_ts_scte35g, uref, NULL);

    round = 4;
    ubase_assert(upipe_ts_mux_prepare(upipe_ts_scte35g, UCLOCK_FREQ * 10, 0));
    assert(!round);

    round = 4;
    ubase_assert(upipe_ts_mux_prepare(upipe_ts_scte35g, UCLOCK_FREQ * 11, 0));
    assert(!round);

    round = 4;
    ubase_assert(upipe_ts_mux_prepare(upipe_ts_scte35g, UCLOCK_FREQ * 12, 0));
    assert(!round);

    round = 1;
    ubase_assert(upipe_ts_mux_prepare(upipe_ts_scte35g, UCLOCK_FREQ * 13, 0));
    assert(!round);

    upipe_release(upipe_ts_scte35g);
    upipe_mgr_release(upipe_ts_scte35g_mgr); // nop

    test_free(upipe_sink);

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe);

    return 0;
}
