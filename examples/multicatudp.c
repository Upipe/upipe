/*
 * Copyright (C) 2016-2017 OpenHeadend S.A.R.L.
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

/** @file
 * @short Upipe implementation of a multicat-like udp player
 *
 * Pipes and uref/ubuf/upump managers definitions are hardcoded in this
 * example.
 *
 * Usage example :
 *   ./multicatudp -d -r 270000000 -k 270000000 foo/ .ts .aux 239.255.42.77:1234
 * will read files from folder foo (which needs to exist) and play them to udp
 * address 239.255.42.77 the way multicat would do.
 * The rotate interval is 10sec (10sec at 27MHz gives 27000000).
 * The start date is 270000000 (coded in aux files).
 * Please pay attention to the trailing slash in "foo/".
 * If the first argument is a file name, it is opened.
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_syslog.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_uclock.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/uprobe_transfer.h>
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
#include <upipe-modules/upipe_multicat_source.h>
#include <upipe-modules/upipe_delay.h>
#include <upipe-modules/upipe_time_limit.h>
#include <upipe-modules/upipe_udp_sink.h>
#include <upipe-modules/upipe_file_sink.h>
#include <upipe-modules/upipe_worker_sink.h>
#include <upipe-pthread/uprobe_pthread_upump_mgr.h>
#include <upipe-pthread/upipe_pthread_transfer.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <signal.h>
#include <syslog.h>
#include <pthread.h>

#include <ev.h>

#define UDICT_POOL_DEPTH 10
#define UREF_POOL_DEPTH 10
#define UBUF_POOL_DEPTH 10
#define UPUMP_POOL 10
#define UPUMP_BLOCKER_POOL 10
#define XFER_QUEUE 255
#define XFER_POOL 20
#define SINK_QUEUE_LENGTH 2000
#define UPROBE_LOG_LEVEL UPROBE_LOG_NOTICE
#define DEFAULT_ROTATE (UCLOCK_FREQ * 3600)
#define DEFAULT_ROTATE_OFFSET 0
#define DEFAULT_READAHEAD (UCLOCK_FREQ / 5)
#define DEFAULT_MTU 1316

static void usage(const char *argv0) {
    fprintf(stdout, "Usage: %s [-d] [-r <rotate>] [-O <rotate offset>] [-R <read-ahead>] [-k <start>] (-m <MTU>] [-l <syslog ident>] <source dir/prefix> <data suffix> <aux suffix> <destination>\n", argv0);
    fprintf(stdout, "   -d: force debug log level\n");
    fprintf(stdout, "   -r: rotate interval in 27MHz unit\n");
    fprintf(stdout, "   -O: rotate offset in 27MHz unit\n");
    fprintf(stdout, "   -R: read-ahead in 27MHz unit\n");
    fprintf(stdout, "   -k: start time in 27MHz unit\n");
    fprintf(stdout, "   -m: data packet size\n");
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
        case UPROBE_SINK_END:
            assert(0);
            break;
    }
    return UBASE_ERR_NONE;
}

int main(int argc, char *argv[])
{
    const char *syslog_ident = NULL;
    const char *dstpath, *dirpath, *data, *aux;
    uint64_t rotate = DEFAULT_ROTATE;
    uint64_t rotate_offset = DEFAULT_ROTATE_OFFSET;
    uint64_t readahead = DEFAULT_READAHEAD;
    int64_t start = 0;
    unsigned long mtu = DEFAULT_MTU;
    unsigned int rt_priority = 0;
    int opt;
    enum uprobe_log_level loglevel = UPROBE_LOG_LEVEL;

    /* parse options */
    while ((opt = getopt(argc, argv, "r:O:R:k:m:l:i:d")) != -1) {
        switch (opt) {
            case 'r':
                rotate = strtoull(optarg, NULL, 0);
                break;
            case 'O':
                rotate_offset = strtoull(optarg, NULL, 0);
                break;
            case 'R':
                readahead = strtoull(optarg, NULL, 0);
                break;
            case 'k':
                start = strtoll(optarg, NULL, 0);
                break;
            case 'm':
                mtu = strtoul(optarg, NULL, 0);
                break;
            case 'd':
                loglevel = UPROBE_LOG_DEBUG;
                break;
            case 'l':
                syslog_ident = optarg;
                break;
            case 'i':
                rt_priority = strtol(optarg, NULL, 0);
                break;
            default:
                usage(argv[0]);
        }
    }
    if (argc - optind < 4) {
        usage(argv[0]);
    }
    dirpath = argv[optind++];
    data = argv[optind++];
    aux = argv[optind++];
    dstpath = argv[optind++];

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
    struct uprobe *logger;
    if (syslog_ident != NULL) {
        logger = uprobe_syslog_alloc(uprobe_use(&uprobe),
                                     syslog_ident, LOG_NDELAY | LOG_PID,
                                     LOG_USER, loglevel);
    } else {
        logger = uprobe_stdio_alloc(uprobe_use(&uprobe), stderr, loglevel);
    }
    assert(logger != NULL);
    logger = uprobe_uref_mgr_alloc(logger, uref_mgr);
    assert(logger != NULL);
    logger = uprobe_uclock_alloc(logger, uclock);
    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_POOL_DEPTH);
    assert(logger != NULL);
    logger = uprobe_pthread_upump_mgr_alloc(logger);
    assert(logger != NULL);
    uprobe_pthread_upump_mgr_set(logger, upump_mgr);
    upump_mgr_release(upump_mgr);

    /* sink */
    uprobe_throw(logger, NULL, UPROBE_FREEZE_UPUMP_MGR);
    struct uprobe *uprobe_sink = uprobe_xfer_alloc(uprobe_use(logger));
    uprobe_xfer_add(uprobe_sink, UPROBE_XFER_VOID, UPROBE_SINK_END, 0);
    struct upipe *sink;

    struct stat st;
    if (stat(dstpath, &st) == 0) {
        struct upipe_mgr *fsink_mgr = upipe_fsink_mgr_alloc();
        sink = upipe_void_alloc(fsink_mgr,
                uprobe_pfx_alloc(uprobe_sink, UPROBE_LOG_VERBOSE, "fsink"));
        upipe_mgr_release(fsink_mgr);
        assert(sink != NULL);

        if (!ubase_check(upipe_fsink_set_path(sink, dstpath,
                                              UPIPE_FSINK_NONE))) {
            uprobe_err_va(logger, NULL, "unable to open '%s'", dstpath);
            exit(EXIT_FAILURE);
        }
    } else {
        struct upipe_mgr *udpsink_mgr = upipe_udpsink_mgr_alloc();
        sink = upipe_void_alloc(udpsink_mgr,
                uprobe_pfx_alloc(uprobe_sink, UPROBE_LOG_VERBOSE, "fsink"));
        upipe_mgr_release(udpsink_mgr);
        assert(sink != NULL);

        if (!ubase_check(upipe_set_uri(sink, dstpath))) {
            uprobe_err_va(logger, NULL, "unable to open '%s'", dstpath);
            exit(EXIT_FAILURE);
        }
    }
    upipe_attach_uclock(sink);
    upipe_set_max_length(sink, SINK_QUEUE_LENGTH);
    uprobe_throw(logger, NULL, UPROBE_THAW_UPUMP_MGR);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (rt_priority) {
        pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
        pthread_attr_setschedpolicy(&attr, SCHED_RR);
        struct sched_param param;
        param.sched_priority = rt_priority;
        pthread_attr_setschedparam(&attr, &param);
    }
    struct upipe_mgr *xfer_mgr = upipe_pthread_xfer_mgr_alloc(XFER_QUEUE,
            XFER_POOL, uprobe_use(logger), upump_ev_mgr_alloc_loop,
            UPUMP_POOL, UPUMP_BLOCKER_POOL, NULL, NULL, &attr);
    assert(xfer_mgr != NULL);

    /* deport to the RT thread */
    struct upipe_mgr *wsink_mgr = upipe_wsink_mgr_alloc(xfer_mgr);
    upipe_mgr_release(xfer_mgr);
    assert(wsink_mgr != NULL);
    sink = upipe_wsink_alloc(wsink_mgr,
            uprobe_pfx_alloc(
                uprobe_use(logger),
                UPROBE_LOG_VERBOSE, "wsink"),
            sink,
            uprobe_pfx_alloc(uprobe_use(logger),
                             UPROBE_LOG_VERBOSE, "wsink_x"),
            SINK_QUEUE_LENGTH);
    upipe_mgr_release(wsink_mgr);
    assert(sink != NULL);


    struct upipe_mgr *msrc_mgr = upipe_msrc_mgr_alloc();
    struct upipe *msrc = upipe_void_alloc(msrc_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), loglevel,
                             "msrc"));
    assert(msrc != NULL);
    upipe_mgr_release(msrc_mgr);

    struct upipe_mgr *delay_mgr = upipe_delay_mgr_alloc();
    struct upipe *delay = upipe_void_alloc_output(msrc, delay_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), loglevel,
                             "delay"));
    upipe_mgr_release(delay_mgr);
    assert(delay != NULL);

    struct upipe_mgr *time_limit_mgr = upipe_time_limit_mgr_alloc();
    struct upipe *time_limit = upipe_void_alloc_output(delay, time_limit_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), loglevel,
                             "time limit"));
    upipe_mgr_release(time_limit_mgr);
    assert(time_limit != NULL);
    upipe_set_output(time_limit, sink);
    upipe_release(sink);

    struct uref *flow = uref_alloc_control(uref_mgr);
    assert(flow != NULL);
    uref_msrc_flow_set_path(flow, dirpath);
    uref_msrc_flow_set_data(flow, data);
    uref_msrc_flow_set_aux(flow, aux);
    uref_msrc_flow_set_rotate(flow, rotate);
    uref_msrc_flow_set_offset(flow, rotate_offset);
    uint64_t now = uclock_now(uclock);
    if (start < 0)
        start += now;

    if (!ubase_check(upipe_delay_set_delay(delay, now + readahead - start)) ||
        !ubase_check(upipe_time_limit_set_limit(time_limit, readahead)) ||
        !ubase_check(upipe_set_flow_def(msrc, flow)) ||
        !ubase_check(upipe_set_output_size(msrc, mtu)) ||
        !ubase_check(upipe_src_set_position(msrc, start))) {
        uprobe_err(logger, NULL, "unable to start");
        upipe_release(msrc);
    }
    uref_free(flow);
    upipe_release(delay);
    upipe_release(time_limit);

    /* fire loop ! */
    ev_loop(loop, 0);

    uprobe_release(logger);
    uprobe_clean(&uprobe);

    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uclock_release(uclock);

    ev_default_destroy();
    return 0;
}
