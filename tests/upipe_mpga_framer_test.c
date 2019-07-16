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
 */

/** @file
 * @short unit tests for MPEG-1 1/2/3 and AAC audio framer module
 */

#undef NDEBUG

#include <upipe/ubits.h>
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
#include <upipe-framers/upipe_mpga_framer.h>
#include <upipe-framers/uref_mpga_flow.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>

#include <bitstream/mpeg/mpga.h>
#include <bitstream/mpeg/aac.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UBUF_POOL_DEPTH 0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

static unsigned int nb_packets = 0;
static bool need_global = false;
static enum uref_mpga_encaps need_encaps = UREF_MPGA_ENCAPS_ADTS;
static struct uref *last_output = NULL;

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

static void check_data(struct uref *uref, size_t offset, size_t size)
{
    int read_size = size;
    const uint8_t *r;
    ubase_assert(uref_block_read(uref, offset, &read_size, &r));
    assert(read_size == size);
    for (int i = 0; i < size; i++) {
        assert(r[i] == i % 256);
    }
    uref_block_unmap(uref, offset);
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
            assert(size == 768);
            check_data(uref, MPGA_HEADER_SIZE, 768 - MPGA_HEADER_SIZE);
            break;
        case 1:
        case 2:
            assert(size == 768);
            check_data(uref, ADTS_HEADER_SIZE, 768 - ADTS_HEADER_SIZE);
            break;
        case 3:
        case 4:
        case 7:
        case 10:
            assert(size == 768 - ADTS_HEADER_SIZE);
            check_data(uref, 0, 768 - ADTS_HEADER_SIZE);
            break;
        case 5:
        case 6:
            assert(size == 768 - ADTS_HEADER_SIZE + 12);
            break;
        case 8:
        case 9:
            assert(size == 768 - ADTS_HEADER_SIZE + 9);
            break;
        default:
            assert(0);
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
            int err3 = uref_mpga_flow_get_encaps(flow_def, &encaps);
            ubase_assert(err3);
            assert(encaps == need_encaps);
            if (need_global) {
                assert(ubase_check(err1));
                assert(ubase_check(err2));
            } else {
                assert(!ubase_check(err1));
                assert(!ubase_check(err2));
            }
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
                ubase_assert(uref_mpga_flow_set_encaps(uref, need_encaps));
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

static void write_mpga(uint8_t *buffer)
{
    mpga_set_sync(buffer);
    mpga_set_layer(buffer, MPGA_LAYER_2);
    mpga_set_bitrate_index(buffer, 0xc); /* 256 kbits/s */
    mpga_set_sampling_freq(buffer, 0x1); /* 48 kHz */
    mpga_set_mode(buffer, MPGA_MODE_STEREO);
}

static void write_adts(uint8_t *buffer)
{
    adts_set_sync(buffer);
    adts_set_sampling_freq(buffer, 0x3); /* 48 kHz */
    adts_set_channels(buffer, 2);
    adts_set_length(buffer, 768);
    adts_set_num_blocks(buffer, 0);
}

static void write_data(uint8_t *buffer, size_t size)
{
    for (int i = 0; i < size; i++)
        buffer[i] = i % 256;
}

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
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_LEVEL);
    assert(uprobe_stdio != NULL);
    uprobe_stdio = uprobe_ubuf_mem_alloc(uprobe_stdio, umem_mgr,
                                         UBUF_POOL_DEPTH, UBUF_POOL_DEPTH);
    assert(uprobe_stdio != NULL);

    struct uref *uref;
    uref = uref_block_flow_alloc_def(uref_mgr, "mp2.sound.");
    assert(uref != NULL);

    struct upipe *upipe_sink = upipe_void_alloc(&test_mgr,
                                                uprobe_use(uprobe_stdio));
    assert(upipe_sink != NULL);

    struct upipe_mgr *upipe_mpgaf_mgr = upipe_mpgaf_mgr_alloc();
    assert(upipe_mpgaf_mgr != NULL);
    struct upipe *upipe_mpgaf = upipe_void_alloc(upipe_mpgaf_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "mpgaf 1"));
    assert(upipe_mpgaf != NULL);
    ubase_assert(upipe_set_flow_def(upipe_mpgaf, uref));
    ubase_assert(upipe_set_output(upipe_mpgaf, upipe_sink));
    uref_free(uref);

    uint8_t *buffer;
    int size;

    /* MPEG-1 */
    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 42 + 768 + MPGA_HEADER_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == 42 + 768 + MPGA_HEADER_SIZE);
    memset(buffer, 0, 42 + 768 + MPGA_HEADER_SIZE);

    buffer += 42;
    write_mpga(buffer);
    write_data(buffer + MPGA_HEADER_SIZE, 768 - MPGA_HEADER_SIZE);

    buffer += 768;
    write_mpga(buffer);

    uref_block_unmap(uref, 0);
    uref_clock_set_pts_orig(uref, 27000000);
    uref_clock_set_dts_orig(uref, 27000000);
    uref_clock_set_cr_sys(uref, 84);
    uref_clock_set_rap_sys(uref, 42);
    upipe_input(upipe_mpgaf, uref, NULL);
    assert(nb_packets == 1);

    upipe_release(upipe_mpgaf);

    /* Try again with ADTS AAC */
    uref = uref_block_flow_alloc_def(uref_mgr, "aac.sound.");
    assert(uref != NULL);

    upipe_mpgaf = upipe_void_alloc(upipe_mpgaf_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "mpgaf 2"));
    assert(upipe_mpgaf != NULL);
    ubase_assert(upipe_set_flow_def(upipe_mpgaf, uref));
    ubase_assert(upipe_set_output(upipe_mpgaf, upipe_sink));
    uref_free(uref);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 42 + 768 + ADTS_HEADER_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == 42 + 768 + ADTS_HEADER_SIZE);
    memset(buffer, 0, 42 + 768 + ADTS_HEADER_SIZE);

    buffer += 42;
    write_adts(buffer);
    write_data(buffer + ADTS_HEADER_SIZE, 768 - ADTS_HEADER_SIZE);

    buffer += 768;
    write_adts(buffer);

    uref_block_unmap(uref, 0);
    uref_clock_set_pts_orig(uref, 27000000);
    uref_clock_set_dts_orig(uref, 27000000);
    uref_clock_set_cr_sys(uref, 84);
    uref_clock_set_rap_sys(uref, 42);
    upipe_input(upipe_mpgaf, uref, NULL);
    assert(nb_packets == 2);

    upipe_release(upipe_mpgaf);

    /* Try again with raw to ADTS conversion */
    uint8_t headers[2];
    struct ubits bw;
    ubits_init(&bw, headers, 2, UBITS_WRITE);
    ubits_put(&bw, 5, ASC_TYPE_LC);
    ubits_put(&bw, 4, 0x3); /* 48 kHz */
    ubits_put(&bw, 4, 2); /* stereo */
    ubits_put(&bw, 1, 0); /* frame length - 1024 samples */
    ubits_put(&bw, 1, 0); /* !core coder */
    ubits_put(&bw, 1, 0); /* !extension */
    uint8_t *headers_end;
    int err = ubits_clean(&bw, &headers_end);
    ubase_assert(err);

    uref = uref_block_flow_alloc_def(uref_mgr, "aac.sound.");
    assert(uref != NULL);
    ubase_assert(uref_mpga_flow_set_encaps(uref, UREF_MPGA_ENCAPS_RAW));
    ubase_assert(uref_flow_set_headers(uref, headers, 2));

    upipe_mpgaf = upipe_void_alloc(upipe_mpgaf_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "mpgaf 3"));
    assert(upipe_mpgaf != NULL);
    ubase_assert(upipe_set_flow_def(upipe_mpgaf, uref));
    ubase_assert(upipe_set_output(upipe_mpgaf, upipe_sink));
    uref_free(uref);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 768 - ADTS_HEADER_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == 768 - ADTS_HEADER_SIZE);
    write_data(buffer, 768 - ADTS_HEADER_SIZE);

    uref_block_unmap(uref, 0);
    uref_clock_set_pts_orig(uref, 27000000);
    uref_clock_set_dts_orig(uref, 27000000);
    uref_clock_set_cr_sys(uref, 84);
    uref_clock_set_rap_sys(uref, 42);
    upipe_input(upipe_mpgaf, uref, NULL);
    assert(nb_packets == 3);

    upipe_release(upipe_mpgaf);

    /* Try again with ADTS to raw conversion */
    need_global = true;
    need_encaps = UREF_MPGA_ENCAPS_RAW;
    uref = uref_block_flow_alloc_def(uref_mgr, "aac.sound.");
    assert(uref != NULL);

    upipe_mpgaf = upipe_void_alloc(upipe_mpgaf_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "mpgaf 4"));
    assert(upipe_mpgaf != NULL);
    ubase_assert(upipe_set_flow_def(upipe_mpgaf, uref));
    ubase_assert(upipe_set_output(upipe_mpgaf, upipe_sink));
    uref_free(uref);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 42 + 768 + ADTS_HEADER_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == 42 + 768 + ADTS_HEADER_SIZE);
    memset(buffer, 0, 42 + 768 + ADTS_HEADER_SIZE);

    buffer += 42;
    write_adts(buffer);
    write_data(buffer + ADTS_HEADER_SIZE, 768 - ADTS_HEADER_SIZE);

    buffer += 768;
    write_adts(buffer);

    uref_block_unmap(uref, 0);
    uref_clock_set_pts_orig(uref, 27000000);
    uref_clock_set_dts_orig(uref, 27000000);
    uref_clock_set_cr_sys(uref, 84);
    uref_clock_set_rap_sys(uref, 42);
    upipe_input(upipe_mpgaf, uref, NULL);
    assert(nb_packets == 4);

    upipe_release(upipe_mpgaf);

    /* Try again with raw pass-through */
    need_global = true;
    need_encaps = UREF_MPGA_ENCAPS_RAW;
    uref = uref_block_flow_alloc_def(uref_mgr, "aac.sound.");
    assert(uref != NULL);
    ubase_assert(uref_mpga_flow_set_encaps(uref, UREF_MPGA_ENCAPS_RAW));
    ubase_assert(uref_flow_set_headers(uref, headers, 2));

    upipe_mpgaf = upipe_void_alloc(upipe_mpgaf_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "mpgaf 5"));
    assert(upipe_mpgaf != NULL);
    ubase_assert(upipe_set_flow_def(upipe_mpgaf, uref));
    ubase_assert(upipe_set_output(upipe_mpgaf, upipe_sink));
    uref_free(uref);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 768 - ADTS_HEADER_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == 768 - ADTS_HEADER_SIZE);
    write_data(buffer, 768 - ADTS_HEADER_SIZE);

    uref_block_unmap(uref, 0);
    uref_clock_set_pts_orig(uref, 27000000);
    uref_clock_set_dts_orig(uref, 27000000);
    uref_clock_set_cr_sys(uref, 84);
    uref_clock_set_rap_sys(uref, 42);
    upipe_input(upipe_mpgaf, uref, NULL);
    assert(nb_packets == 5);

    upipe_release(upipe_mpgaf);

    /* Try again with raw to LOAS conversion */
    need_global = false;
    need_encaps = UREF_MPGA_ENCAPS_LOAS;
    uref = uref_block_flow_alloc_def(uref_mgr, "aac.sound.");
    assert(uref != NULL);
    ubase_assert(uref_mpga_flow_set_encaps(uref, UREF_MPGA_ENCAPS_RAW));
    ubase_assert(uref_flow_set_headers(uref, headers, 2));

    upipe_mpgaf = upipe_void_alloc(upipe_mpgaf_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "mpgaf 6"));
    assert(upipe_mpgaf != NULL);
    ubase_assert(upipe_set_flow_def(upipe_mpgaf, uref));
    ubase_assert(upipe_set_output(upipe_mpgaf, upipe_sink));
    uref_free(uref);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 768 - ADTS_HEADER_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == 768 - ADTS_HEADER_SIZE);
    write_data(buffer, 768 - ADTS_HEADER_SIZE);

    uref_block_unmap(uref, 0);
    uref_clock_set_pts_orig(uref, 27000000);
    uref_clock_set_dts_orig(uref, 27000000);
    uref_clock_set_cr_sys(uref, 84);
    uref_clock_set_rap_sys(uref, 42);
    upipe_input(upipe_mpgaf, uref, NULL);
    assert(nb_packets == 6);

    upipe_release(upipe_mpgaf);

    /* Try again with LOAS to LOAS pass-through */
    need_global = false;
    need_encaps = UREF_MPGA_ENCAPS_LOAS;
    uref = uref_block_flow_alloc_def(uref_mgr, "aac_latm.sound.");
    assert(uref != NULL);
    ubase_assert(uref_mpga_flow_set_encaps(uref, UREF_MPGA_ENCAPS_LOAS));
    ubase_assert(uref_flow_set_complete(uref));

    upipe_mpgaf = upipe_void_alloc(upipe_mpgaf_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "mpgaf 7"));
    assert(upipe_mpgaf != NULL);
    ubase_assert(upipe_set_flow_def(upipe_mpgaf, uref));
    ubase_assert(upipe_set_output(upipe_mpgaf, upipe_sink));
    uref_free(uref);

    uref = uref_dup(last_output);
    assert(uref != NULL);

    uref_clock_set_pts_orig(uref, 27000000);
    uref_clock_set_dts_orig(uref, 27000000);
    uref_clock_set_cr_sys(uref, 84);
    uref_clock_set_rap_sys(uref, 42);
    upipe_input(upipe_mpgaf, uref, NULL);
    assert(nb_packets == 7);

    upipe_release(upipe_mpgaf);

    /* Try again with LOAS to raw conversion */
    need_global = false;
    need_encaps = UREF_MPGA_ENCAPS_RAW;
    uref = uref_block_flow_alloc_def(uref_mgr, "aac_latm.sound.");
    assert(uref != NULL);
    ubase_assert(uref_mpga_flow_set_encaps(uref, UREF_MPGA_ENCAPS_LOAS));
    ubase_assert(uref_flow_set_complete(uref));

    upipe_mpgaf = upipe_void_alloc(upipe_mpgaf_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "mpgaf 8"));
    assert(upipe_mpgaf != NULL);
    ubase_assert(upipe_set_flow_def(upipe_mpgaf, uref));
    ubase_assert(upipe_set_output(upipe_mpgaf, upipe_sink));
    uref_free(uref);

    uref = uref_dup(last_output);
    assert(uref != NULL);

    uref_clock_set_pts_orig(uref, 27000000);
    uref_clock_set_dts_orig(uref, 27000000);
    uref_clock_set_cr_sys(uref, 84);
    uref_clock_set_rap_sys(uref, 42);
    upipe_input(upipe_mpgaf, uref, NULL);
    assert(nb_packets == 8);

    upipe_release(upipe_mpgaf);

    /* Try again with raw to LATM conversion */
    need_global = false;
    need_encaps = UREF_MPGA_ENCAPS_LATM;
    uref = uref_block_flow_alloc_def(uref_mgr, "aac.sound.");
    assert(uref != NULL);
    ubase_assert(uref_mpga_flow_set_encaps(uref, UREF_MPGA_ENCAPS_RAW));
    ubase_assert(uref_flow_set_headers(uref, headers, 2));

    upipe_mpgaf = upipe_void_alloc(upipe_mpgaf_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "mpgaf 9"));
    assert(upipe_mpgaf != NULL);
    ubase_assert(upipe_set_flow_def(upipe_mpgaf, uref));
    ubase_assert(upipe_set_output(upipe_mpgaf, upipe_sink));
    uref_free(uref);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 768 - ADTS_HEADER_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == 768 - ADTS_HEADER_SIZE);
    write_data(buffer, 768 - ADTS_HEADER_SIZE);

    uref_block_unmap(uref, 0);
    uref_clock_set_pts_orig(uref, 27000000);
    uref_clock_set_dts_orig(uref, 27000000);
    uref_clock_set_cr_sys(uref, 84);
    uref_clock_set_rap_sys(uref, 42);
    upipe_input(upipe_mpgaf, uref, NULL);
    assert(nb_packets == 9);

    upipe_release(upipe_mpgaf);

    /* Try again with LATM to LATM pass-through */
    need_global = false;
    need_encaps = UREF_MPGA_ENCAPS_LATM;
    uref = uref_block_flow_alloc_def(uref_mgr, "aac_latm.sound.");
    assert(uref != NULL);
    ubase_assert(uref_mpga_flow_set_encaps(uref, UREF_MPGA_ENCAPS_LATM));
    ubase_assert(uref_flow_set_complete(uref));

    upipe_mpgaf = upipe_void_alloc(upipe_mpgaf_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "mpgaf 10"));
    assert(upipe_mpgaf != NULL);
    ubase_assert(upipe_set_flow_def(upipe_mpgaf, uref));
    ubase_assert(upipe_set_output(upipe_mpgaf, upipe_sink));
    uref_free(uref);

    uref = uref_dup(last_output);
    assert(uref != NULL);

    uref_clock_set_pts_orig(uref, 27000000);
    uref_clock_set_dts_orig(uref, 27000000);
    uref_clock_set_cr_sys(uref, 84);
    uref_clock_set_rap_sys(uref, 42);
    upipe_input(upipe_mpgaf, uref, NULL);
    assert(nb_packets == 10);

    upipe_release(upipe_mpgaf);

    /* Try again with LATM to raw conversion */
    need_global = false;
    need_encaps = UREF_MPGA_ENCAPS_RAW;
    uref = uref_block_flow_alloc_def(uref_mgr, "aac_latm.sound.");
    assert(uref != NULL);
    ubase_assert(uref_mpga_flow_set_encaps(uref, UREF_MPGA_ENCAPS_LATM));
    ubase_assert(uref_flow_set_complete(uref));

    upipe_mpgaf = upipe_void_alloc(upipe_mpgaf_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "mpgaf 11"));
    assert(upipe_mpgaf != NULL);
    ubase_assert(upipe_set_flow_def(upipe_mpgaf, uref));
    ubase_assert(upipe_set_output(upipe_mpgaf, upipe_sink));
    uref_free(uref);

    uref = uref_dup(last_output);
    assert(uref != NULL);

    uref_clock_set_pts_orig(uref, 27000000);
    uref_clock_set_dts_orig(uref, 27000000);
    uref_clock_set_cr_sys(uref, 84);
    uref_clock_set_rap_sys(uref, 42);
    upipe_input(upipe_mpgaf, uref, NULL);
    assert(nb_packets == 11);

    upipe_release(upipe_mpgaf);

    uref_free(last_output);
    test_free(upipe_sink);
    upipe_mgr_release(upipe_mpgaf_mgr);

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(uprobe_stdio);
    uprobe_clean(&uprobe);

    return 0;
}
