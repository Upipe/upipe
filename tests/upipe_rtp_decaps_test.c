/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
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

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe-modules/upipe_rtp_decaps.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#include <bitstream/ietf/rtp.h>

#define UDICT_POOL_DEPTH    0
#define UREF_POOL_DEPTH     0
#define UBUF_POOL_DEPTH     0
#define UBUF_PREPEND        0
#define UBUF_APPEND         0
#define UBUF_ALIGN          32
#define UBUF_ALIGN_OFFSET   0
#define SIZE                1328

#define UPROBE_LOG_LEVEL UPROBE_LOG_VERBOSE

static unsigned int nb_packets = 0;
static bool expect_discontinuity = false;

/** helper phony pipe */
static struct upipe *test_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe,
                                uint32_t signature, va_list args)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_init(upipe, mgr, uprobe);
    upipe_throw_ready(upipe);
    return upipe;
}

/** helper phony pipe */
static void test_input(struct upipe *upipe, struct uref *uref,
                       struct upump **upump_p)
{
    const uint8_t *buf;
    int size;

    size = -1;
    ubase_assert(uref_block_read(uref, 0, &size, &buf));
    assert(size == SIZE - RTP_HEADER_SIZE);
    uref_block_unmap(uref, 0);
    assert(ubase_check(uref_flow_get_discontinuity(uref)) ==
           expect_discontinuity);
    nb_packets--;
    uref_free(uref);
}

/** helper phony pipe */
static int test_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            const char *def;
            ubase_assert(uref_flow_get_def(flow_def, &def));
            assert(!strcmp(def, "block.mpegtsaligned."));
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
    upipe_throw_dead(upipe);
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe */
static struct upipe_mgr rtpd_test_mgr = {
    .refcount = NULL,
    .signature = 0,
    .upipe_alloc = test_alloc,
    .upipe_input = test_input,
    .upipe_control = test_control
};

/** definition of our uprobe */
static int catch(struct uprobe *uprobe, struct upipe *upipe, int event, va_list args)
{
    switch (event) {
        case UPROBE_READY:
        case UPROBE_DEAD:
        case UPROBE_NEW_FLOW_DEF:
        case UPROBE_CLOCK_REF:
            break;
        default:
            assert(0);
            break;
    }
    return UBASE_ERR_NONE;
}

int main(int argc, char **argv)
{
    printf("Compiled %s %s - %s\n", __DATE__, __TIME__, __FILE__);
    struct uref *uref = NULL;
    uint8_t *buf;
    int size;

    /* uref and mem management */
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH, umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0); 
    assert(uref_mgr != NULL);

    /* block */
    struct ubuf_mgr *block_mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH,
            UBUF_POOL_DEPTH, umem_mgr,
            UBUF_PREPEND,
            UBUF_APPEND,
            UBUF_ALIGN,
            UBUF_ALIGN_OFFSET);
    assert(block_mgr);

    /* uprobe stuff */
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_DEBUG);
    assert(uprobe_stdio != NULL);

    uref = uref_block_flow_alloc_def(uref_mgr, "rtp.");
    assert(uref);

    /* build rtpd pipe */
    struct upipe_mgr *upipe_rtpd_mgr = upipe_rtpd_mgr_alloc();
    assert(upipe_rtpd_mgr);
    struct upipe *rtpd = upipe_void_alloc(upipe_rtpd_mgr,
                uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                                 "rtpd"));
    assert(rtpd);
    ubase_assert(upipe_set_flow_def(rtpd, uref));
    uref_free(uref);

    /* rtpd_test */
    struct upipe *rtpd_test = upipe_void_alloc(&rtpd_test_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "rtpdtest"));
    assert(rtpd_test != NULL);
    ubase_assert(upipe_set_output(rtpd, rtpd_test));
    upipe_release(rtpd_test);

    /* Now send uref */
    uref = uref_block_alloc(uref_mgr, block_mgr, SIZE);
    size = -1;
    uref_block_write(uref, 0, &size, &buf);
    rtp_set_hdr(buf);
    rtp_set_type(buf, RTP_TYPE_TS);
    rtp_set_seqnum(buf, 1);
    rtp_set_timestamp(buf, 0);
    uref_block_unmap(uref, 0);
    nb_packets = 1;
    upipe_input(rtpd, uref, NULL);
    assert(!nb_packets);

    uref = uref_block_alloc(uref_mgr, block_mgr, SIZE);
    size = -1;
    uref_block_write(uref, 0, &size, &buf);
    rtp_set_hdr(buf);
    rtp_set_type(buf, RTP_TYPE_TS);
    rtp_set_seqnum(buf, 42);
    rtp_set_timestamp(buf, 0);
    uref_block_unmap(uref, 0);
    expect_discontinuity = true;
    nb_packets = 1;
    upipe_input(rtpd, uref, NULL);
    assert(!nb_packets);

    uint64_t lost;
    ubase_assert(upipe_rtpd_get_packets_lost(rtpd, &lost));
    assert(lost == 42 - 1 - 1);

    /* release pipe */
    upipe_release(rtpd);
    test_free(rtpd_test);

    /* release managers */
    upipe_mgr_release(upipe_rtpd_mgr); // no-op
    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(block_mgr);
    umem_mgr_release(umem_mgr);
    udict_mgr_release(udict_mgr);
    uprobe_release(uprobe_stdio);
    uprobe_clean(&uprobe);

    return 0;
}
