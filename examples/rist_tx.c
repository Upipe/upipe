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
#include "upipe/uprobe.h"
#include "upipe/uprobe_stdio.h"
#include "upipe/uprobe_prefix.h"
#include "upipe/uprobe_uref_mgr.h"
#include "upipe/uprobe_upump_mgr.h"
#include "upipe/uprobe_uclock.h"
#include "upipe/uprobe_ubuf_mem.h"
#include "upipe/uprobe_dejitter.h"
#include "upipe/uclock.h"
#include "upipe/uclock_std.h"
#include "upipe/umem.h"
#include "upipe/umem_alloc.h"
#include "upipe/udict.h"
#include "upipe/udict_inline.h"
#include "upipe/ubuf.h"
#include "upipe/ubuf_block.h"
#include "upipe/uref.h"
#include "upipe/uref_block.h"
#include "upipe/uref_clock.h"
#include "upipe/uref_std.h"
#include "upipe/upump.h"
#include "upipe/upipe_dump.h"
#include "upump-ev/upump_ev.h"
#include "upipe/uuri.h"
#include "upipe/ustring.h"
#include "upipe/upipe.h"
#include "upipe-modules/upipe_udp_source.h"
#include "upipe-modules/upipe_udp_sink.h"

#include "upipe-srt/upipe_srt_sender.h"
#include "upipe-srt/upipe_srt_handshake.h"

#include <fcntl.h>
#include <arpa/inet.h>

#ifdef UPIPE_HAVE_GCRYPT_H
#include <gcrypt.h>
#endif

#define UDICT_POOL_DEPTH 10
#define UREF_POOL_DEPTH 10
#define UBUF_POOL_DEPTH 10
#define UPUMP_POOL 10
#define UPUMP_BLOCKER_POOL 10

static void usage(const char *argv0) {
    fprintf(stdout, "Usage: %s [-d] <udp source> <udp dest> <latency>\n", argv0);
    fprintf(stdout, "   -d: more verbose\n");
    fprintf(stdout, "   -q: more quiet\n");
    fprintf(stdout, "   -k encryption password\n");
    exit(EXIT_FAILURE);
}

static struct upipe *upipe_udpsink;
static struct upipe *upipe_udpsrc_srt;
static struct upipe *upipe_udpsrc;
static struct upipe *upipe_srt_handshake_sub;
static struct upipe *upipe_srt_sender;
static struct upipe *upipe_srt_sender_sub;

static struct upump_mgr *upump_mgr;
static struct uref_mgr *uref_mgr;

static char *srcpath;
static char *dirpath;
static char *latency;
static char *password;

static enum uprobe_log_level loglevel = UPROBE_LOG_DEBUG;

static struct uprobe uprobe_udp_srt;
static struct uprobe *logger;

static bool restart;

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

static void stop(struct upump *upump);

/** definition of our uprobe */
static int catch_udp(struct uprobe *uprobe, struct upipe *upipe,
                 int event, va_list args)
{
    switch (event) {
    case UPROBE_SOURCE_END:
        upipe_warn(upipe, "Remote end not listening, can't receive SRT");
        restart = true;
        struct upump *u = upump_alloc_timer(upump_mgr, stop, upipe_udpsrc,
                NULL, UCLOCK_FREQ, 0);
        upump_start(u);
        return uprobe_throw_next(uprobe, upipe, event, args);
    case UPROBE_UDPSRC_NEW_PEER: {
        int udp_fd;
        int sig = va_arg(args, int);
        if (sig != UPIPE_UDPSRC_SIGNATURE)
            break;

        const struct sockaddr *s = va_arg(args, struct sockaddr*);
        const socklen_t *len = va_arg(args, socklen_t *);

        char uri[INET6_ADDRSTRLEN+6];
        addr_to_str(s, uri);
        upipe_warn_va(upipe, "Remote %s", uri);

        ubase_assert(upipe_udpsrc_get_fd(upipe_udpsrc_srt, &udp_fd));
        ubase_assert(upipe_udpsink_set_fd(upipe_udpsink, dup(udp_fd)));

        ubase_assert(upipe_udpsink_set_peer(upipe_udpsink, s, *len));

        return UBASE_ERR_NONE;
    }
    }
    return uprobe_throw_next(uprobe, upipe, event, args);
}

