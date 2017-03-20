/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *          Benjamin Cohen
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
 * @short unit tests for rtp source pipe
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_upump_mgr.h>
#include <upipe/uprobe_uclock.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/uclock.h>
#include <upipe/uclock_std.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_std.h>
#include <upipe/uref_dump.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe/upipe.h>
#include <upipe-modules/upipe_rtp_demux.h>
#include <upipe-modules/upipe_udp_source.h>
#include <upipe-modules/upipe_setflowdef.h>
#include <upipe-framers/upipe_mpga_framer.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <bitstream/ietf/rtp.h>
#include <bitstream/ietf/rtp2250.h>
#include <bitstream/mpeg/mpga.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UBUF_POOL_DEPTH 0
#define UPUMP_POOL 0
#define UPUMP_BLOCKER_POOL 0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG
#define MPGA_SIZE 768
#define BUF_SIZE (RTP_HEADER_SIZE + RTP2250A_HEADER_SIZE + MPGA_SIZE)

static int sockfd;
static struct ubuf_mgr *ubuf_mgr;
static struct uref_mgr *uref_mgr;
static struct upipe *source;
static struct upump *write_pump;
static struct addrinfo hints, *servinfo, *p;
static int counter_in = 0;
static int counter_out = 0;
static uint8_t w[BUF_SIZE];
static uint64_t dts = UINT32_MAX;

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
        case UPROBE_SOURCE_END:
        case UPROBE_SYNC_ACQUIRED:
        case UPROBE_UDPSRC_NEW_PEER:
            break;
        case UPROBE_CLOCK_REF:
        case UPROBE_CLOCK_TS:
            counter_in++;
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
    upipe_throw_ready(upipe);
    return upipe;
}

/** helper phony pipe */
static void test_input(struct upipe *upipe, struct uref *uref,
                       struct upump **upump_p)
{
    uint8_t buf[MPGA_HEADER_SIZE];
    const uint8_t *rbuf;
    assert(uref != NULL);
    uref_dump(uref, upipe->uprobe);

    if ((rbuf = uref_block_peek(uref, 0, MPGA_HEADER_SIZE, buf))) {
        assert(rbuf[0] == 0xff);
        uref_block_peek_unmap(uref, 0, buf, rbuf);
    } else
        assert(0);

    uint64_t dts_prog, duration;
    ubase_assert(uref_clock_get_dts_prog(uref, &dts_prog));
    ubase_assert(uref_clock_get_duration(uref, &duration));
    assert(dts == dts_prog);
    assert(duration == UCLOCK_FREQ * 1152 / 48000);
    dts += duration;

