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
 *
 
[udp] -- data --> [dup] --> {start: front,0;} [datasink], [genaux]
[genaux] -- aux --> [auxsink]

+-----+  data   +-----+          +----------+  aux   +---------+
| udp | ------> | dup | -+-----> |  genaux  | -----> | auxsink |
+-----+         +-----+  |       +----------+        +---------+
                         |       +----------+
                         +-----> | datasink |
                                 +----------+
 */

/** @file
 * @short Upipe implementation of a multicat-like udp recorder/forwarder
 *
 * Pipes and uref/ubuf/upump managers definitions are hardcoded in this
 * example.
 *
 * Usage example :
 *   ./udpmulticat -d -r 270000000 @239.255.42.77:1234 foo/ .ts will
 * listen to multicast address 239.255.42.77:1234 and outputfiles in
 * folder foo (which needs to exist) the way multicat would do.
 * The rotate interval is 10sec (10sec at 27MHz gives 27000000).
 * Please pay attention to the trailing slash in "foo/".
 * If no suffix is specified, udpmulticat will send data to a udp socket.
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
#include <upipe/ubuf_block_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe/upipe.h>
#include <upipe-modules/upipe_genaux.h>
#include <upipe-modules/upipe_dup.h>
#include <upipe-modules/upipe_udp_source.h>
#include <upipe-modules/upipe_file_sink.h>
#include <upipe-modules/upipe_multicat_sink.h>
#include <upipe-modules/upipe_udp_sink.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <signal.h>

#include <ev.h>

#define UDICT_POOL_DEPTH 10
#define UREF_POOL_DEPTH 10
#define UBUF_POOL_DEPTH 10
#define UPUMP_POOL 10
#define UPUMP_BLOCKER_POOL 10
#define READ_SIZE 4096
#define UPROBE_LOG_LEVEL UPROBE_LOG_WARNING

static void usage(const char *argv0) {
    fprintf(stdout, "Usage: %s [-d] [-r <rotate>] <udp source> <dest dir/prefix> [<suffix>]\n", argv0);
    fprintf(stdout, "   -d: force debug log level\n");
    fprintf(stdout, "   -r: rotate interval in 27MHz unit\n");
    fprintf(stdout, "If no <suffix> specified, udpmulticat sends data to a udp socket\n");
    exit(EXIT_FAILURE);
}

/** definition of our uprobe */
static int catch(struct uprobe *uprobe, struct upipe *upipe,
                 int event, va_list args)
{
    switch (event) {
        default:
            break;
        case UPROBE_SOURCE_END:
            upipe_release(upipe);
            break;
    }
    return UBASE_ERR_NONE;
}

int main(int argc, char *argv[])
{
    const char *srcpath, *dirpath, *suffix = NULL;
    bool udp = false;
    uint64_t rotate = 0;
    int opt;
    enum uprobe_log_level loglevel = UPROBE_LOG_LEVEL;

    /* parse options */
    while ((opt = getopt(argc, argv, "r:d")) != -1) {
        switch (opt) {
            case 'r':
                rotate = strtoull(optarg, NULL, 0);
                break;
            case 'd':
                loglevel = UPROBE_LOG_DEBUG;
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
    if (argc - optind >= 1) {
        suffix = argv[optind++];
    } else {
        udp = true;
    }

    /* setup environnement */
    struct ev_loop *loop = ev_default_loop(0);
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop, UPUMP_POOL,
                                                     UPUMP_BLOCKER_POOL);
    struct uclock *uclock = uclock_std_alloc(UCLOCK_FLAG_REALTIME);
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout, loglevel);
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

    struct upipe_mgr *upipe_multicat_sink_mgr = upipe_multicat_sink_mgr_alloc();
    struct upipe_mgr *upipe_fsink_mgr = upipe_fsink_mgr_alloc();


    /* udp source */
    struct upipe_mgr *upipe_udpsrc_mgr = upipe_udpsrc_mgr_alloc();
    struct upipe *upipe_udpsrc = upipe_void_alloc(upipe_udpsrc_mgr,
            uprobe_pfx_alloc(uprobe_use(logger),
                             loglevel, "udp source"));
    upipe_set_output_size(upipe_udpsrc, READ_SIZE);
    upipe_attach_uclock(upipe_udpsrc);
    if (!ubase_check(upipe_set_uri(upipe_udpsrc, srcpath))) {
        return EXIT_FAILURE;
    }

    if (udp) {
        /* send to udp */
        struct upipe_mgr *upipe_udpsink_mgr = upipe_udpsink_mgr_alloc();
        struct upipe *upipe_sink = upipe_void_alloc_output(upipe_udpsrc,
                upipe_udpsink_mgr,
                uprobe_pfx_alloc(uprobe_use(logger),
                                 loglevel, "udpsink"));
        if (!ubase_check(upipe_udpsink_set_uri(upipe_sink, dirpath, 0))) {
            return EXIT_FAILURE;
        }
        upipe_release(upipe_sink);
    }
    else 
    {
        /* dup */
        struct upipe_mgr *upipe_dup_mgr = upipe_dup_mgr_alloc();
        struct upipe *upipe_dup = upipe_void_alloc_output(upipe_udpsrc,
                upipe_dup_mgr,
                uprobe_pfx_alloc(uprobe_use(logger), loglevel, "dup"));

        struct upipe *upipe_dup_data = upipe_void_alloc_sub(upipe_dup,
                    uprobe_pfx_alloc(uprobe_use(logger),
                                     loglevel, "dupdata"));
        struct upipe *upipe_dup_aux = upipe_void_alloc_sub(upipe_dup,
                    uprobe_pfx_alloc(uprobe_use(logger),
                                     loglevel, "dupaux"));

        /* data files (multicat sink) */
        struct upipe *datasink = upipe_void_alloc_output(upipe_dup_data,
                upipe_multicat_sink_mgr,
                uprobe_pfx_alloc(uprobe_use(logger),
                                 loglevel, "datasink"));
        upipe_multicat_sink_set_fsink_mgr(datasink, upipe_fsink_mgr);
        if (rotate) {
            upipe_multicat_sink_set_rotate(datasink, rotate);
        }
        upipe_multicat_sink_set_path(datasink, dirpath, suffix);
        upipe_release(datasink);

        /* aux block generation pipe */
        struct upipe_mgr *upipe_genaux_mgr = upipe_genaux_mgr_alloc();
        struct upipe *genaux = upipe_void_alloc_output(upipe_dup_aux,
                upipe_genaux_mgr,
                uprobe_pfx_alloc(uprobe_use(logger),
                                 loglevel, "genaux"));

        /* aux files (multicat sink) */
        struct upipe *auxsink = upipe_void_alloc_output(genaux,
                upipe_multicat_sink_mgr,
                uprobe_pfx_alloc(uprobe_use(logger),
                                 loglevel, "auxsink"));
        upipe_multicat_sink_set_fsink_mgr(auxsink, upipe_fsink_mgr);
        if (rotate) {
            upipe_multicat_sink_set_rotate(auxsink, rotate);
        }
        upipe_multicat_sink_set_path(auxsink, dirpath, ".aux");
        upipe_release(genaux);
        upipe_release(auxsink);
        upipe_release(upipe_dup);
    }

    /* fire loop ! */
    ev_loop(loop, 0);

    /* should never be here for the moment. todo: sighandler.
     * release everything */
    uprobe_release(logger);
    uprobe_clean(&uprobe);

    upump_mgr_release(upump_mgr);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uclock_release(uclock);

    ev_default_destroy();
    return 0;
}
