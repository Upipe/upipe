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
 * @short unit tests for MPEG-2 video framer module
 */

#undef NDEBUG

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
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_std.h>
#include <upipe/uref_dump.h>
#include <upipe/upipe.h>
#include <upipe-framers/upipe_mpgv_framer.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>

#include <bitstream/mpeg/mp2v.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UBUF_POOL_DEPTH 0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

static unsigned int nb_packets = 0;

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
    size_t size;
    ubase_assert(uref_block_size(uref, &size));
    uint64_t systime_rap = UINT64_MAX;
    uint64_t pts_orig = UINT64_MAX, dts_orig = UINT64_MAX;
    uref_clock_get_rap_sys(uref, &systime_rap);
    uref_clock_get_pts_orig(uref, &pts_orig);
    uref_clock_get_dts_orig(uref, &dts_orig);
    switch (nb_packets) {
        case 0:
            assert(size == MP2VSEQ_HEADER_SIZE + MP2VSEQX_HEADER_SIZE +
                    MP2VPIC_HEADER_SIZE + MP2VPICX_HEADER_SIZE + 4);
            assert(systime_rap == 42);
            assert(pts_orig == 27000000);
            assert(dts_orig == 27000000);
            break;
        case 1:
            assert(size == MP2VPIC_HEADER_SIZE + MP2VPICX_HEADER_SIZE + 4 +
                    MP2VEND_HEADER_SIZE);
            assert(systime_rap == 42);
            assert(pts_orig == UINT64_MAX);
            assert(dts_orig == 27000000 + 40 * 27000);
            break;
        default:
            assert(0);
    }
    uref_free(uref);
    nb_packets++;
}

/** helper phony pipe */
static int test_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            uref_dump(flow_def, upipe->uprobe);
            return UBASE_ERR_NONE;
        }
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
    uref = uref_block_flow_alloc_def(uref_mgr, "mpeg2video.pic.");
    assert(uref != NULL);

    struct upipe *upipe_sink = upipe_void_alloc(&test_mgr,
                                                uprobe_use(uprobe_stdio));
    assert(upipe_sink != NULL);

    struct upipe_mgr *upipe_mpgvf_mgr = upipe_mpgvf_mgr_alloc();
    assert(upipe_mpgvf_mgr != NULL);
    struct upipe *upipe_mpgvf = upipe_void_alloc(upipe_mpgvf_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "mpgvf"));
    assert(upipe_mpgvf != NULL);
    ubase_assert(upipe_set_flow_def(upipe_mpgvf, uref));
    ubase_assert(upipe_set_output(upipe_mpgvf, upipe_sink));
    uref_free(uref);

    uint8_t *buffer;
    int size;

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 42 +
            MP2VSEQ_HEADER_SIZE + MP2VSEQX_HEADER_SIZE +
            (MP2VPIC_HEADER_SIZE + MP2VPICX_HEADER_SIZE + 4) * 2 +
            MP2VEND_HEADER_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == 42 + MP2VSEQ_HEADER_SIZE + MP2VSEQX_HEADER_SIZE +
            (MP2VPIC_HEADER_SIZE + MP2VPICX_HEADER_SIZE + 4) * 2 +
            MP2VEND_HEADER_SIZE);
    memset(buffer, 0, 42);

    buffer += 42;
    mp2vseq_init(buffer);
    mp2vseq_set_horizontal(buffer, 720);
    mp2vseq_set_vertical(buffer, 576);
    mp2vseq_set_aspect(buffer, MP2VSEQ_ASPECT_16_9);
    mp2vseq_set_framerate(buffer, MP2VSEQ_FRAMERATE_25);
    mp2vseq_set_bitrate(buffer, 2000000/400);
    mp2vseq_set_vbvbuffer(buffer, 1835008/16/1024);
    buffer += MP2VSEQ_HEADER_SIZE;

    mp2vseqx_init(buffer);
    mp2vseqx_set_profilelevel(buffer,
                              MP2VSEQX_PROFILE_MAIN | MP2VSEQX_LEVEL_MAIN);
    mp2vseqx_set_chroma(buffer, MP2VSEQX_CHROMA_420);
    mp2vseqx_set_horizontal(buffer, 0);
    mp2vseqx_set_vertical(buffer, 0);
    mp2vseqx_set_bitrate(buffer, 0);
    mp2vseqx_set_vbvbuffer(buffer, 0);
    buffer += MP2VSEQX_HEADER_SIZE;

    mp2vpic_init(buffer);
    mp2vpic_set_temporalreference(buffer, 0);
    mp2vpic_set_codingtype(buffer, MP2VPIC_TYPE_I);
    mp2vpic_set_vbvdelay(buffer, UINT16_MAX);
    buffer += MP2VPIC_HEADER_SIZE;

    mp2vpicx_init(buffer);
    mp2vpicx_set_fcode00(buffer, 0);
    mp2vpicx_set_fcode01(buffer, 0);
    mp2vpicx_set_fcode10(buffer, 0);
    mp2vpicx_set_fcode11(buffer, 0);
    mp2vpicx_set_intradc(buffer, 0);
    mp2vpicx_set_structure(buffer, MP2VPICX_FRAME_PICTURE);
    mp2vpicx_set_tff(buffer);
    buffer += MP2VPICX_HEADER_SIZE;

    mp2vstart_init(buffer, 1);
    buffer += 4;

    mp2vpic_init(buffer);
    mp2vpic_set_temporalreference(buffer, 2);
    mp2vpic_set_codingtype(buffer, MP2VPIC_TYPE_P);
    mp2vpic_set_vbvdelay(buffer, UINT16_MAX);
    buffer += MP2VPIC_HEADER_SIZE;

    mp2vpicx_init(buffer);
    mp2vpicx_set_fcode00(buffer, 0);
    mp2vpicx_set_fcode01(buffer, 0);
    mp2vpicx_set_fcode10(buffer, 0);
    mp2vpicx_set_fcode11(buffer, 0);
    mp2vpicx_set_intradc(buffer, 0);
    mp2vpicx_set_structure(buffer, MP2VPICX_FRAME_PICTURE);
    mp2vpicx_set_tff(buffer);
    buffer += MP2VPICX_HEADER_SIZE;

    mp2vstart_init(buffer, 1);
    buffer += 4;

    mp2vend_init(buffer);
    uref_block_unmap(uref, 0);
    uref_clock_set_dts_orig(uref, 27000000);
    uref_clock_set_dts_pts_delay(uref, 0);
    uref_clock_set_cr_sys(uref, 84);
    uref_clock_set_rap_sys(uref, 42);
    upipe_input(upipe_mpgvf, uref, NULL);
    assert(nb_packets == 2);

    upipe_release(upipe_mpgvf);
    upipe_mgr_release(upipe_mpgvf_mgr);

    test_free(upipe_sink);

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(uprobe_stdio);
    uprobe_clean(&uprobe);

    return 0;
}
