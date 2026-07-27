/*
 * Copyright (C) 2026 EasyTools
 *
 * Authors: Clément Vasseur
 *
 * SPDX-License-Identifier: MIT
 *
 */

/** @file
 * @short unit tests for H.265 video framer module
 * H.265 is so broad that it is hopeless to write a unit test with a decent
 * coverage. So for the moment just parse and dump an H.265 elementary stream.
 */

#undef NDEBUG

#include "upipe/uprobe.h"
#include "upipe/uprobe_stdio.h"
#include "upipe/uprobe_prefix.h"
#include "upipe/uprobe_uref_mgr.h"
#include "upipe/uprobe_ubuf_mem.h"
#include "upipe/umem.h"
#include "upipe/umem_alloc.h"
#include "upipe/udict.h"
#include "upipe/udict_inline.h"
#include "upipe/uref.h"
#include "upipe/uref_std.h"
#include "upipe/uref_flow.h"
#include "upipe/uref_clock.h"
#include "upipe/uref_block.h"
#include "upipe/uref_block_flow.h"
#include "upipe/uref_dump.h"
#include "upipe/ubuf.h"
#include "upipe/ubuf_block_mem.h"
#include "upipe/upipe.h"
#include "upipe-framers/upipe_h265_framer.h"
#include "upipe-framers/uref_h26x_flow.h"

#include "upipe_h265_framer_test.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include <bitstream/itu/h265.h>

