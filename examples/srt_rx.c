/*
 * Copyright (C) 2016-2024 Open Broadcast Systems Ltd.
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

#include <upipe/config.h>

#undef NDEBUG
#include "upipe/uprobe.h"
#include "upipe/upipe_dump.h"
#include "upipe/uprobe_stdio.h"
#include "upipe/uprobe_prefix.h"
#include "upipe/uprobe_uref_mgr.h"
#include "upipe/uprobe_upump_mgr.h"
#include "upipe/uprobe_uclock.h"
#include "upipe/uprobe_ubuf_mem.h"
#include "upipe/uprobe_dejitter.h"
#include "upipe/uclock.h"
#include "upipe/uclock_std.h"
#include "upipe/uref_clock.h"
#include "upipe/umem.h"
#include "upipe/umem_alloc.h"
#include "upipe/udict.h"
#include "upipe/udict_inline.h"
#include "upipe/uref.h"
#include "upipe/uref_std.h"
#include "upipe/upump.h"
#include "upump-ev/upump_ev.h"
#include "upipe/uuri.h"
#include "upipe/ustring.h"
#include "upipe/upipe.h"
#include "upipe-modules/upipe_udp_source.h"
#include <upipe-modules/upipe_rtp_decaps.h>
#include "upipe-modules/upipe_udp_sink.h"
#include "upipe-srt/upipe_srt_handshake.h"
#include "upipe-srt/upipe_srt_receiver.h"
#include "upipe/uprobe_helper_uprobe.h"
#include "upipe/uprobe_helper_alloc.h"

#include <arpa/inet.h>

#ifdef UPIPE_HAVE_GCRYPT_H
#include <gcrypt.h>
#endif

#define UDICT_POOL_DEPTH 10
#define UREF_POOL_DEPTH 10
#define UBUF_POOL_DEPTH 10
#define UPUMP_POOL 10
#define UPUMP_BLOCKER_POOL 10
#define READ_SIZE 4096


/* structure */
struct uprobe_obe_log {
    struct urefcount urefcount;
    struct uclock *uclock;
    struct uprobe uprobe;
    uint64_t start;
    uatomic_uint32_t loglevel;
};

/* helper */
UPROBE_HELPER_UPROBE(uprobe_obe_log, uprobe)

/* alloc */
struct uprobe *uprobe_obe_log_alloc(struct uprobe *next);

static int uprobe_obe_log_throw(struct uprobe *uprobe, struct upipe *upipe,
                              int event, va_list args)
{
    struct uprobe_obe_log *probe_obe_log = uprobe_obe_log_from_uprobe(uprobe);
    if (event != UPROBE_LOG)
        return uprobe_throw_next(uprobe, upipe, event, args);

    va_list args_copy;
    va_copy(args_copy, args);
    struct ulog *ulog = va_arg(args_copy, struct ulog *);

    uint32_t loglevel = uatomic_load(&probe_obe_log->loglevel);
    if (loglevel > ulog->level)
        return UBASE_ERR_NONE;

    char time_str[22];
    if (probe_obe_log->uclock) {
        uint64_t t = uclock_now(probe_obe_log->uclock) - probe_obe_log->start;
        snprintf(time_str, sizeof(time_str), "%.2f", (float)t / 27000.);
    } else {
        snprintf(time_str, sizeof(time_str), "?");
    }
    struct ulog_pfx ulog_pfx = {
        .tag = time_str,
    };
    ulist_add(&ulog->prefixes, ulog_pfx_to_uchain(&ulog_pfx));

    return uprobe_throw_next(uprobe, upipe, event, args);
}

static void uprobe_obe_log_set_loglevel(struct uprobe *uprobe, int loglevel)
{
    struct uprobe_obe_log *probe_obe_log = uprobe_obe_log_from_uprobe(uprobe);
    uatomic_store(&probe_obe_log->loglevel, loglevel);
}

