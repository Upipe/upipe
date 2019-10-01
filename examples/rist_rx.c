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
#include <upipe/uref_clock.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe/uuri.h>
#include <upipe/ustring.h>
#include <upipe/upipe.h>
#include <upipe-modules/upipe_null.h>
#include <upipe-modules/upipe_dup.h>
#include <upipe-modules/upipe_udp_source.h>
#include <upipe-modules/upipe_file_source.h>
#include <upipe-modules/upipe_file_sink.h>
#include <upipe-modules/upipe_multicat_sink.h>
#include <upipe-modules/upipe_udp_sink.h>
#include <upipe-modules/upipe_probe_uref.h>
#include <upipe-filters/upipe_rtp_feedback.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define UDICT_POOL_DEPTH 10
#define UREF_POOL_DEPTH 10
#define UBUF_POOL_DEPTH 10
#define UPUMP_POOL 10
#define UPUMP_BLOCKER_POOL 10
#define READ_SIZE 4096

static enum uprobe_log_level loglevel = UPROBE_LOG_DEBUG;

static struct upipe_mgr *udp_sink_mgr;
static struct upump_mgr *upump_mgr;

static struct upipe *upipe_rtpfb;
static struct upipe *upipe_rtpfb_sub;
static struct upipe *upipe_udpsrc;
static struct upipe *upipe_udpsrc_rtcp;
static struct upipe *upipe_dup;

struct rtcp_sink {
    struct upipe *dup_sub;
    struct sockaddr_storage addr;
    socklen_t addr_len;
    struct upump *timeout;
};

static struct rtcp_sink rtcp_sink[2];

static int udp_fd = -1;

static void usage(const char *argv0) {
    fprintf(stdout, "Usage: %s [-d]  <udp source> <udp dest> <latency>", argv0);
    fprintf(stdout, "   -d: more verbose\n");
    fprintf(stdout, "   -q: more quiet\n");
    exit(EXIT_FAILURE);
}

static void gather_stats(struct upipe *upipe, struct uref *uref)
{
    uint64_t cr_sys = 0;
    if (unlikely(!ubase_check(uref_clock_get_cr_sys(uref, &cr_sys)))) {
        upipe_err(upipe, "Couldn't read cr_sys in probe_uref");
    }

    static uint64_t last_print;
    if (unlikely(last_print == 0))
        last_print = cr_sys;

    if ((cr_sys - last_print) < 3 * UCLOCK_FREQ)
        return;

    last_print = cr_sys;

    unsigned expected_seqnum, last_output_seqnum;
    size_t buffers, nacks, repairs, loss, dups;
    if (unlikely(!ubase_check(upipe_rtpfb_get_stats(upipe_rtpfb,
                &expected_seqnum, &last_output_seqnum,
                &buffers, &nacks, &repairs, &loss, &dups
                ))))
        upipe_err_va(upipe, "Couldn't get stats from rtpfb");

    unsigned nack_overflow = (repairs && repairs < nacks) ? (nacks - repairs ) * 100 / repairs : 0;
    upipe_notice_va(upipe, "%5u (%3zu) %5u\t%zu repairs %zu NACKS (%u%% too much)\tlost %zu\tduplicates %zu",
            last_output_seqnum, buffers, expected_seqnum, repairs, nacks, nack_overflow, loss, dups);
}

static void sink_timeout(struct upump *upump)
{
    struct rtcp_sink *sink = upump_get_opaque(upump, struct rtcp_sink *);
    upump_stop(upump);
    upump_free(upump);

    upipe_err_va(sink->dup_sub, "timeout");
    sink->timeout = NULL;
    upipe_release(sink->dup_sub);
    sink->dup_sub = NULL;
}

static struct rtcp_sink *last_peer;

