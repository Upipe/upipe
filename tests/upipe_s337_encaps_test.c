/*
 * Copyright (C) 2012-2014 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
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
#include <upipe/ubuf_block_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_std.h>
#include <upipe/uref_dump.h>
#include <upipe/uref_clock.h>
#include <upipe-modules/upipe_s337_encaps.h>

#include <upipe/upipe_helper_upipe.h>

#include <bitstream/smpte/337.h>
#include <bitstream/atsc/a52.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#define UDICT_POOL_DEPTH    0
#define UREF_POOL_DEPTH     0
#define UBUF_POOL_DEPTH     0
#define UBUF_ALIGN          0
#define UBUF_ALIGN_OFFSET   0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

#define PACKETS 42
#define PACKET_SIZE 66

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
    }
    return UBASE_ERR_NONE;
}

/** helper phony pipe */
struct s337_encaps_test {
    struct uref *entry;
    int packets;
    struct upipe upipe;
};

/** helper phony pipe */
UPIPE_HELPER_UPIPE(s337_encaps_test, upipe, 0);

/** helper phony pipe */
static struct upipe *test_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe,
                                uint32_t signature, va_list args)
{
    struct s337_encaps_test *s337_encaps_test = malloc(sizeof(struct s337_encaps_test));
    assert(s337_encaps_test != NULL);
    upipe_init(&s337_encaps_test->upipe, mgr, uprobe);
    s337_encaps_test->entry = NULL;
    s337_encaps_test->packets = 0;
    return &s337_encaps_test->upipe;
}

/** helper phony pipe */
static void test_input(struct upipe *upipe, struct uref *uref,
                       struct upump **upump_p)
{
    struct s337_encaps_test *s337_encaps_test = s337_encaps_test_from_upipe(upipe);

    assert(uref != NULL);
    upipe_dbg(upipe, "===> received input uref");
    uref_dump(uref, upipe->uprobe);

    size_t samples;
    uint8_t sample_size;
    ubase_assert(uref_sound_size(uref, &samples, &sample_size));
    assert(samples == A52_FRAME_SAMPLES);
    assert(sample_size == 2 * 4);

    /* map header */
    int size = 2;
    const int32_t *buf;

    uref_sound_read_int32_t(uref, 0, size, &buf, 1);
    assert(size == 2);

    /* 16-bit big endian */
    uint8_t s337[S337_PREAMBLE_SIZE];
    for (int i = 0; i < 4; i++) {
        uint16_t word = (buf[i] >> 16) & 0xffff;
        s337[2*i + 0] = word >> 8;
        s337[2*i + 1] = word & 0xff;
    }

    assert(s337[0] == S337_PREAMBLE_A1);
    assert(s337[1] == S337_PREAMBLE_A2);
    assert(s337[2] == S337_PREAMBLE_B1);
    assert(s337[3] == S337_PREAMBLE_B2);
    assert(s337_get_data_type(s337) == S337_TYPE_A52);
    assert(s337_get_data_mode(s337) == S337_MODE_16);
    assert(s337_get_error(s337) == false);
    assert(s337_get_length(s337) == PACKET_SIZE * 8);

    /* unmap */
    uref_sound_unmap(uref, 0, -1, 1);

    /* keep uref */
    if (s337_encaps_test->entry) {
        uref_free(s337_encaps_test->entry);
    }
    s337_encaps_test->entry = uref;
    s337_encaps_test->packets++;
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
    upipe_dbg_va(upipe, "releasing pipe %p", upipe);
    struct s337_encaps_test *s337_encaps_test = s337_encaps_test_from_upipe(upipe);
    if (s337_encaps_test->entry)
        uref_free(s337_encaps_test->entry);
    upipe_clean(upipe);
    free(s337_encaps_test);
}

/** helper phony pipe */
static struct upipe_mgr s337_encaps_test_mgr = {
    .refcount = NULL,
    .signature = 0,
    .upipe_alloc = test_alloc,
    .upipe_input = test_input,
    .upipe_control = test_control
};

int main(int argc, char **argv)
{
    printf("Compiled %s %s - %s\n", __DATE__, __TIME__, __FILE__);

    struct ubuf_mgr *ubuf_mgr;
    struct uref *uref;

    /* uref and mem management */
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH, umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    assert(uref_mgr != NULL);

    /* block */
    ubuf_mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH,
                                                    UBUF_POOL_DEPTH, umem_mgr,
                                                    0,
                                                    0,
                                                    UBUF_ALIGN,
                                                    UBUF_ALIGN_OFFSET);
    assert(ubuf_mgr);

    /* uprobe stuff */
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_DEBUG);
    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_POOL_DEPTH);
    assert(logger != NULL);

    /* Send first flow definition packet */
    uref = uref_block_flow_alloc_def(uref_mgr, "ac3.sound.");
    ubase_assert(uref_sound_flow_set_rate(uref, 44100));
    assert(uref);

    /* build s337_encaps pipe */
    struct upipe_mgr *upipe_s337_encaps_mgr = upipe_s337_encaps_mgr_alloc();
    struct upipe *s337_encaps = upipe_void_alloc(upipe_s337_encaps_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "s337e"));
    assert(upipe_s337_encaps_mgr);
    ubase_assert(upipe_set_flow_def(s337_encaps, uref));
    uref_free(uref);
    assert(s337_encaps);

    struct upipe *s337_encaps_test = upipe_void_alloc(&s337_encaps_test_mgr,
                                                      uprobe_use(logger));
    assert(s337_encaps_test != NULL);
    ubase_assert(upipe_set_output(s337_encaps, s337_encaps_test));

    /* Now send uref */
    for (int i=0; i < PACKETS; i++) {
        uref = uref_block_alloc(uref_mgr, ubuf_mgr, PACKET_SIZE);
        assert(uref);
        upipe_input(s337_encaps, uref, NULL);
        assert(s337_encaps_test_from_upipe(s337_encaps_test)->entry);
    }

    assert(s337_encaps_test_from_upipe(s337_encaps_test)->packets == PACKETS);

    upipe_release(s337_encaps);
    test_free(s337_encaps_test);

    /* release managers */
    ubuf_mgr_release(ubuf_mgr);
    uref_mgr_release(uref_mgr);
    umem_mgr_release(umem_mgr);
    udict_mgr_release(udict_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe);

    return 0;
}