    counter_in++;
    if (counter_in == 330) {
        upipe_set_uri(source, NULL);
    }

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
    upipe_dbg_va(upipe, "releasing pipe %p", upipe);
    upipe_throw_dead(upipe);
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe */
static struct upipe_mgr test_mgr = {
    .refcount = NULL,
    .signature = 0,
    .upipe_alloc = test_alloc,
    .upipe_input = test_input,
    .upipe_control = test_control
};

/* packet generator */
static void genpackets(struct upump *unused)
{
    int i;
    printf("Counter: %d\n", counter_out);
    if (counter_out > 100) {
        upump_stop(write_pump);
        return;
    }
    for (i=0; i < 10; i++) {
        counter_out++;
        sendto(sockfd, w, BUF_SIZE, 0, p->ai_addr, p->ai_addrlen);
        rtp_set_seqnum(w, rtp_get_seqnum(w) + 1);
        rtp_set_timestamp(w, rtp_get_timestamp(w) + 90000 * 1152 / 48000);
    }
}

int main(int argc, char *argv[])
{
    char udp_uri[512], port_str[8];
    int i, port;
    bool ret;

    /* env */
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    assert(uref_mgr != NULL);
    ubuf_mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH,
                                                         umem_mgr, 0, 0, -1, 0);
    assert(ubuf_mgr != NULL);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc_default(UPUMP_POOL,
            UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);
    struct uclock *uclock = uclock_std_alloc(0);
    assert(uclock != NULL);
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_LEVEL);
    assert(logger != NULL);
    logger = uprobe_uref_mgr_alloc(logger, uref_mgr);
    assert(logger != NULL);
    logger = uprobe_upump_mgr_alloc(logger, upump_mgr);
    assert(logger != NULL);
    logger = uprobe_uclock_alloc(logger, uclock);
    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_POOL_DEPTH);
    assert(logger != NULL);

    struct upipe_mgr *rtp_demux_mgr = upipe_rtp_demux_mgr_alloc();
    assert(rtp_demux_mgr != NULL);
    struct upipe_mgr *mpgaf_mgr = upipe_mpgaf_mgr_alloc();
    assert(mpgaf_mgr != NULL);
    upipe_rtp_demux_mgr_set_mpgaf_mgr(rtp_demux_mgr, mpgaf_mgr);
    upipe_mgr_release(mpgaf_mgr);
    struct upipe *demux = upipe_void_alloc(rtp_demux_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "rtp demux"));
    assert(demux != NULL);
    upipe_mgr_release(rtp_demux_mgr);

    struct upipe_mgr *udpsrc_mgr = upipe_udpsrc_mgr_alloc();
    assert(udpsrc_mgr != NULL);
    source = upipe_void_alloc(udpsrc_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "udpsrc"));

    struct uref *flow_def = uref_block_flow_alloc_def(uref_mgr,
                                                      "rtp.mp3.sound.");
    struct upipe_mgr *setflowdef_mgr = upipe_setflowdef_mgr_alloc();
    assert(setflowdef_mgr != NULL);
    struct upipe *upipe = upipe_void_alloc_output(source, setflowdef_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "setflowdef"));
    assert(upipe != NULL);
    upipe_mgr_release(setflowdef_mgr);
    upipe_setflowdef_set_dict(upipe, flow_def);
    uref_free(flow_def);

    upipe = upipe_void_chain_output_sub(upipe, demux,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "rtp demux sub"));
    assert(upipe != NULL);
    upipe_release(demux);

    memset(w, 0, sizeof(w));
    rtp_set_hdr(w);
    rtp_set_type(w, 96);
    uint8_t *payload = rtp_payload(w);
    rtp2250a_set_hdr(payload);
    payload += RTP2250A_HEADER_SIZE;
    mpga_set_sync(payload);
    mpga_set_layer(payload, MPGA_LAYER_3);
    mpga_set_bitrate_index(payload, 13);
    mpga_set_sampling_freq(payload, 1);
    mpga_set_mode(payload, MPGA_MODE_STEREO);

    srand(42);
    for (i=0; i < 10; i++) {
        port = ((rand() % 40000) + 1024);
        snprintf(udp_uri, sizeof(udp_uri), "@127.0.0.1:%d", port);
        printf("Trying uri: %s ...\n", udp_uri);
        if (( ret = ubase_check(upipe_set_uri(source, udp_uri)) )) {
            break;
        }
    }
    assert(ret);

    struct upipe *test_pipe = upipe_void_chain_output(upipe, &test_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "test"));
    assert(test_pipe != NULL);

    /* open client socket */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    snprintf(port_str, sizeof(port_str), "%d", port);
    assert(getaddrinfo("127.0.0.1", port_str, &hints, &servinfo) == 0);
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if (( sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            continue;
        }
        break;
    }
    assert(p);

    write_pump = upump_alloc_idler(upump_mgr, genpackets, NULL, NULL);
    assert(write_pump);
    upump_start(write_pump);

    /* fire */
    upump_mgr_run(upump_mgr, NULL);

    assert(counter_in == 330);
    close(sockfd);

    /* release */
    upump_free(write_pump);
    upipe_release(source);
    test_free(test_pipe);
    upump_mgr_release(upump_mgr);
    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uclock_release(uclock);
    uprobe_release(logger);
    uprobe_clean(&uprobe);

    freeaddrinfo(servinfo);

    return 0;
}