/** definition of our uprobe */
static int catch_udp(struct uprobe *uprobe, struct upipe *upipe,
                 int event, va_list args)
{
    const char *uri;
    switch (event) {
    case UPROBE_UDPSRC_NEW_PEER: {
        int sig = va_arg(args, int);
        if (sig != UPIPE_UDPSRC_SIGNATURE)
            return uprobe_throw_next(uprobe, upipe, event, args);

        const struct sockaddr *s = va_arg(args, struct sockaddr*);
        const struct sockaddr_in *in = (const struct sockaddr_in*) s;
        socklen_t addr_len = sizeof(*in);
        const socklen_t *len = va_arg(args, socklen_t *);

        if (s->sa_family != AF_INET) {
            upipe_err_va(upipe, "New UDP remote, unknown addr family %d",
                s->sa_family);
            return UBASE_ERR_NONE;
        }

        if (*len < addr_len) {
            upipe_err_va(upipe, "Too small AF_INET address");
            return UBASE_ERR_NONE;
        }

        upipe_dbg_va(upipe, "Got new remote: %s:%hu ",
                inet_ntoa(in->sin_addr), ntohs(in->sin_port));

        const size_t n = sizeof(rtcp_sink) / sizeof(*rtcp_sink);
        struct rtcp_sink *sink = NULL;
        ssize_t avail = -1; /* index of the free remote */
        for (size_t i = 0; i < n; i++) {
            if (!rtcp_sink[i].dup_sub) {
                if (!sink) {
                    avail = i;
                    sink = &rtcp_sink[i];
                }
                continue;
            }

            if (memcmp(&rtcp_sink[i].addr, in, addr_len))
                continue;

            upipe_dbg_va(upipe, "Remote already existing");
            sink = &rtcp_sink[i];
            upump_stop(sink->timeout);
            break;
        }

        if (!sink) {
            upipe_err_va(upipe, "Too many RTCP remotes already");
            return UBASE_ERR_NONE;
        }

        if (last_peer) /* restart the timeout for the previous peer we got */
            upump_restart(last_peer->timeout);

        /* keep the timer for this remote stopped for now,
         * this could be the only one we have */
        last_peer = sink;

        if (sink->dup_sub)
            return UBASE_ERR_NONE;

        sink->dup_sub = upipe_void_alloc_sub(upipe_dup,
                uprobe_pfx_alloc_va(uprobe_use(uprobe), loglevel,
                    "dup %zu", avail));
        assert(sink->dup_sub);

        struct upipe *rtcp_sink = upipe_void_alloc_output(sink->dup_sub,
                udp_sink_mgr, uprobe_pfx_alloc_va(uprobe_use(uprobe), loglevel,
                    "udpsink rtpfb %zu", avail));
        ubase_assert(upipe_udpsink_set_fd(rtcp_sink, dup(udp_fd)));
        upipe_release(rtcp_sink);

        sink->addr_len = addr_len;
        memcpy(&sink->addr, in, addr_len);

        ubase_assert(upipe_udpsink_set_peer(rtcp_sink,
                    (const struct sockaddr*)&sink->addr, addr_len));

        sink->timeout = upump_alloc_timer(upump_mgr, sink_timeout,
                sink, NULL, 3 * UCLOCK_FREQ, 3 * UCLOCK_FREQ);

        return UBASE_ERR_NONE;
    }
    case UPROBE_SOURCE_END:
        /* This control can not fail, and will trigger restart of upump */
        upipe_get_uri(upipe, &uri);
        return UBASE_ERR_NONE;
    default:
        return uprobe_throw_next(uprobe, upipe, event, args);
    }
}

/** definition of our uprobe */
static int catch(struct uprobe *uprobe, struct upipe *upipe,
                 int event, va_list args)
{
    switch (event) {
        default:
            break;
        case UPROBE_SOURCE_END:
            if (upipe->mgr->signature == UPIPE_DUP_OUTPUT_SIGNATURE) {
                const size_t n = sizeof(rtcp_sink) / sizeof(*rtcp_sink);
                for (size_t i = 0; i < n; i++) {
                    struct rtcp_sink *sink = &rtcp_sink[i];
                    if (sink->dup_sub == upipe) {
                        upump_stop(sink->timeout);
                        sink->dup_sub = NULL;
                    }
                }
            }
            upipe_release(upipe);
            break;
        case UPROBE_PROBE_UREF: {
            int sig = va_arg(args, int);
            if (sig != UPIPE_PROBE_UREF_SIGNATURE)
                return UBASE_ERR_INVALID;
            struct uref *uref = va_arg(args, struct uref *);
            va_arg(args, struct upump **);
            gather_stats(upipe, uref);
            break;
        }
    }
    return UBASE_ERR_NONE;
}

static void stop(struct upump *upump)
{
    upump_stop(upump);
    upump_free(upump);

    upipe_release(upipe_udpsrc_rtcp);
    upipe_release(upipe_udpsrc);
}