static void uprobe_obe_log_set_uclock(struct uprobe *uprobe, struct uclock *uclock)
{
    struct uprobe_obe_log *probe_obe_log = uprobe_obe_log_from_uprobe(uprobe);
    uclock_release(probe_obe_log->uclock);
    probe_obe_log->uclock = uclock_use(uclock);
    probe_obe_log->start = uclock_now(uclock);
}

static struct uprobe *uprobe_obe_log_init(struct uprobe_obe_log *probe_obe_log,
                                       struct uprobe *next)
{
    struct uprobe *probe = uprobe_obe_log_to_uprobe(probe_obe_log);
    probe_obe_log->uclock = NULL;
    probe_obe_log->start = UINT64_MAX;
    uatomic_init(&probe_obe_log->loglevel, UPROBE_LOG_DEBUG);
    uprobe_init(probe, uprobe_obe_log_throw, next);
    return probe;
}

static void uprobe_obe_log_clean(struct uprobe_obe_log *probe_obe_log)
{
    uprobe_clean(uprobe_obe_log_to_uprobe(probe_obe_log));
    uclock_release(probe_obe_log->uclock);
    uatomic_clean(&probe_obe_log->loglevel);
}

#define ARGS_DECL struct uprobe *next
#define ARGS next
UPROBE_HELPER_ALLOC(uprobe_obe_log);
#undef ARGS
#undef ARGS_DECL

static enum uprobe_log_level loglevel = UPROBE_LOG_DEBUG;

static struct upipe_mgr *udp_sink_mgr;
static struct upump_mgr *upump_mgr;

static struct upipe *upipe_udpsrc;
static struct upipe *upipe_udp_sink;
static struct upipe *upipe_srtr_sub;

static struct uprobe uprobe_udp;
static struct uprobe uprobe_srt;
static struct uprobe *logger;

static char *dirpath;
static char *srcpath;
static char *password;
static int key_length = 128;
static char *latency;

static bool restart;

static struct upipe_mgr *rtpd_mgr;

static void addr_to_str(const struct sockaddr *s, char uri[INET6_ADDRSTRLEN+6])
{
    uint16_t port = 0;
    switch(s->sa_family) {
    case AF_INET: {
        struct sockaddr_in *in = (struct sockaddr_in *)s;
        inet_ntop(AF_INET, &in->sin_addr, uri, INET6_ADDRSTRLEN);
        port = ntohs(in->sin_port);
        break;
    }
    case AF_INET6: {
        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)s;
        inet_ntop(AF_INET6, &in6->sin6_addr, uri, INET6_ADDRSTRLEN);
        port = ntohs(in6->sin6_port);
        break;
    }
    default:
        uri[0] = '\0';
    }

    size_t uri_len = strlen(uri);
    sprintf(&uri[uri_len], ":%hu", port);
}

