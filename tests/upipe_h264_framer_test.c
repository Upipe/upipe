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
 *
 */

/** @file
 * @short unit tests for H264 video framer module
 * Unlike MPEG-2, H264 is so broad that it is hopeless to write a unit test
 * with a decent coverage. So for the moment just parse and dump an H264
 * elementary stream.
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/udict_dump.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_dump.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/uclock.h>
#include <upipe/upipe.h>
#include <upipe/upump.h>
#include <upipe-framers/upipe_h264_framer.h>
#include <upipe-framers/uref_h26x_flow.h>

#include "upipe_h264_framer_test.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include <bitstream/mpeg/h264.h>

#define UPROBE_LOG_LEVEL UPROBE_LOG_VERBOSE
#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UBUF_POOL_DEPTH 0
#define UBUF_SHARED_POOL_DEPTH 0
#define SPS_PPS_SIZE 33
#define AUD_SIZE 5

static unsigned int nb_packets = 0;
static bool need_global = false;
static enum uref_h26x_encaps need_encaps = UREF_H26X_ENCAPS_ANNEXB;
static struct uref *last_output = NULL;
static struct uref *last_flow_def = NULL;

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
        case UPROBE_SYNC_ACQUIRED:
        case UPROBE_SYNC_LOST:
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
    upipe_dbg_va(upipe, "frame: %u", nb_packets);
    uref_dump(uref, upipe->uprobe);
    uint64_t systime_rap = UINT64_MAX;
    uint64_t pts_orig = UINT64_MAX, dts_orig = UINT64_MAX;
    uref_clock_get_rap_sys(uref, &systime_rap);
    uref_clock_get_pts_orig(uref, &pts_orig);
    uref_clock_get_dts_orig(uref, &dts_orig);
    assert(systime_rap == 42);
    assert(pts_orig == 27000000);
    assert(dts_orig == 27000000);
    size_t size;
    ubase_assert(uref_block_size(uref, &size));
    upipe_dbg_va(upipe, "size: %zu", size);
    switch (nb_packets) {
        case 0:
        case 1:
        case 2:
            assert(size == sizeof(h264_headers) + sizeof(h264_pic) + AUD_SIZE);
            break;
        case 3:
        case 4:
            /* + 3 to account for 3-octet annex B startcode */
            assert(size == sizeof(h264_headers) + sizeof(h264_pic) + 3);
            break;
        case 5:
            assert(size == sizeof(h264_headers) + sizeof(h264_pic) + AUD_SIZE + 3);
            break;
        case 6:
            assert(size == sizeof(h264_pic) + AUD_SIZE + SPS_PPS_SIZE + 4 * 2);
            break;
        default:
            assert(0);
            break;
    }
    uref_free(last_output);
    last_output = uref;
    nb_packets++;
}

/** helper phony pipe */
static int test_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            const uint8_t *headers;
            size_t size;
            int err1 = uref_flow_get_headers(flow_def, &headers, &size);
            int err2 = uref_flow_get_global(flow_def);
            uint8_t encaps;
            int err3 = uref_h26x_flow_get_encaps(flow_def, &encaps);
            ubase_assert(err3);
            assert(encaps == need_encaps);
            if (need_global) {
                assert(ubase_check(err1));
                assert(ubase_check(err2));
                if (encaps == UREF_H26X_ENCAPS_ANNEXB)
                    assert(size == SPS_PPS_SIZE + 8);
                else
                    assert(size == SPS_PPS_SIZE +
                            H264AVCC_HEADER + H264AVCC_HEADER2 +
                            H264AVCC_SPS_HEADER + H264AVCC_PPS_HEADER);
            } else {
                assert(!ubase_check(err1));
                assert(!ubase_check(err2));
            }
            uref_free(last_flow_def);
            last_flow_def = uref_dup(flow_def);
            return UBASE_ERR_NONE;
        }
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *urequest = va_arg(args, struct urequest *);
            if (urequest->type == UREQUEST_FLOW_FORMAT) {
                struct uref *uref = uref_dup(urequest->uref);
                assert(uref != NULL);
                if (need_global)
                    ubase_assert(uref_flow_set_global(uref));
                else
                    uref_flow_delete_global(uref);
                ubase_assert(uref_h26x_flow_set_encaps(uref, need_encaps));
                return urequest_provide_flow_format(urequest, uref);
            }
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