int main(int argc, char *argv[])
{
    char *dirpath, *latency, *srcpath;
    int opt;

    /* parse options */
    while ((opt = getopt(argc, argv, "qd")) != -1) {
        switch (opt) {
            case 'd':
                loglevel--;
                break;
            case 'q':
                loglevel++;
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
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    upump_mgr = upump_ev_mgr_alloc_default(UPUMP_POOL,
                                                     UPUMP_BLOCKER_POOL);
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
    struct uclock *uclock = NULL;

    udp_sink_mgr = upipe_udpsink_mgr_alloc();

    uclock = uclock_std_alloc(UCLOCK_FLAG_REALTIME);

    logger = uprobe_uclock_alloc(logger, uclock);
    assert(logger != NULL);

    /* rtp source */
    struct upipe_mgr *upipe_udpsrc_mgr = upipe_udpsrc_mgr_alloc();
    upipe_udpsrc = upipe_void_alloc(upipe_udpsrc_mgr, uprobe_pfx_alloc(uprobe_use(logger),
                loglevel, "udp source"));

    /* rtcp source */
    struct uprobe uprobe_udp;
    uprobe_init(&uprobe_udp, catch_udp, uprobe_pfx_alloc(uprobe_use(logger),
                loglevel, "udp rtcp source"));
    upipe_udpsrc_rtcp = upipe_void_alloc(upipe_udpsrc_mgr, &uprobe_udp);
    upipe_mgr_release(upipe_udpsrc_mgr);

    struct upipe_mgr *upipe_probe_uref_mgr = upipe_probe_uref_mgr_alloc();
    struct upipe *upipe_probe_uref = upipe_void_alloc_output(upipe_udpsrc,
            upipe_probe_uref_mgr, uprobe_use(logger));
    assert(upipe_probe_uref);
    upipe_mgr_release(upipe_probe_uref_mgr);
    upipe_release(upipe_probe_uref);

    struct upipe_mgr *upipe_rtpfb_mgr = upipe_rtpfb_mgr_alloc();
    upipe_rtpfb = upipe_void_alloc_output(upipe_probe_uref,
            upipe_rtpfb_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), loglevel, "rtpfb"));
    upipe_mgr_release(upipe_rtpfb_mgr);

    upipe_rtpfb_sub = upipe_void_alloc_output_sub(upipe_udpsrc_rtcp, upipe_rtpfb,
            uprobe_pfx_alloc(uprobe_use(logger), loglevel, "rtpfb_sub"));
    assert(upipe_rtpfb_sub);

    upipe_rtpfb_output_set_name(upipe_rtpfb_sub, "Upipe");

    struct upipe_mgr *dup_mgr = upipe_dup_mgr_alloc();
    upipe_dup = upipe_void_chain_output(upipe_rtpfb_sub, dup_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), loglevel, "dup rtpfb_sub"));
    assert(upipe_dup);
    upipe_release(upipe_dup);
    upipe_mgr_release(dup_mgr);

    if (!ubase_check(upipe_set_option(upipe_rtpfb, "latency", latency))) {
        return EXIT_FAILURE;
    }

    /* receive RTP */
    if (!ubase_check(upipe_set_uri(upipe_udpsrc, srcpath))) {
        return EXIT_FAILURE;
    }

    struct ustring u = ustring_from_str(srcpath);
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
    snprintf(uri, sizeof(uri), "%.*s@%.*s:%u%.*s",
        (int)authority.userinfo.len, authority.userinfo.at,
        (int)authority.host.len, authority.host.at, port + 1,
        (int)settings.len, settings.at);

    if (!ubase_check(upipe_set_uri(upipe_udpsrc_rtcp, uri))) {
        return EXIT_FAILURE;
    }

    upipe_attach_uclock(upipe_udpsrc);
    upipe_attach_uclock(upipe_udpsrc_rtcp);

    ubase_assert(upipe_udpsrc_get_fd(upipe_udpsrc_rtcp, &udp_fd));
    assert(udp_fd != -1);

    struct upipe *upipe_udp_sink = upipe_void_chain_output(upipe_rtpfb,
            udp_sink_mgr, uprobe_pfx_alloc(uprobe_use(logger), loglevel,
                "udpsink"));
    upipe_set_uri(upipe_udp_sink, dirpath);

    upipe_release(upipe_udp_sink);

    if (0) {
        struct upump *u = upump_alloc_timer(upump_mgr, stop, NULL, NULL,
                UCLOCK_FREQ, 0);
        upump_start(u);
    }

    /* fire loop ! */
    upump_mgr_run(upump_mgr, NULL);

    /* should never be here for the moment. todo: sighandler.
     * release everything */
    uprobe_clean(&uprobe);
    uprobe_clean(&uprobe_udp);
    uprobe_release(logger);

    const size_t n = sizeof(rtcp_sink) / sizeof(*rtcp_sink);
    for (size_t i = 0; i < n; i++) {
        struct rtcp_sink *sink = &rtcp_sink[i];
        if (sink->timeout)
            upump_free(sink->timeout);
    }

    upump_mgr_release(upump_mgr);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uclock_release(uclock);
    upipe_mgr_release(udp_sink_mgr);

    return 0;
}
