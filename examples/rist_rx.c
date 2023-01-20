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

#include <upipe/config.h>

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
#include "upipe-modules/upipe_udp_sink.h"
#include "upipe-srt/upipe_srt_handshake.h"
#include "upipe-srt/upipe_srt_receiver.h"

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

static enum uprobe_log_level loglevel = UPROBE_LOG_DEBUG;

static struct upipe_mgr *udp_sink_mgr;
static struct upump_mgr *upump_mgr;

static struct upipe *upipe_udpsrc;
static struct upipe *upipe_udp_sink;
static struct upipe *upipe_srth_sub;
static struct upipe *upipe_srtr_sub;

static struct uprobe uprobe_udp;
static struct uprobe uprobe_srt;
static struct uprobe *logger;

static char *dirpath;
static char *srcpath;
static char *password;

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

static int start(void)
{
    bool listener = srcpath && *srcpath == '@';
    struct upipe_mgr *upipe_udpsrc_mgr = upipe_udpsrc_mgr_alloc();
    upipe_udpsrc = upipe_void_alloc(upipe_udpsrc_mgr, &uprobe_udp);
    upipe_mgr_release(upipe_udpsrc_mgr);

    struct upipe_mgr *upipe_srt_handshake_mgr = upipe_srt_handshake_mgr_alloc();
    struct upipe *upipe_srth = upipe_void_alloc_output(upipe_udpsrc,
            upipe_srt_handshake_mgr, &uprobe_srt);
    assert(upipe_srth);
    upipe_set_option(upipe_srth, "listener", listener ? "1" : "0");
    upipe_srt_handshake_set_password(upipe_srth, password);
    upipe_mgr_release(upipe_srt_handshake_mgr);

    upipe_srth_sub = upipe_void_alloc_sub(upipe_srth,
            uprobe_pfx_alloc(uprobe_use(logger), loglevel, "srth_sub"));
    assert(upipe_srth_sub);
    upipe_udp_sink = upipe_void_alloc_output(upipe_srth_sub,
            udp_sink_mgr, uprobe_pfx_alloc(uprobe_use(logger), loglevel,
                "udpsink"));
    upipe_release(upipe_udp_sink);

    struct upipe_mgr *upipe_srt_receiver_mgr = upipe_srt_receiver_mgr_alloc();
    struct upipe *upipe_srtr = upipe_void_chain_output(upipe_srth,
            upipe_srt_receiver_mgr, uprobe_pfx_alloc(uprobe_use(logger), loglevel, "srtr"));
    assert(upipe_srtr);
    upipe_mgr_release(upipe_srt_receiver_mgr);

    upipe_srtr_sub = upipe_void_alloc_sub(upipe_srtr,
            uprobe_pfx_alloc(uprobe_use(logger), loglevel, "srtr_sub"));
    assert(upipe_srtr_sub);
    upipe_set_output(upipe_srtr_sub, upipe_udp_sink);

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
    upump_stop(upump);
    upump_free(upump);

    upipe_release(upipe_srth_sub);
    upipe_release(upipe_srtr_sub);
    upipe_release(upipe_udpsrc);

    if (restart) {
        restart = false;
        start();
    }
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
    fprintf(stdout, "Usage: %s [-d] [-k password] <udp source> <udp dest>", argv0);
    fprintf(stdout, "   -d: more verbose\n");
    fprintf(stdout, "   -q: more quiet\n");
    fprintf(stdout, "   -k encryption password\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    int opt;

    /* parse options */
    while ((opt = getopt(argc, argv, "qdk:")) != -1) {
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
            default:
                usage(argv[0]);
        }
    }
    if (argc - optind < 2) {
        usage(argv[0]);
    }
    srcpath = argv[optind++];
    dirpath = argv[optind++];

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

    logger = uprobe_uclock_alloc(logger, uclock);
    assert(logger != NULL);

    uprobe_init(&uprobe_udp, catch_udp, uprobe_pfx_alloc(uprobe_use(logger), loglevel, "udp source"));
    uprobe_init(&uprobe_srt, catch_srt, uprobe_pfx_alloc(uprobe_use(logger), loglevel, "srth"));

    int ret = start();
    if (ret)
        return ret;

    if (0) {
        struct upump *u = upump_alloc_timer(upump_mgr, stop, NULL, NULL,
                UCLOCK_FREQ, 0);
        upump_start(u);
    }

    /* fire loop ! */
    upump_mgr_run(upump_mgr, NULL);

    /* should never be here for the moment. todo: sighandler.
     * release everything */
    uprobe_clean(&uprobe_srt);
    uprobe_clean(&uprobe_udp);
    uprobe_release(logger);

    upump_mgr_release(upump_mgr);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uclock_release(uclock);
    upipe_mgr_release(udp_sink_mgr);

    return 0;
}
