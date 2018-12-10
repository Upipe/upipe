/*
 * Copyright (C) 2016-2017 Open Broadcast Systems Ltd.
 *
 * Authors: Rafaël Carré
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

#undef NDEBUG
#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_upump_mgr.h>
#include <upipe/uprobe_uclock.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/uprobe_dejitter.h>
#include <upipe/uclock.h>
#include <upipe/uclock_std.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_dump.h>
#include <upipe/uref_block.h>
#include <upipe/uref_std.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe/uuri.h>
#include <upipe/ustring.h>
#include <upipe/upipe.h>
#include <upipe-modules/upipe_genaux.h>
#include <upipe-modules/upipe_dup.h>
#include <upipe-modules/upipe_rtcp.h>
#include <upipe-modules/upipe_udp_source.h>
#include <upipe-modules/upipe_file_sink.h>
#include <upipe-modules/upipe_multicat_sink.h>
#include <upipe-modules/upipe_udp_sink.h>
#include <upipe-modules/upipe_probe_uref.h>
#include <upipe-filters/upipe_rtcp_fb_receiver.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>

#include <bitstream/ietf/rtcp_sr.h>
#include <bitstream/ietf/rtcp3611.h>
#include <bitstream/ietf/rtcp_rr.h>
#include <bitstream/ietf/rtp.h>

#define UDICT_POOL_DEPTH 10
#define UREF_POOL_DEPTH 10
#define UBUF_POOL_DEPTH 10
#define UPUMP_POOL 10
#define UPUMP_BLOCKER_POOL 10
#define READ_SIZE 4096

static void usage(const char *argv0) {
    fprintf(stdout, "Usage: %s [-d] <udp source> <udp dest> <latency>\n", argv0);
    fprintf(stdout, "   -d: more verbose\n");
    fprintf(stdout, "   -q: more quiet\n");
    exit(EXIT_FAILURE);
}

static struct upipe *upipe_udpsink;
static struct upipe *upipe_udpsink_rtcp;
static struct upipe *upipe_udpsrc_sub;

static uint64_t last_sr_ntp;
static uint64_t last_sr_cr;

static struct uref_mgr *uref_mgr;

/** definition of our uprobe */
static int catch_udp(struct uprobe *uprobe, struct upipe *upipe,
                 int event, va_list args)
{
    const char *uri;

    switch (event) {
    case UPROBE_SOURCE_END:
        upipe_warn(upipe, "Remote end not listening, can't receive RTCP");
        /* This control can not fail, and will trigger restart of upump */
        upipe_get_uri(upipe, &uri);
        return UBASE_ERR_NONE;
    case UPROBE_UDPSRC_NEW_PEER:
        return UBASE_ERR_NONE;
    default:
        return uprobe_throw_next(uprobe, upipe, event, args);
    }
}