int main(int argc, char **argv)
{
    /* structures managers */
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

    /* probes */
    struct uprobe uprobe_s;
    uprobe_init(&uprobe_s, catch, NULL);
    struct uprobe *uprobe;
    uprobe = uprobe_stdio_alloc(&uprobe_s, stdout, UPROBE_LOG_LEVEL);
    assert(uprobe != NULL);
    uprobe = uprobe_uref_mgr_alloc(uprobe, uref_mgr);
    assert(uprobe != NULL);
    uprobe = uprobe_ubuf_mem_alloc(uprobe, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_SHARED_POOL_DEPTH);
    assert(uprobe != NULL);

    struct upipe *sink = upipe_void_alloc(&test_mgr, uprobe_use(uprobe));
    assert(sink != NULL);

    struct uref *flow_def = uref_block_flow_alloc_def(uref_mgr, "h264.pic.");
    assert(flow_def != NULL);
    uref_h26x_flow_set_encaps(flow_def, UREF_H26X_ENCAPS_ANNEXB);

    struct upipe_mgr *h264f_mgr = upipe_h264f_mgr_alloc();
    assert(h264f_mgr != NULL);
    struct upipe *h264f = upipe_void_alloc(h264f_mgr,
                   uprobe_pfx_alloc(uprobe_use(uprobe), UPROBE_LOG_VERBOSE,
                                    "h264f 1"));
    assert(h264f != NULL);
    ubase_assert(upipe_set_output(h264f, sink));
    ubase_assert(upipe_set_flow_def(h264f, flow_def));

    /* build two urefs with headers + picture */
    struct ubuf *ubuf1 = ubuf_block_alloc_from_opaque(ubuf_mgr, h264_headers,
                                                      sizeof(h264_headers));
    assert(ubuf1 != NULL);
    struct ubuf *ubuf2 = ubuf_block_alloc_from_opaque(ubuf_mgr, h264_pic,
                                                      sizeof(h264_pic));
    assert(ubuf2 != NULL);
    struct uref *uref1 = uref_alloc(uref_mgr);
    assert(uref1 != NULL);
    uref_attach_ubuf(uref1, ubuf_dup(ubuf1));
    struct uref *uref2 = uref_alloc(uref_mgr);
    assert(uref2 != NULL);
    uref_attach_ubuf(uref2, ubuf_dup(ubuf2));
    uref_clock_set_dts_orig(uref1, 27000000);
    uref_clock_set_dts_pts_delay(uref1, 0);
    uref_clock_set_cr_sys(uref1, 84);
    uref_clock_set_rap_sys(uref1, 42);
    upipe_input(h264f, uref1, NULL);
    assert(nb_packets == 0);
    upipe_input(h264f, uref2, NULL);
    assert(nb_packets == 0);
    upipe_release(h264f);
    assert(nb_packets == 1);

    /* build a urefs with headers + picture and signal complete AU */
    ubase_assert(uref_flow_set_complete(flow_def));
    h264f = upipe_void_alloc(h264f_mgr,
                   uprobe_pfx_alloc(uprobe_use(uprobe), UPROBE_LOG_VERBOSE,
                                    "h264f 2"));
    assert(h264f != NULL);
    ubase_assert(upipe_set_output(h264f, sink));
    ubase_assert(upipe_set_flow_def(h264f, flow_def));

    /* build a uref with headers + picture */
    ubuf_block_append(ubuf1, ubuf2);
    struct uref *uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    uref_attach_ubuf(uref, ubuf1);
    uref_clock_set_dts_orig(uref, 27000000);
    uref_clock_set_dts_pts_delay(uref, 0);
    uref_clock_set_cr_sys(uref, 84);
    uref_clock_set_rap_sys(uref, 42);
    upipe_input(h264f, uref_dup(uref), NULL);
    assert(nb_packets == 2);
    upipe_release(h264f);

    /* Request annex B global headers */
    h264f = upipe_void_alloc(h264f_mgr,
                   uprobe_pfx_alloc(uprobe_use(uprobe), UPROBE_LOG_VERBOSE,
                                    "h264f 3"));
    assert(h264f != NULL);
    ubase_assert(upipe_set_output(h264f, sink));
    ubase_assert(upipe_set_flow_def(h264f, flow_def));

    need_global = true;
    uref_clock_set_dts_orig(uref, 27000000);
    uref_clock_set_dts_pts_delay(uref, 0);
    uref_clock_set_cr_sys(uref, 84);
    uref_clock_set_rap_sys(uref, 42);
    upipe_input(h264f, uref_dup(uref), NULL);
    assert(nb_packets == 3);
    upipe_release(h264f);

    /* Request length startcodes */
    h264f = upipe_void_alloc(h264f_mgr,
                   uprobe_pfx_alloc(uprobe_use(uprobe), UPROBE_LOG_VERBOSE,
                                    "h264f 4"));
    assert(h264f != NULL);
    ubase_assert(upipe_set_output(h264f, sink));
    ubase_assert(upipe_set_flow_def(h264f, flow_def));

    need_encaps = UREF_H26X_ENCAPS_LENGTH4;
    need_global = true;
    uref_clock_set_dts_orig(uref, 27000000);
    uref_clock_set_dts_pts_delay(uref, 0);
    uref_clock_set_cr_sys(uref, 84);
    uref_clock_set_rap_sys(uref, 42);
    upipe_input(h264f, uref_dup(uref), NULL);
    assert(nb_packets == 4);
    upipe_release(h264f);

    /* Length -> length */
    uref_free(uref);
    uref = uref_dup(last_output);
    assert(uref != NULL);
    h264f = upipe_void_alloc(h264f_mgr,
                   uprobe_pfx_alloc(uprobe_use(uprobe), UPROBE_LOG_VERBOSE,
                                    "h264f 5"));
    assert(h264f != NULL);
    ubase_assert(upipe_set_output(h264f, sink));
    ubase_assert(upipe_set_flow_def(h264f, last_flow_def));

    need_encaps = UREF_H26X_ENCAPS_LENGTH4;
    need_global = true;
    uref_clock_set_dts_orig(uref, 27000000);
    uref_clock_set_dts_pts_delay(uref, 0);
    uref_clock_set_cr_sys(uref, 84);
    uref_clock_set_rap_sys(uref, 42);
    upipe_input(h264f, uref_dup(uref), NULL);
    assert(nb_packets == 5);
    upipe_release(h264f);

    /* Length -> annex B */
    h264f = upipe_void_alloc(h264f_mgr,
                   uprobe_pfx_alloc(uprobe_use(uprobe), UPROBE_LOG_VERBOSE,
                                    "h264f 6"));
    assert(h264f != NULL);
    ubase_assert(upipe_set_output(h264f, sink));
    ubase_assert(upipe_set_flow_def(h264f, last_flow_def));

    need_encaps = UREF_H26X_ENCAPS_ANNEXB;
    need_global = false;
    uref_clock_set_dts_orig(uref, 27000000);
    uref_clock_set_dts_pts_delay(uref, 0);
    uref_clock_set_cr_sys(uref, 84);
    uref_clock_set_rap_sys(uref, 42);
    upipe_input(h264f, uref_dup(uref), NULL);
    assert(nb_packets == 6);
    upipe_release(h264f);
    uref_free(uref);

    /* annex B global headers -> !global headers */
    ubase_assert(uref_flow_set_headers(flow_def, h264_headers,
                                       sizeof(h264_headers)));
    h264f = upipe_void_alloc(h264f_mgr,
                   uprobe_pfx_alloc(uprobe_use(uprobe), UPROBE_LOG_VERBOSE,
                                    "h264f 7"));
    assert(h264f != NULL);
    ubase_assert(upipe_set_output(h264f, sink));
    ubase_assert(upipe_set_flow_def(h264f, flow_def));

    struct ubuf *ubuf = ubuf_block_alloc_from_opaque(ubuf_mgr, h264_pic,
                                                     sizeof(h264_pic));
    assert(ubuf != NULL);
    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    uref_attach_ubuf(uref, ubuf);
    need_encaps = UREF_H26X_ENCAPS_ANNEXB;
    need_global = false;
    uref_clock_set_dts_orig(uref, 27000000);
    uref_clock_set_dts_pts_delay(uref, 0);
    uref_clock_set_cr_sys(uref, 84);
    uref_clock_set_rap_sys(uref, 42);
    upipe_input(h264f, uref, NULL);
    assert(nb_packets == 7);
    upipe_release(h264f);

    uref_free(flow_def);
    uref_free(last_output);
    uref_free(last_flow_def);
    test_free(sink);

    upipe_mgr_release(h264f_mgr);
    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(uprobe);
    uprobe_clean(&uprobe_s);

    return 0;
}