static int start(void)
{
    bool listener = srcpath && strchr(srcpath, '@');
    struct upipe_mgr *upipe_udpsrc_mgr = upipe_udpsrc_mgr_alloc();
    upipe_udpsrc = upipe_void_alloc(upipe_udpsrc_mgr, &uprobe_udp);
    upipe_mgr_release(upipe_udpsrc_mgr);

    struct upipe_mgr *upipe_srt_handshake_mgr = upipe_srt_handshake_mgr_alloc((long)&upipe_udpsrc);
    struct upipe *upipe_srth = upipe_void_alloc_output(upipe_udpsrc,
            upipe_srt_handshake_mgr, &uprobe_srt);
    assert(upipe_srth);
    upipe_set_option(upipe_srth, "listener", listener ? "1" : "0");
    if (!ubase_check(upipe_set_option(upipe_srth, "latency", latency)))
        return EXIT_FAILURE;

    upipe_srt_handshake_set_password(upipe_srth, password, key_length / 8);
    upipe_mgr_release(upipe_srt_handshake_mgr);

    struct upipe_mgr *upipe_srt_receiver_mgr = upipe_srt_receiver_mgr_alloc();
    struct upipe *upipe_srtr = upipe_void_chain_output(upipe_srth,
            upipe_srt_receiver_mgr, uprobe_pfx_alloc(uprobe_use(logger), loglevel, "srtr"));
    assert(upipe_srtr);
    if (!ubase_check(upipe_set_option(upipe_srtr, "latency", latency)))
        return EXIT_FAILURE;

    upipe_mgr_release(upipe_srt_receiver_mgr);

    upipe_srtr_sub = upipe_void_alloc_sub(upipe_srtr,
            uprobe_pfx_alloc(uprobe_use(logger), loglevel, "srtr_sub"));
    assert(upipe_srtr_sub);

    upipe_udp_sink = upipe_void_alloc_output(upipe_srtr_sub,
            udp_sink_mgr, uprobe_pfx_alloc(uprobe_use(logger), loglevel,
                "udpsink"));
    upipe_release(upipe_udp_sink);

    if (rtpd_mgr) {
        upipe_srtr = upipe_void_chain_output(upipe_srtr, rtpd_mgr,
                uprobe_pfx_alloc(uprobe_use(logger),
                    loglevel, "rtpd"));
        assert(upipe_srtr);
    }

    int udp_fd;
    /* receive SRT */
    if (listener) {
        if (!ubase_check(upipe_set_uri(upipe_udpsrc, srcpath)))
            return EXIT_FAILURE;
        ubase_assert(upipe_udpsrc_get_fd(upipe_udpsrc, &udp_fd));

    } else {
        if (!ubase_check(upipe_set_uri(upipe_udp_sink, srcpath)))
            return EXIT_FAILURE;

        ubase_assert(upipe_udpsink_get_fd(upipe_udp_sink, &udp_fd));
        ubase_assert(upipe_udpsrc_set_fd(upipe_udpsrc, dup(udp_fd)));
    }

    struct sockaddr_storage ad;
    socklen_t peer_len = sizeof(ad);
    struct sockaddr *peer = (struct sockaddr*) &ad;

    if (!getsockname(udp_fd, peer, &peer_len)) {
        char uri[INET6_ADDRSTRLEN+6];
        addr_to_str(peer, uri);
        upipe_warn_va(upipe_srth, "Local %s", uri); // XXX: INADDR_ANY when listening
        upipe_srt_handshake_set_peer(upipe_srth, peer, peer_len);
    }

    upipe_attach_uclock(upipe_udpsrc);
    struct upipe *upipe_udp_sink_data = upipe_void_chain_output(upipe_srtr,
            udp_sink_mgr, uprobe_pfx_alloc(uprobe_use(logger), loglevel,
                "udpsink data"));
    upipe_set_uri(upipe_udp_sink_data, dirpath);
    upipe_release(upipe_udp_sink_data);

    return 0;
}

static void stop(struct upump *upump)
{
    if (upump) {
        upump_stop(upump);
        upump_free(upump);
    }

    upipe_release(upipe_udpsrc);
    upipe_release(upipe_srtr_sub);

    if (restart) {
        restart = false;
        start();
    }
}

static void sig_cb(struct upump *upump)
{
    static int done = false;

    if (done)
        abort();
    done = true;

    restart = false;
    stop(NULL);
}

static int catch_srt(struct uprobe *uprobe, struct upipe *upipe,
                 int event, va_list args)
{
    if (event == UPROBE_SOURCE_END) {
        restart = true;
        struct upump *u = upump_alloc_timer(upump_mgr, stop, NULL, NULL, 0, 0);
        upump_start(u);
        return UBASE_ERR_NONE;
    }

    return uprobe_throw_next(uprobe, upipe, event, args);
}