static void parse_rtcp(struct upipe *upipe, const uint8_t *rtp, int s,
        uint64_t cr_sys, struct ubuf_mgr *ubuf_mgr)
{
    while (s > 0) {
        if (s < 4 || !rtp_check_hdr(rtp)) {
            upipe_warn_va(upipe, "Received invalid RTP packet");
            break;
        }

        size_t len = 4 + 4 * rtcp_get_length(rtp);
        if (len > s) {
           break;
        }

        switch (rtcp_get_pt(rtp)) {
        case RTCP_PT_SR:
            if (s < RTCP_SR_SIZE)
                break;
            uint32_t ntp_msw = rtcp_sr_get_ntp_time_msw(rtp);
            uint32_t ntp_lsw = rtcp_sr_get_ntp_time_lsw(rtp);
            if (cr_sys != UINT64_MAX)
                last_sr_cr = cr_sys;
            last_sr_ntp = ((uint64_t)ntp_msw << 32) | ntp_lsw;
            upipe_verbose_va(upipe, "RTCP SR, CR %"PRIu64" NTP %"PRIu64, last_sr_cr,
                    last_sr_ntp);
            break;
        case RTCP_PT_RR:
            if (s < RTCP_RR_SIZE)
                break;
            if (rtcp_get_rc(rtp) < 1)
                break;

            uint32_t delay = rtcp_rr_get_delay_since_last_sr(rtp);
            uint32_t last_sr = rtcp_rr_get_last_sr(rtp);
            if (last_sr != ((last_sr_ntp >> 16) & 0xffffffff))
                break;

            if (cr_sys != UINT64_MAX) {
                cr_sys -= last_sr_cr;
                cr_sys -= delay * UCLOCK_FREQ / 65536;
                upipe_verbose_va(upipe, "RTCP RR: RTT %f", (float) cr_sys / UCLOCK_FREQ);
            }
            break;
        case RTCP_PT_XR:
            if (s < RTCP_XR_HEADER_SIZE + RTCP_XR_RRTP_SIZE)
                break;

            uint8_t ssrc[4];
            rtcp_xr_get_ssrc_sender(rtp, ssrc);
            rtp += RTCP_XR_HEADER_SIZE;

            if (rtcp_xr_get_bt(rtp) != RTCP_XR_RRTP_BT)
                break;
            if ((rtcp_xr_get_length(rtp) + 1) * 4 != RTCP_XR_RRTP_SIZE)
                break;

            uint64_t ntp = rtcp_xr_rrtp_get_ntp(rtp);

            struct uref *xr = uref_alloc(uref_mgr);
            if (!xr)
                break;

            if (cr_sys != UINT64_MAX)
                uref_clock_set_cr_sys(xr, cr_sys);

            const size_t xr_len = RTCP_XR_HEADER_SIZE + RTCP_XR_DLRR_SIZE;
            struct ubuf *ubuf = ubuf_block_alloc(ubuf_mgr, xr_len);
            if (!ubuf) {
                uref_free(xr);
                break;
            }

            uref_attach_ubuf(xr, ubuf);

            uint8_t *buf_xr;
            s = 0;
            uref_block_write(xr, 0, &s, &buf_xr);

            rtcp_set_rtp_version(buf_xr);
            rtcp_set_pt(buf_xr, RTCP_PT_XR);
            rtcp_set_length(buf_xr, xr_len / 4 - 1);

            static const uint8_t pi_ssrc[4] = { 0, 0, 0, 0 };
            rtcp_xr_set_ssrc_sender(buf_xr, pi_ssrc);

            buf_xr += RTCP_XR_HEADER_SIZE;
            rtcp_xr_set_bt(buf_xr, RTCP_XR_DLRR_BT);
            rtcp_xr_dlrr_set_reserved(buf_xr);
            rtcp_xr_set_length(buf_xr, RTCP_XR_DLRR_SIZE / 4 - 1);
            rtcp_xr_dlrr_set_ssrc_receiver(buf_xr, ssrc);

            ntp >>= 16;
            rtcp_xr_dlrr_set_lrr(buf_xr, (uint32_t)ntp);

            rtcp_xr_dlrr_set_dlrr(buf_xr, 0); // delay = 0, we answer immediately

            uref_block_unmap(xr, 0);

            upipe_notice_va(upipe, "sending XR");
            upipe_input(upipe_udpsink_rtcp, xr, NULL);
            break;
        default:
            break;
        }

        s -= len;
        rtp += len;
    }
}

/** definition of our uprobe */
static int catch(struct uprobe *uprobe, struct upipe *upipe,
                 int event, va_list args)
{
    struct uref *uref = NULL;

    switch (event) {
    case UPROBE_SOURCE_END:
        upipe_release(upipe);
        break;

    case UPROBE_PROBE_UREF: {
        int sig = va_arg(args, int);
        if (sig != UPIPE_PROBE_UREF_SIGNATURE)
            return UBASE_ERR_INVALID;
        uref = va_arg(args, struct uref *);
        va_arg(args, struct upump **);
        va_arg(args, bool *);

        const uint8_t *buf;
        int s = -1;
        if (!ubase_check(uref_block_read(uref, 0, &s, &buf)))
            return UBASE_ERR_INVALID;

        uint64_t cr_sys;
        if (!ubase_check(uref_clock_get_cr_sys(uref, &cr_sys)))
            cr_sys = UINT64_MAX;

        parse_rtcp(upipe, buf, s, cr_sys, uref->ubuf->mgr);

        uref_block_unmap(uref, 0);

        break;
    }
    default:
        return uprobe_throw_next(uprobe, upipe, event, args);
    }
    return UBASE_ERR_NONE;
}

