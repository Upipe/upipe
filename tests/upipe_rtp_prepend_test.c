/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
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
#include <upipe/uprobe_log.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_std.h>
#include <upipe/uref_dump.h>
#include <upipe/uref_clock.h>
#include <upipe-modules/upipe_rtp_prepend.h>

#include <upipe/upipe_helper_upipe.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <bitstream/ietf/rtp.h>

#define DEFAULT_FREQ 90000 /* (90kHz, see rfc 2250 and 3551) */

#define UDICT_POOL_DEPTH    5
#define UREF_POOL_DEPTH     5
#define UBUF_POOL_DEPTH     5
#define UBUF_PREPEND        0
#define UBUF_APPEND         0
#define UBUF_ALIGN          16
#define UBUF_ALIGN_OFFSET   0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

#define PACKET_NUM 42

/** definition of our uprobe */
static bool catch(struct uprobe *uprobe, struct upipe *upipe, enum uprobe_event event, va_list args)
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
    return true;
}

/** phony pipe to test upipe_rtp_prepend */
struct rtp_prepend_test {
    struct uref *entry;
    uint16_t seqnum;
    struct upipe upipe;
};

/** helper phony pipe to test upipe_rtp_prepend */
UPIPE_HELPER_UPIPE(rtp_prepend_test, upipe, 0);

/** helper phony pipe to test upipe_rtp_prepend */
static struct upipe *rtp_prepend_test_alloc(struct upipe_mgr *mgr,
                                            struct uprobe *uprobe,
                                            uint32_t signature, va_list args)
{
    struct rtp_prepend_test *rtp_prepend_test = malloc(sizeof(struct rtp_prepend_test));
    assert(rtp_prepend_test != NULL);
    upipe_init(&rtp_prepend_test->upipe, mgr, uprobe);
    rtp_prepend_test->entry = NULL;
    rtp_prepend_test->seqnum = 0;
    return &rtp_prepend_test->upipe;
}

/** helper phony pipe to test upipe_rtp_prepend */
static void rtp_prepend_test_input(struct upipe *upipe, struct uref *uref,
                              struct upump *upump)
{
    struct rtp_prepend_test *rtp_prepend_test = rtp_prepend_test_from_upipe(upipe);
    uint16_t seqnum;
    uint32_t result, expected;
    uint64_t dts = 0;
    int size;
    const uint8_t *buf;
    lldiv_t div;
    unsigned int freq = DEFAULT_FREQ;

    assert(uref != NULL);
    upipe_dbg(upipe, "===> received input uref");
    uref_dump(uref, upipe->uprobe);

    /* compute expected timestamp */
    if (unlikely(!uref_clock_get_dts(uref, &dts))) {
        uref_clock_get_systime(uref, &dts);
    }
    div = lldiv(dts, UCLOCK_FREQ);
    expected = div.quot * freq + ((uint64_t)div.rem * freq)/UCLOCK_FREQ;

    /* map  header */
    size = RTP_HEADER_SIZE;
    uref_block_read(uref, 0, &size, &buf);
    assert(size == RTP_HEADER_SIZE);

    /* seqnum */
    seqnum = rtp_get_seqnum(buf);
    if (unlikely(!rtp_prepend_test->seqnum)) {
        rtp_prepend_test->seqnum = seqnum;
    }
    upipe_dbg_va(upipe, "seqnum expected: %"PRIu16" \t result: %"PRIu16,
                 rtp_prepend_test->seqnum, seqnum);
    assert(rtp_prepend_test->seqnum == seqnum);

    /* timestamp */
    result = rtp_get_timestamp(buf);
    upipe_dbg_va(upipe, "timestamp expected: %"PRIu64" \t result: %"PRIu64,
                 expected, result);
    assert(expected == result);

    /* unmap */
    uref_block_unmap(uref, 0);

    /* keep uref */
    if (rtp_prepend_test->entry) {
        uref_free(rtp_prepend_test->entry);
    }
    rtp_prepend_test->entry = uref;
    rtp_prepend_test->seqnum++;
}

/** helper phony pipe to test upipe_rtp_prepend */
static void rtp_prepend_test_free(struct upipe *upipe)
{
    upipe_dbg_va(upipe, "releasing pipe %p", upipe);
    struct rtp_prepend_test *rtp_prepend_test = rtp_prepend_test_from_upipe(upipe);
    if (rtp_prepend_test->entry)
        uref_free(rtp_prepend_test->entry);
    upipe_clean(upipe);
    free(rtp_prepend_test);
}

/** helper phony pipe to test upipe_dup */
static struct upipe_mgr rtp_prepend_test_mgr = {
    .signature = 0,
    .upipe_alloc = rtp_prepend_test_alloc,
    .upipe_input = rtp_prepend_test_input,
    .upipe_control = NULL,
    .upipe_free = NULL,

    .upipe_mgr_free = NULL
};

int main(int argc, char **argv)
{
    printf("Compiled %s %s - %s\n", __DATE__, __TIME__, __FILE__);

    struct ubuf_mgr *ubuf_mgr;
    struct uref *uref;
    uint64_t opaque = 0x00cafebabe;
    int i;

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
                                                    UBUF_PREPEND, UBUF_APPEND,
                                                    UBUF_ALIGN,
                                                    UBUF_ALIGN_OFFSET);
    assert(ubuf_mgr);

    /* uprobe stuff */
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_DEBUG);
    assert(uprobe_stdio != NULL);
    struct uprobe *log = uprobe_log_alloc(uprobe_stdio, UPROBE_LOG_DEBUG);
    assert(log != NULL);

    /* Send first flow definition packet */
    uref = uref_block_flow_alloc_def(uref_mgr, "bar.");
    assert(uref);

    /* build rtp_prepend pipe */
    struct upipe_mgr *upipe_rtp_prepend_mgr = upipe_rtp_prepend_mgr_alloc();
    struct upipe *rtp_prepend = upipe_flow_alloc(upipe_rtp_prepend_mgr,
            uprobe_pfx_adhoc_alloc(log, UPROBE_LOG_LEVEL, "rtp"),
            uref);
    assert(upipe_rtp_prepend_mgr);
    assert(rtp_prepend);
    assert(upipe_set_ubuf_mgr(rtp_prepend, ubuf_mgr));

    struct upipe *rtp_prepend_test = upipe_flow_alloc(&rtp_prepend_test_mgr,
                                                      log, uref);
    assert(rtp_prepend_test != NULL);
    assert(upipe_set_output(rtp_prepend, rtp_prepend_test));
    uref_free(uref);

    /* Now send uref */
    for (i=0; i < PACKET_NUM; i++) {
        opaque += i * UCLOCK_FREQ + rand();
        uref = uref_block_alloc(uref_mgr, ubuf_mgr, 42);
        assert(uref);
        assert(uref_clock_set_systime(uref, opaque));
        upipe_input(rtp_prepend, uref, NULL);
        assert(rtp_prepend_test_from_upipe(rtp_prepend_test)->entry);
    }

    upipe_release(rtp_prepend);
    rtp_prepend_test_free(rtp_prepend_test);

    /* release managers */
    ubuf_mgr_release(ubuf_mgr);
    uref_mgr_release(uref_mgr);
    umem_mgr_release(umem_mgr);
    udict_mgr_release(udict_mgr);
    uprobe_log_free(log);
    uprobe_stdio_free(uprobe_stdio);

    return 0;
}