static int start(void)
{
    bool listener = dirpath && *dirpath == '@';

    /* rtp source */
    struct upipe_mgr *upipe_udpsrc_mgr = upipe_udpsrc_mgr_alloc();
    upipe_udpsrc = upipe_void_alloc(upipe_udpsrc_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), loglevel, "udp source data"));

    if (!ubase_check(upipe_set_uri(upipe_udpsrc, srcpath))) {
        return EXIT_FAILURE;
    }
    upipe_attach_uclock(upipe_udpsrc);

    /* send through srt sender */
    struct upipe_mgr *upipe_srt_sender_mgr = upipe_srt_sender_mgr_alloc();
    upipe_srt_sender = upipe_void_alloc_output(upipe_udpsrc, upipe_srt_sender_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), loglevel, "srt sender"));
    upipe_mgr_release(upipe_srt_sender_mgr);

    if (!ubase_check(upipe_set_option(upipe_srt_sender, "latency", latency)))
        return EXIT_FAILURE;

    uprobe_init(&uprobe_udp_srt, catch_udp, uprobe_use(logger));
    upipe_udpsrc_srt = upipe_void_alloc(upipe_udpsrc_mgr,
            uprobe_pfx_alloc(&uprobe_udp_srt, loglevel, "udp source srt"));
    upipe_attach_uclock(upipe_udpsrc_srt);

    struct upipe_mgr *upipe_srt_handshake_mgr = upipe_srt_handshake_mgr_alloc();
    struct upipe *upipe_srt_handshake = upipe_void_alloc_output(upipe_udpsrc_srt, upipe_srt_handshake_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), loglevel, "srt handshake"));
    upipe_set_option(upipe_srt_handshake, "listener", listener ? "1" : "0");
    upipe_srt_handshake_set_password(upipe_srt_handshake, password);

    upipe_mgr_release(upipe_srt_handshake_mgr);

    upipe_mgr_release(upipe_udpsrc_mgr);

    upipe_srt_handshake_sub = upipe_void_alloc_sub(upipe_srt_handshake,
        uprobe_pfx_alloc(uprobe_use(logger), loglevel, "srt handshake sub"));
    assert(upipe_srt_handshake_sub);

    upipe_srt_sender_sub = upipe_void_chain_output_sub(upipe_srt_handshake,
        upipe_srt_sender,
        uprobe_pfx_alloc(uprobe_use(logger), loglevel, "srt sender sub"));
    assert(upipe_srt_sender_sub);
    upipe_release(upipe_srt_sender_sub);

    /* send to udp */
    struct upipe_mgr *upipe_udpsink_mgr = upipe_udpsink_mgr_alloc();
    upipe_udpsink = upipe_void_chain_output(upipe_srt_sender, upipe_udpsink_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), loglevel, "udp sink"));
    upipe_release(upipe_udpsink);

    upipe_set_output(upipe_srt_handshake_sub, upipe_udpsink);

    int udp_fd = -1;
    if (listener) {
        if (!ubase_check(upipe_set_uri(upipe_udpsrc_srt, dirpath))) {
            return EXIT_FAILURE;
        }
        ubase_assert(upipe_udpsrc_get_fd(upipe_udpsrc_srt, &udp_fd));
    } else {
        if (!ubase_check(upipe_set_uri(upipe_udpsink, dirpath))) {
            return EXIT_FAILURE;
        }

        ubase_assert(upipe_udpsink_get_fd(upipe_udpsink, &udp_fd));
        int flags = fcntl(udp_fd, F_GETFL);
        flags |= O_NONBLOCK;
        if (fcntl(udp_fd, F_SETFL, flags) < 0)
            upipe_err(upipe_udpsink, "Could not set flags");;
        ubase_assert(upipe_udpsrc_set_fd(upipe_udpsrc_srt, udp_fd));
    }

    struct sockaddr_storage ad;
    socklen_t peer_len = sizeof(ad);
    struct sockaddr *peer = (struct sockaddr*) &ad;

    if (!getsockname(udp_fd, peer, &peer_len)) {
        char uri[INET6_ADDRSTRLEN+6];
        addr_to_str(peer, uri);
        upipe_warn_va(upipe_srt_handshake, "Local %s", uri); // XXX: INADDR_ANY when listening
        upipe_srt_handshake_set_peer(upipe_srt_handshake, peer, peer_len);
    }

    return 0;
}

static void stop(struct upump *upump)
{
    if (upump) {
        upump_stop(upump);
        upump_free(upump);
    }

    upipe_release(upipe_udpsrc_srt);
    upipe_release(upipe_udpsrc);
    upipe_release(upipe_srt_handshake_sub);

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

    stop(NULL);
}


int main(int argc, char *argv[])
{
    int opt;

    /* parse options */
    while ((opt = getopt(argc, argv, "qdk:")) != -1) {
        switch (opt) {
            case 'q':
                loglevel++;
                break;
            case 'd':
                loglevel--;
                break;
                 break;
            case 'k':
                password = optarg;
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
    uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    upump_mgr = upump_ev_mgr_alloc_default(UPUMP_POOL,
                                                     UPUMP_BLOCKER_POOL);
    struct uclock *uclock = uclock_std_alloc(UCLOCK_FLAG_REALTIME);
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

    logger = uprobe_uclock_alloc(logger, uclock);
    assert(logger != NULL);

    int ret = start();
    if (ret)
        return ret;

    if (0) {
        upipe_dump_open(NULL, NULL, "dump.dot", NULL, upipe_udpsink, upipe_udpsrc, upipe_udpsrc_srt, NULL);
        struct upump *u = upump_alloc_timer(upump_mgr, stop, upipe_udpsrc,
                NULL, UCLOCK_FREQ, 0);
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

    /* should never be here for the moment. todo: sighandler.
     * release everything */
    uprobe_release(logger);
    uprobe_clean(&uprobe_udp_srt);

    upump_mgr_release(upump_mgr);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uclock_release(uclock);

    return 0;
}