#define UPROBE_LOG_LEVEL UPROBE_LOG_VERBOSE
#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UBUF_POOL_DEPTH 0
#define UBUF_SHARED_POOL_DEPTH 0
/* size of the VPS + SPS + PPS NAL payloads (without start codes) */
#define VPS_SPS_PPS_SIZE 68
/* annex B access unit delimiter: start code + 2-octet NAL header */
#define AUD_SIZE 6

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
            /* annex B output: full access unit + prepended AUD */
            assert(size == sizeof(h265_pic) + AUD_SIZE);
            break;
        case 3:
        case 4:
            /* length startcodes: + 1 to widen the 3-octet annex B start
             * code of the slice into a 4-octet length prefix */
            assert(size == sizeof(h265_pic) + 1);
            break;
        case 5:
            /* length -> annex B: length prefixes back to start codes + AUD */
            assert(size == sizeof(h265_pic) + 1 + AUD_SIZE);
            break;
        case 6:
            assert(size == sizeof(h265_pic) + AUD_SIZE);
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
                    assert(size == VPS_SPS_PPS_SIZE + 4 * 3);
                else
                    assert(size == VPS_SPS_PPS_SIZE +
                           H265HVCC_HEADER + 3 * H265HVCC_ARRAY_HEADER +
                           3 * H265HVCC_NALU_HEADER);
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

    struct uref *flow_def = uref_block_flow_alloc_def(uref_mgr, "hevc.pic.");
    assert(flow_def != NULL);
    uref_h26x_flow_set_encaps(flow_def, UREF_H26X_ENCAPS_ANNEXB);

    struct upipe_mgr *h265f_mgr = upipe_h265f_mgr_alloc();
    assert(h265f_mgr != NULL);
    struct upipe *h265f = upipe_void_alloc(h265f_mgr,
                   uprobe_pfx_alloc(uprobe_use(uprobe), UPROBE_LOG_VERBOSE,
                                    "h265f 1"));
    assert(h265f != NULL);
    ubase_assert(upipe_set_output(h265f, sink));
    ubase_assert(upipe_set_flow_def(h265f, flow_def));

    /* build a uref with a full access unit (headers + picture) */
    struct ubuf *ubuf = ubuf_block_alloc_from_opaque(ubuf_mgr, h265_pic,
                                                     sizeof(h265_pic));
    assert(ubuf != NULL);
    struct uref *uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    uref_attach_ubuf(uref, ubuf);
    uref_clock_set_dts_orig(uref, 27000000);
    uref_clock_set_dts_pts_delay(uref, 0);
    uref_clock_set_cr_sys(uref, 84);
    uref_clock_set_rap_sys(uref, 42);
    upipe_input(h265f, uref_dup(uref), NULL);
    assert(nb_packets == 0);
    upipe_release(h265f);
    assert(nb_packets == 1);

    /* signal complete AU */
    ubase_assert(uref_flow_set_complete(flow_def));
    h265f = upipe_void_alloc(h265f_mgr,
                   uprobe_pfx_alloc(uprobe_use(uprobe), UPROBE_LOG_VERBOSE,
                                    "h265f 2"));
    assert(h265f != NULL);
    ubase_assert(upipe_set_output(h265f, sink));
    ubase_assert(upipe_set_flow_def(h265f, flow_def));

    uref_clock_set_dts_orig(uref, 27000000);
    uref_clock_set_dts_pts_delay(uref, 0);
    uref_clock_set_cr_sys(uref, 84);
    uref_clock_set_rap_sys(uref, 42);
    upipe_input(h265f, uref_dup(uref), NULL);
    assert(nb_packets == 2);
    upipe_release(h265f);

    /* Request annex B global headers */
    h265f = upipe_void_alloc(h265f_mgr,
                   uprobe_pfx_alloc(uprobe_use(uprobe), UPROBE_LOG_VERBOSE,
                                    "h265f 3"));
    assert(h265f != NULL);
    ubase_assert(upipe_set_output(h265f, sink));
    ubase_assert(upipe_set_flow_def(h265f, flow_def));

    need_global = true;
    uref_clock_set_dts_orig(uref, 27000000);
    uref_clock_set_dts_pts_delay(uref, 0);
    uref_clock_set_cr_sys(uref, 84);
    uref_clock_set_rap_sys(uref, 42);
    upipe_input(h265f, uref_dup(uref), NULL);
    assert(nb_packets == 3);
    upipe_release(h265f);

    /* Request length startcodes */
    h265f = upipe_void_alloc(h265f_mgr,
                   uprobe_pfx_alloc(uprobe_use(uprobe), UPROBE_LOG_VERBOSE,
                                    "h265f 4"));
    assert(h265f != NULL);
    ubase_assert(upipe_set_output(h265f, sink));
    ubase_assert(upipe_set_flow_def(h265f, flow_def));

    need_encaps = UREF_H26X_ENCAPS_LENGTH4;
    need_global = true;
    uref_clock_set_dts_orig(uref, 27000000);
    uref_clock_set_dts_pts_delay(uref, 0);
    uref_clock_set_cr_sys(uref, 84);
    uref_clock_set_rap_sys(uref, 42);
    upipe_input(h265f, uref_dup(uref), NULL);
    assert(nb_packets == 4);
    upipe_release(h265f);

    /* Length -> length */
    uref_free(uref);
    uref = uref_dup(last_output);
    assert(uref != NULL);
    h265f = upipe_void_alloc(h265f_mgr,
                   uprobe_pfx_alloc(uprobe_use(uprobe), UPROBE_LOG_VERBOSE,
                                    "h265f 5"));
    assert(h265f != NULL);
    ubase_assert(upipe_set_output(h265f, sink));
    ubase_assert(upipe_set_flow_def(h265f, last_flow_def));

    need_encaps = UREF_H26X_ENCAPS_LENGTH4;
    need_global = true;
    uref_clock_set_dts_orig(uref, 27000000);
    uref_clock_set_dts_pts_delay(uref, 0);
    uref_clock_set_cr_sys(uref, 84);
    uref_clock_set_rap_sys(uref, 42);
    upipe_input(h265f, uref_dup(uref), NULL);
    assert(nb_packets == 5);
    upipe_release(h265f);

    /* Length -> annex B */
    h265f = upipe_void_alloc(h265f_mgr,
                   uprobe_pfx_alloc(uprobe_use(uprobe), UPROBE_LOG_VERBOSE,
                                    "h265f 6"));
    assert(h265f != NULL);
    ubase_assert(upipe_set_output(h265f, sink));
    ubase_assert(upipe_set_flow_def(h265f, last_flow_def));

    need_encaps = UREF_H26X_ENCAPS_ANNEXB;
    need_global = false;
    uref_clock_set_dts_orig(uref, 27000000);
    uref_clock_set_dts_pts_delay(uref, 0);
    uref_clock_set_cr_sys(uref, 84);
    uref_clock_set_rap_sys(uref, 42);
    upipe_input(h265f, uref_dup(uref), NULL);
    assert(nb_packets == 6);
    upipe_release(h265f);
    uref_free(uref);

    /* annex B global headers -> !global headers */
    ubase_assert(uref_flow_set_headers(flow_def, h265_headers,
                                       sizeof(h265_headers)));
    h265f = upipe_void_alloc(h265f_mgr,
                   uprobe_pfx_alloc(uprobe_use(uprobe), UPROBE_LOG_VERBOSE,
                                    "h265f 7"));
    assert(h265f != NULL);
    ubase_assert(upipe_set_output(h265f, sink));
    ubase_assert(upipe_set_flow_def(h265f, flow_def));

    ubuf = ubuf_block_alloc_from_opaque(ubuf_mgr, h265_pic, sizeof(h265_pic));
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
    upipe_input(h265f, uref, NULL);
    assert(nb_packets == 7);
    upipe_release(h265f);

    uref_free(flow_def);
    uref_free(last_output);
    uref_free(last_flow_def);
    test_free(sink);

    upipe_mgr_release(h265f_mgr);
    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(uprobe);
    uprobe_clean(&uprobe_s);

    return 0;
}
