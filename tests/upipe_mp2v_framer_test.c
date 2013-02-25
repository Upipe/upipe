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
 * @short unit tests for MPEG-2 video framer module
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_log.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/udict_dump.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_std.h>
#include <upipe/upipe.h>
#include <upipe-framers/upipe_mp2v_framer.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>

#include <bitstream/mpeg/mp2v.h>

#define UDICT_POOL_DEPTH 10
#define UREF_POOL_DEPTH 10
#define UBUF_POOL_DEPTH 10
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

static unsigned int nb_packets = 0;

/** definition of our uprobe */
static bool catch(struct uprobe *uprobe, struct upipe *upipe,
                  enum uprobe_event event, va_list args)
{
    switch (event) {
        default:
            assert(0);
            break;
        case UPROBE_READY:
        case UPROBE_DEAD:
        case UPROBE_SYNC_ACQUIRED:
        case UPROBE_SYNC_LOST:
            break;
    }
    return true;
}

/** helper phony pipe to test upipe_mp2vf */
static struct upipe *test_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_init(upipe, mgr, uprobe);
    return upipe;
}

/** helper phony pipe to test upipe_mp2vf */
static void test_input(struct upipe *upipe, struct uref *uref,
                       struct upump *upump)
{
    assert(uref != NULL);
    const char *def;
    if (uref_flow_get_def(uref, &def)) {
        upipe_dbg_va(upipe, "flow def: %s", def);
        udict_dump(uref->udict, upipe->uprobe);
        uref_free(uref);
        return;
    }

    if (unlikely(uref_flow_get_end(uref))) {
        uref_free(uref);
        return;
    }

    upipe_dbg_va(upipe, "frame: %u", nb_packets);
    udict_dump(uref->udict, upipe->uprobe);
    size_t size;
    assert(uref_block_size(uref, &size));
    uint64_t systime_rap = UINT64_MAX;
    uint64_t pts_orig = UINT64_MAX, dts_orig = UINT64_MAX;
    uref_clock_get_systime_rap(uref, &systime_rap);
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

/** helper phony pipe to test upipe_mp2vf */
static void test_free(struct upipe *upipe)
{
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe to test upipe_mp2vf */
static struct upipe_mgr test_mgr = {
    .upipe_alloc = test_alloc,
    .upipe_input = test_input,
    .upipe_control = NULL,
    .upipe_free = NULL,

    .upipe_mgr_free = NULL
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
                                                         umem_mgr, -1, -1,
                                                         -1, 0);
    assert(ubuf_mgr != NULL);
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_LEVEL);
    assert(uprobe_stdio != NULL);
    struct uprobe *log = uprobe_log_alloc(uprobe_stdio, UPROBE_LOG_LEVEL);
    assert(log != NULL);

    struct upipe *upipe_sink = upipe_alloc(&test_mgr, log);
    assert(upipe_sink != NULL);

    struct upipe_mgr *upipe_mp2vf_mgr = upipe_mp2vf_mgr_alloc();
    assert(upipe_mp2vf_mgr != NULL);
    struct upipe *upipe_mp2vf = upipe_alloc(upipe_mp2vf_mgr,
            uprobe_pfx_adhoc_alloc(log, UPROBE_LOG_LEVEL, "mp2vf"));
    assert(upipe_mp2vf != NULL);
    assert(upipe_set_output(upipe_mp2vf, upipe_sink));

    struct uref *uref;
    uint8_t *buffer;
    int size;
    uref = uref_block_flow_alloc_def(uref_mgr, "mpeg2video.");
    assert(uref != NULL);
    upipe_input(upipe_mp2vf, uref, NULL);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 42 +
            MP2VSEQ_HEADER_SIZE + MP2VSEQX_HEADER_SIZE +
            (MP2VPIC_HEADER_SIZE + MP2VPICX_HEADER_SIZE + 4) * 2 +
            MP2VEND_HEADER_SIZE);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
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
    uref_block_unmap(uref, 0, size);
    uref_clock_set_pts_orig(uref, 27000000);
    uref_clock_set_dts_orig(uref, 27000000);
    uref_clock_set_systime(uref, 84);
    uref_clock_set_systime_rap(uref, 42);
    upipe_input(upipe_mp2vf, uref, NULL);
    assert(nb_packets == 2);

    upipe_release(upipe_mp2vf);
    upipe_mgr_release(upipe_mp2vf_mgr);

    test_free(upipe_sink);

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_log_free(log);
    uprobe_stdio_free(uprobe_stdio);

    return 0;
}