static void stop(struct upump *upump)
{
    struct upipe *udpsrc = upump_get_opaque(upump, struct upipe*);
    upump_stop(upump);
    upump_free(upump);

    upipe_release(upipe_udpsrc_sub);
    upipe_release(udpsrc);
}

int main(int argc, char *argv[])
{
    char *srcpath, *dirpath, *latency;
    int opt;
    enum uprobe_log_level loglevel = UPROBE_LOG_DEBUG;

    /* parse options */
    while ((opt = getopt(argc, argv, "qd")) != -1) {
        switch (opt) {
            case 'q':
                loglevel++;
                break;
            case 'd':
                loglevel--;
                break;
            default:
                usage(argv[0]);
        }
    }
    if (argc - optind < 3) {
        usage(argv[0]);
    }
    srcpath = argv[optind++];
    dirpath = argv[optind++];
    latency = argv[optind++];

    /* setup environnement */

    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc_default(UPUMP_POOL,
                                                     UPUMP_BLOCKER_POOL);
    struct uclock *uclock = uclock_std_alloc(UCLOCK_FLAG_REALTIME);
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout, loglevel);
    assert(logger != NULL);
    struct uprobe *uprobe_dejitter = uprobe_dejitter_alloc(logger, true, 0);
    assert(uprobe_dejitter != NULL);

    logger = uprobe_uref_mgr_alloc(uprobe_dejitter, uref_mgr);

    assert(logger != NULL);
    logger = uprobe_upump_mgr_alloc(logger, upump_mgr);
    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_POOL_DEPTH);
    assert(logger != NULL);

    logger = uprobe_uclock_alloc(logger, uclock);
    assert(logger != NULL);

    /* rtp source */
    struct upipe_mgr *upipe_udpsrc_mgr = upipe_udpsrc_mgr_alloc();
    struct upipe *upipe_udpsrc = upipe_void_alloc(upipe_udpsrc_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), loglevel, "udp source"));

    if (!ubase_check(upipe_set_uri(upipe_udpsrc, srcpath))) {
        return EXIT_FAILURE;
    }
    upipe_attach_uclock(upipe_udpsrc);

    /* send through rtcp fb receiver */
    struct upipe_mgr *upipe_rtcpfb_mgr = upipe_rtcpfb_mgr_alloc();
    struct upipe *upipe_rtcpfb = upipe_void_alloc_output(upipe_udpsrc, upipe_rtcpfb_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), loglevel, "rtcp fb"));
    upipe_mgr_release(upipe_rtcpfb_mgr);

    if (!ubase_check(upipe_set_option(upipe_rtcpfb, "latency", latency)))
        return EXIT_FAILURE;

    struct uprobe uprobe_udp_rtcp;
    uprobe_init(&uprobe_udp_rtcp, catch_udp, uprobe_use(logger));
    upipe_udpsrc_sub = upipe_void_alloc(upipe_udpsrc_mgr,
            uprobe_pfx_alloc(&uprobe_udp_rtcp, loglevel, "udp source rtcp"));
    upipe_attach_uclock(upipe_udpsrc_sub);

    upipe_mgr_release(upipe_udpsrc_mgr);

    /* catch RTCP XR/NACK messages before they're output to rtcp_fb */
    struct upipe_mgr *upipe_probe_uref_mgr = upipe_probe_uref_mgr_alloc();
    struct upipe *upipe_probe_uref = upipe_void_alloc_output(upipe_udpsrc_sub,
            upipe_probe_uref_mgr, uprobe_pfx_alloc(uprobe_use(logger), loglevel, "probe"));
    assert(upipe_probe_uref);
    upipe_mgr_release(upipe_probe_uref_mgr);

    struct upipe *upipe_rtcp_sub = upipe_void_chain_output_sub(upipe_probe_uref,
        upipe_rtcpfb,
        uprobe_pfx_alloc(uprobe_use(logger), loglevel, "rtcp fb sub"));
    assert(upipe_rtcp_sub);
    upipe_release(upipe_rtcp_sub);

    struct upipe_mgr *dup_mgr = upipe_dup_mgr_alloc();
    struct upipe *dup = upipe_void_chain_output(upipe_rtcpfb, dup_mgr,
            uprobe_pfx_alloc(uprobe_use(logger),
                             loglevel, "dup"));
    upipe_mgr_release(dup_mgr);

    upipe_rtcpfb = upipe_void_alloc_sub(dup,
            uprobe_pfx_alloc(uprobe_use(logger),
                             loglevel, "dup 1"));

    struct upipe *rtcp_dup = upipe_void_alloc_sub(dup,
            uprobe_pfx_alloc(uprobe_use(logger),
                             loglevel, "dup 2"));

    upipe_release(dup);

    struct upipe_mgr *rtcp_mgr = upipe_rtcp_mgr_alloc();
    struct upipe *rtcp = upipe_void_alloc_output(rtcp_dup, rtcp_mgr,
            uprobe_pfx_alloc(uprobe_use(logger),
                             loglevel, "rtcp"));
    upipe_mgr_release(rtcp_mgr);

    /* catch RTCP SR messages before they're output */
    upipe_probe_uref_mgr = upipe_probe_uref_mgr_alloc();
    rtcp = upipe_void_chain_output(rtcp,
            upipe_probe_uref_mgr, uprobe_pfx_alloc(uprobe_use(logger), loglevel, "probe2"));
    assert(rtcp);
    upipe_mgr_release(upipe_probe_uref_mgr);

    /* send to udp */
    struct upipe_mgr *upipe_udpsink_mgr = upipe_udpsink_mgr_alloc();
    upipe_udpsink = upipe_void_alloc_output(upipe_rtcpfb, upipe_udpsink_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), loglevel, "udp sink"));
    upipe_release(upipe_udpsink);

    if (!ubase_check(upipe_set_uri(upipe_udpsink, dirpath))) {
        return EXIT_FAILURE;
    }

    /* send RTCP to udp */
    upipe_udpsink_rtcp = upipe_void_chain_output(rtcp, upipe_udpsink_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), loglevel, "udp sink rtcp"));
    upipe_mgr_release(upipe_udpsink_mgr);
    upipe_release(upipe_udpsink_rtcp);

    struct ustring u = ustring_from_str(dirpath);
    struct uuri_authority authority = uuri_parse_authority(&u);
    struct ustring settings = uuri_parse_path(&u);

    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%.*s", (int)authority.port.len,
            authority.port.at);
    int port = atoi(port_str);

    if (port & 1) {
        fprintf(stderr, "RTP port should be even\n");
        return EXIT_FAILURE;
    }

    char uri[128];
    snprintf(uri, sizeof(uri), "%.*s%s%.*s:%u%.*s",
        (int)authority.userinfo.len, authority.userinfo.at,
        ustring_is_empty(authority.userinfo) ? "" : "@",
        (int)authority.host.len, authority.host.at, port + 1,
        (int)settings.len, settings.at);

    if (!ubase_check(upipe_set_uri(upipe_udpsink_rtcp, uri))) {
        return EXIT_FAILURE;
    }

    int udp_fd = -1;
    ubase_assert(upipe_udpsink_get_fd(upipe_udpsink_rtcp, &udp_fd));
    int flags = fcntl(udp_fd, F_GETFL);
    flags |= O_NONBLOCK;
    if (fcntl(udp_fd, F_SETFL, flags) < 0)
        upipe_err(upipe_udpsink, "Could not set flags");;
    ubase_assert(upipe_udpsrc_set_fd(upipe_udpsrc_sub, udp_fd));

    if (0) {
        struct upump *u = upump_alloc_timer(upump_mgr, stop, upipe_udpsrc,
                NULL, UCLOCK_FREQ, 0);
        upump_start(u);
    }

    /* fire loop ! */
    upump_mgr_run(upump_mgr, NULL);

    /* should never be here for the moment. todo: sighandler.
     * release everything */
    uprobe_release(logger);
    uprobe_clean(&uprobe);
    uprobe_clean(&uprobe_udp_rtcp);

    upump_mgr_release(upump_mgr);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uclock_release(uclock);

    return 0;
}