static int catch_udp(struct uprobe *uprobe, struct upipe *upipe,
                 int event, va_list args)
{
    if (event == UPROBE_SOURCE_END) {
        /* This control can not fail, and will trigger restart of upump */
        const char *uri;
        upipe_get_uri(upipe, &uri);
        return UBASE_ERR_NONE;
    }

    if (event != UPROBE_UDPSRC_NEW_PEER)
        return uprobe_throw_next(uprobe, upipe, event, args);

    int sig = va_arg(args, int);
    if (sig != UPIPE_UDPSRC_SIGNATURE)
        return uprobe_throw_next(uprobe, upipe, event, args);

    const struct sockaddr *s = va_arg(args, struct sockaddr*);
    const socklen_t *len = va_arg(args, socklen_t *);
    char uri[INET6_ADDRSTRLEN+6];

    addr_to_str(s, uri);
    upipe_warn_va(upipe, "Remote %s", uri);

    int udp_fd;
    ubase_assert(upipe_udpsrc_get_fd(upipe_udpsrc, &udp_fd));
    ubase_assert(upipe_udpsink_set_fd(upipe_udp_sink, dup(udp_fd)));
    ubase_assert(upipe_udpsink_set_peer(upipe_udp_sink, s, *len));

    return UBASE_ERR_NONE;
}

static void usage(const char *argv0) {
    fprintf(stdout, "Usage: %s [-dr] [-k password] [-l 128] <udp source> <udp dest> <latency>", argv0);
    fprintf(stdout, "   -d: more verbose\n");
    fprintf(stdout, "   -q: more quiet\n");
    fprintf(stdout, "   -r: rtp demux\n");
    fprintf(stdout, "   -k encryption password\n");
    fprintf(stdout, "   -l key length in bits\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    int opt;

    /* parse options */
    while ((opt = getopt(argc, argv, "qrdk:l:")) != -1) {
        switch (opt) {
            case 'd':
                loglevel--;
                break;
            case 'q':
                loglevel++;
                break;
            case 'k':
                password = optarg;
                break;
            case 'l':
                key_length = atoi(optarg);
                break;
            case 'r':
                rtpd_mgr = upipe_rtpd_mgr_alloc();
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

#ifdef UPIPE_HAVE_GCRYPT_H
    gcry_check_version(NULL);
    gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);
#endif

    /* setup environment */

    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    upump_mgr = upump_ev_mgr_alloc_default(UPUMP_POOL,
                                                     UPUMP_BLOCKER_POOL);
    logger = uprobe_stdio_alloc(NULL, stdout, loglevel);
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

    logger = uprobe_obe_log_alloc(logger);

    uprobe_obe_log_set_loglevel(logger, loglevel);
    uprobe_obe_log_set_uclock(logger, uclock);

    logger = uprobe_uclock_alloc(logger, uclock);
    assert(logger != NULL);

    uprobe_init(&uprobe_udp, catch_udp, uprobe_pfx_alloc(uprobe_use(logger), loglevel, "udp source"));
    uprobe_init(&uprobe_srt, catch_srt, uprobe_pfx_alloc(uprobe_use(logger), loglevel, "srth"));

    int ret = start();
    if (ret)
        return ret;

    if (0) {
        //upipe_dump_open(NULL, NULL, "dump.dot", NULL, upipe_udpsrc, NULL);

        struct upump *u = upump_alloc_timer(upump_mgr, stop, NULL, NULL,
                UCLOCK_FREQ, 0);
        upump_start(u);
    }

    struct upump *sigint_pump =
        upump_alloc_signal(upump_mgr, sig_cb,
                           (void *)SIGINT, NULL, SIGINT);
    upump_set_status(sigint_pump, false);
    upump_start(sigint_pump);

    /* fire loop ! */
    upump_mgr_run(upump_mgr, NULL);

    upump_free(sigint_pump);

    uprobe_clean(&uprobe_srt);
    uprobe_clean(&uprobe_udp);
    uprobe_release(logger);

    upump_mgr_release(upump_mgr);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uclock_release(uclock);
    upipe_mgr_release(udp_sink_mgr);
    upipe_mgr_release(rtpd_mgr);

    return 0;
}
