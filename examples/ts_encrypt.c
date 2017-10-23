/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
 *
 * Authors: Arnaud de Turckheim
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

#include <upump-ev/upump_ev.h>

#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/umem_pool.h>

#include <upipe/udict.h>
#include <upipe/udict_inline.h>

#include <upipe/uref_std.h>

#include <upipe/uclock_std.h>

#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_upump_mgr.h>
#include <upipe/uprobe_ubuf_mem_pool.h>
#include <upipe/uprobe_transfer.h>
#include <upipe/uprobe_uclock.h>

#include <upipe-modules/upipe_file_source.h>
#include <upipe-modules/upipe_file_sink.h>
#include <upipe-modules/upipe_rtp_source.h>
#include <upipe-modules/upipe_rtp_prepend.h>
#include <upipe-modules/upipe_udp_sink.h>
#include <upipe-modules/upipe_aggregate.h>
#include <upipe-modules/upipe_worker_source.h>
#include <upipe-modules/upipe_worker_sink.h>

#include <upipe-ts/upipe_ts_align.h>
#include <upipe-ts/upipe_ts_check.h>

#include <upipe-dvbcsa/upipe_dvbcsa_bs_encrypt.h>
#include <upipe-dvbcsa/upipe_dvbcsa_encrypt.h>
#include <upipe-dvbcsa/upipe_dvbcsa_bs_decrypt.h>
#include <upipe-dvbcsa/upipe_dvbcsa_decrypt.h>
#include <upipe-dvbcsa/upipe_dvbcsa_common.h>
#include <upipe-dvbcsa/upipe_dvbcsa_split.h>

#include <upipe-pthread/uprobe_pthread_upump_mgr.h>
#include <upipe-pthread/upipe_pthread_transfer.h>

#include <assert.h>
#include <getopt.h>

#define UMEM_POOL                       128
#define UPUMP_POOL                      5
#define UPUMP_BLOCKER_POOL              5
#define UDICT_POOL_DEPTH                500
#define UREF_POOL_DEPTH                 500
#define UBUF_POOL_DEPTH                 3000
#define UBUF_SHARED_POOL_DEPTH          50
#define XFER_QUEUE                      255
#define XFER_POOL                       20
#define FSRC_QUEUE_LENGTH               5
#define SRC_QUEUE_LENGTH                1024

static struct upipe *source = NULL;

enum {
    OPT_DEBUG   = 'v',
    OPT_BATCH   = 'b',
    OPT_DECRYPT = 'D',
    OPT_KEY     = 'k',
    OPT_UDP     = 'U',
    OPT_MTU     = 'M',
    OPT_CONFORMANCE = 'K',
    OPT_LATENCY = 'L',
    OPT_RT_PRIORITY = 'i',
};

struct uprobe_dvbcsa_split {
    struct uprobe uprobe;
    struct upipe *dvbcsa;
};

UBASE_FROM_TO(uprobe_dvbcsa_split, uprobe, uprobe, uprobe);

static int uprobe_dvbcsa_split_catch(struct uprobe *uprobe,
                                     struct upipe *upipe,
                                     int event, va_list args)
{
    struct uprobe_dvbcsa_split *uprobe_dvbcsa_split =
        uprobe_dvbcsa_split_from_uprobe(uprobe);

    if (event < UPROBE_LOCAL ||
        ubase_get_signature(args) != UPIPE_DVBCSA_SPLIT_SIGNATURE)
        return uprobe_throw_next(uprobe, upipe, event, args);

    switch (event) {
        case UPROBE_DVBCSA_SPLIT_ADD_PID: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_DVBCSA_SPLIT_SIGNATURE);
            uint64_t pid = va_arg(args, uint64_t);
            upipe_notice_va(upipe, "add pid %"PRIu64, pid);
            upipe_dvbcsa_add_pid(uprobe_dvbcsa_split->dvbcsa, pid);
            break;
        }

        case UPROBE_DVBCSA_SPLIT_DEL_PID: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_DVBCSA_SPLIT_SIGNATURE);
            uint64_t pid = va_arg(args, uint64_t);
            upipe_notice_va(upipe, "add pid %"PRIu64, pid);
            upipe_dvbcsa_del_pid(uprobe_dvbcsa_split->dvbcsa, pid);
            break;
        }
    }

    return UBASE_ERR_NONE;
}

static void usage(const char *name)
{
    fprintf(stderr, "%s [options] <input> <output>\n"
            "\t-v   : be more verbose\n"
            "\t-b   : use batch dvbcsa\n"
            "\t-k   : set BISS key\n"
            "\t-L   : set the maximum latency in milliseconds\n"
            "\t-i   : RT priority for source and sink\n"
            "\t-D   : decrypt instead of encrypt\n",
            name);
}

/** @This handles SIGINT and SIGTERM signal. */
static void sigint_cb(struct upump *upump)
{
    upipe_release(source);
    source = NULL;
}

static inline int catch_src(struct uprobe *uprobe,
                            struct upipe *upipe,
                            int event,
                            va_list args)
{
    if (event == UPROBE_SOURCE_END) {
        upipe_release(source);
        source = NULL;
        return UBASE_ERR_NONE;
    }
    return uprobe_throw_next(uprobe, upipe, event, args);
}

int main(int argc, char *argv[])
{
    unsigned int rt_priority = 0;
    int log_level = UPROBE_LOG_NOTICE;
    bool decryption = false;
    bool use_batch = false;
    const char *key = NULL;
    int latency = -1;
    int c;

    while ((c = getopt(argc, argv, "vbk:M:K:L:i:D")) != -1) {
        switch (c) {
            case OPT_DEBUG:
                if (log_level == UPROBE_LOG_DEBUG)
                    log_level = UPROBE_LOG_VERBOSE;
                else
                    log_level = UPROBE_LOG_DEBUG;
                break;
            case OPT_BATCH:
                use_batch = true;
                break;
            case OPT_KEY:
                key = optarg;
                break;
            case OPT_UDP:
            case OPT_MTU:
            case OPT_CONFORMANCE:
                break;
            case OPT_LATENCY:
                latency = atoi(optarg);
                if (latency < 0) {
                    fprintf(stderr, "invalid latency");
                    usage(argv[0]);
                    exit(-1);
                }
                break;
            case OPT_RT_PRIORITY:
                rt_priority = atoi(optarg);
                break;
            case OPT_DECRYPT:
                decryption = true;
                break;
            default:
                usage(argv[0]);
                exit(-1);
        }
    }

    if (optind + 2 > argc) {
        usage(argv[0]);
        exit(-1);
    }

    const char *in = argv[optind];
    const char *out = argv[optind + 1];

    struct uclock *uclock = uclock_std_alloc(0);
    assert(uclock);

    struct upump_mgr *upump_mgr =
        upump_ev_mgr_alloc_default(UPUMP_POOL, UPUMP_BLOCKER_POOL);
    assert(upump_mgr);

    struct umem_mgr *umem_mgr = umem_pool_mgr_alloc(32, 18,
                               UMEM_POOL, /* 32 */
                               UMEM_POOL, /* 64 */
                               UMEM_POOL, /* 128 */
                               16384, /* 256 */
                               UMEM_POOL, /* 512 */
                               UMEM_POOL, /* 1 Ki */
                               UMEM_POOL, /* 2 Ki */
                               UMEM_POOL, /* 4 Ki */
                               UMEM_POOL / 2, /* 8 Ki */
                               UMEM_POOL / 2, /* 16 Ki */
                               UMEM_POOL / 2, /* 32 Ki */
                               UMEM_POOL / 4, /* 64 Ki */
                               UMEM_POOL / 4, /* 128 Ki */
                               UMEM_POOL / 4, /* 256 Ki */
                               UMEM_POOL / 4, /* 512 Ki */
                               UMEM_POOL / 8, /* 1 Mi */
                               UMEM_POOL / 8, /* 2 Mi */
                               UMEM_POOL / 8); /* 4 Mi */
    assert(umem_mgr);

    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr);

    struct uref_mgr *uref_mgr =
        uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    assert(uref_mgr);

    struct uprobe *uprobe_main = uprobe_stdio_alloc(NULL, stderr, log_level);
    assert(uprobe_main);
    uprobe_main = uprobe_ubuf_mem_pool_alloc(uprobe_main, umem_mgr,
                                             UBUF_POOL_DEPTH,
                                             UBUF_SHARED_POOL_DEPTH);
    assert(uprobe_main);
    uprobe_main = uprobe_uref_mgr_alloc(uprobe_main, uref_mgr);
    assert(uprobe_main);
    uprobe_main = uprobe_upump_mgr_alloc(uprobe_main, upump_mgr);
    assert(uprobe_main);
    uprobe_main = uprobe_uclock_alloc(uprobe_main, uclock);
    assert(uprobe_main);
    uprobe_main = uprobe_pthread_upump_mgr_alloc(uprobe_main);
    assert(uprobe_main);
    uprobe_pthread_upump_mgr_set(uprobe_main, upump_mgr);

    /*
     * source thread
     */
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (rt_priority) {
        assert(!pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED));
        assert(!pthread_attr_setschedpolicy(&attr, SCHED_RR));
        struct sched_param param;
        param.sched_priority = rt_priority;
        assert(!pthread_attr_setschedparam(&attr, &param));
    }
    struct upipe_mgr *xfer_mgr = upipe_pthread_xfer_mgr_alloc(XFER_QUEUE,
            XFER_POOL, uprobe_use(uprobe_main), upump_ev_mgr_alloc_loop,
            UPUMP_POOL, UPUMP_BLOCKER_POOL, NULL, NULL, &attr);
    assert(xfer_mgr != NULL);
    struct upipe_mgr *upipe_wsrc_mgr = upipe_wsrc_mgr_alloc(xfer_mgr);
    assert(upipe_wsrc_mgr != NULL);

    /*
     * sink thread
     */
    struct upipe_mgr *upipe_wsink_mgr = upipe_wsink_mgr_alloc(xfer_mgr);
    assert(upipe_wsink_mgr != NULL);
    upipe_mgr_release(xfer_mgr);

    /*
     * source
     */
    struct uprobe *uprobe_src = uprobe_xfer_alloc(uprobe_use(uprobe_main));
    uprobe_xfer_add(uprobe_src, UPROBE_XFER_VOID, UPROBE_SOURCE_END, 0);

    unsigned int src_queue_length = FSRC_QUEUE_LENGTH;
    /* try file source */
    struct upipe_mgr *upipe_fsrc_mgr = upipe_fsrc_mgr_alloc();
    assert(upipe_fsrc_mgr);
    source =
        upipe_void_alloc(
            upipe_fsrc_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_src),
                             UPROBE_LOG_VERBOSE, "src"));
    assert(source);
    upipe_mgr_release(upipe_fsrc_mgr);
    if (!source || !ubase_check(upipe_set_uri(source, in))) {
        /* try rtp source */
        upipe_release(source);

        struct upipe_mgr *upipe_rtpsrc_mgr = upipe_rtpsrc_mgr_alloc();
        assert(upipe_rtpsrc_mgr);
        source = upipe_void_alloc(upipe_rtpsrc_mgr,
                                  uprobe_pfx_alloc(uprobe_use(uprobe_src),
                                                   UPROBE_LOG_VERBOSE, "src"));
        assert(source);
        upipe_mgr_release(upipe_rtpsrc_mgr);
        ubase_assert(upipe_attach_uclock(source));
        ubase_assert(upipe_set_uri(source, in));
        src_queue_length = SRC_QUEUE_LENGTH;
    }
    else
        upipe_attach_upump_mgr(source);
    uprobe_release(uprobe_src);

    source = upipe_wsrc_alloc(upipe_wsrc_mgr,
            uprobe_pfx_alloc(
                uprobe_alloc(catch_src, uprobe_use(uprobe_main)),
                UPROBE_LOG_VERBOSE, "wsrc"),
            source,
            uprobe_pfx_alloc(uprobe_use(uprobe_main),
                             UPROBE_LOG_VERBOSE, "wsrc_x"),
            src_queue_length);
    upipe_mgr_release(upipe_wsrc_mgr);

    /*
     * ts encrypt
     */
    struct upipe *dec;
    struct upipe_mgr *upipe_ts_align_mgr = upipe_ts_align_mgr_alloc();
    assert(upipe_ts_align_mgr);
    dec =
        upipe_void_alloc(
            upipe_ts_align_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_main),
                             UPROBE_LOG_VERBOSE, "align"));
    assert(dec);
    upipe_mgr_release(upipe_ts_align_mgr);

    struct upipe_mgr *upipe_ts_check_mgr = upipe_ts_check_mgr_alloc();
    assert(upipe_ts_check_mgr);
    struct upipe *output =
        upipe_void_alloc_output(
            dec, upipe_ts_check_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_main),
                             UPROBE_LOG_VERBOSE, "check"));
    assert(output);
    upipe_mgr_release(upipe_ts_check_mgr);

    struct uprobe_dvbcsa_split uprobe_dvbcsa_split;
    uprobe_init(&uprobe_dvbcsa_split.uprobe, uprobe_dvbcsa_split_catch,
                uprobe_use(uprobe_main));
    uprobe_dvbcsa_split.dvbcsa = NULL;
    struct upipe_mgr *upipe_dvbcsa_split_mgr =
        upipe_dvbcsa_split_mgr_alloc();
    assert(upipe_dvbcsa_split_mgr);
    output =
        upipe_void_chain_output(
            output, upipe_dvbcsa_split_mgr,
            uprobe_pfx_alloc(uprobe_use(&uprobe_dvbcsa_split.uprobe),
                             UPROBE_LOG_VERBOSE, "split"));
    assert(output);
    upipe_mgr_release(upipe_dvbcsa_split_mgr);

    struct upipe_mgr *upipe_dvbcsa_mgr;
    if (decryption) {
        if (use_batch)
            upipe_dvbcsa_mgr = upipe_dvbcsa_bs_dec_mgr_alloc();
        else
            upipe_dvbcsa_mgr = upipe_dvbcsa_dec_mgr_alloc();
    }
    else {
        if (use_batch)
            upipe_dvbcsa_mgr = upipe_dvbcsa_bs_enc_mgr_alloc();
        else
            upipe_dvbcsa_mgr = upipe_dvbcsa_enc_mgr_alloc();
    }
    assert(upipe_dvbcsa_mgr);
    output =
        upipe_void_chain_output(
            output, upipe_dvbcsa_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_main),
                             UPROBE_LOG_VERBOSE, "encrypt"));
    assert(output);
    upipe_mgr_release(upipe_dvbcsa_mgr);
    ubase_assert(upipe_dvbcsa_set_key(output, key));
    if (use_batch && latency > 0)
        ubase_assert(
            upipe_dvbcsa_set_max_latency(output,
                                         latency * (UCLOCK_FREQ / 1000)));
    uprobe_dvbcsa_split.dvbcsa = output;

    struct upipe_mgr *upipe_agg_mgr = upipe_agg_mgr_alloc();
    assert(upipe_agg_mgr);
    output = upipe_void_chain_output(
        output, upipe_agg_mgr,
        uprobe_pfx_alloc(uprobe_use(uprobe_main),
                         UPROBE_LOG_VERBOSE, "agg"));
    assert(output);
    upipe_mgr_release(upipe_agg_mgr);

    ubase_assert(upipe_set_output(source, dec));
    upipe_release(dec);
    dec = output;

    /*
     * sink
     */
    struct upipe *sink = NULL;

    /* try rtp */
    struct upipe_mgr *upipe_rtp_prepend_mgr = upipe_rtp_prepend_mgr_alloc();
    assert(upipe_rtp_prepend_mgr);
    sink =
        upipe_void_alloc(
            upipe_rtp_prepend_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_main),
                             UPROBE_LOG_VERBOSE, "rtpp"));
    assert(sink);
    upipe_mgr_release(upipe_rtp_prepend_mgr);

    struct upipe_mgr *upipe_udpsink_mgr = upipe_udpsink_mgr_alloc();
    assert(upipe_udpsink_mgr);
    struct upipe *udpsink =
        upipe_void_alloc_output(
            sink, upipe_udpsink_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_main),
                             UPROBE_LOG_VERBOSE, "udp"));
    assert(udpsink);
    ubase_assert(upipe_attach_uclock(udpsink));
    upipe_mgr_release(upipe_udpsink_mgr);
    if (!ubase_check(upipe_set_uri(udpsink, out))) {
        upipe_release(udpsink);
        upipe_release(sink);

        struct upipe_mgr *upipe_fsink_mgr = upipe_fsink_mgr_alloc();
        assert(upipe_fsink_mgr);
        sink =
            upipe_void_alloc(
                upipe_fsink_mgr,
                uprobe_pfx_alloc(uprobe_use(uprobe_main),
                                 UPROBE_LOG_VERBOSE, "file sink"));
        assert(sink);
        upipe_mgr_release(upipe_fsink_mgr);

        ubase_assert(upipe_fsink_set_path(sink, out, UPIPE_FSINK_OVERWRITE));
    }
    else {
        ubase_assert(upipe_set_output(sink, udpsink));
        upipe_release(udpsink);
    }

    sink = upipe_wsink_alloc(upipe_wsink_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_main),
                             UPROBE_LOG_VERBOSE, "wsink audio"),
            sink,
            uprobe_pfx_alloc(uprobe_use(uprobe_main),
                             UPROBE_LOG_VERBOSE, "wsink_x audio"),
            src_queue_length);
    assert(sink != NULL);
    upipe_mgr_release(upipe_wsink_mgr);
    ubase_assert(upipe_set_output(dec, sink));
    upipe_release(dec);
    upipe_release(sink);

    struct upump *sigint_pump = upump_alloc_signal(upump_mgr, sigint_cb,
            (void *)SIGINT, NULL, SIGINT);
    upump_set_status(sigint_pump, false);
    upump_start(sigint_pump);
    struct upump *sigterm_pump = upump_alloc_signal(upump_mgr, sigint_cb,
            (void *)SIGTERM, NULL, SIGTERM);
    upump_set_status(sigterm_pump, false);
    upump_start(sigterm_pump);

    upump_mgr_run(upump_mgr, NULL);

    uprobe_clean(&uprobe_dvbcsa_split.uprobe);
    upump_free(sigint_pump);
    upump_free(sigterm_pump);
    upipe_release(source);
    uprobe_release(uprobe_main);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    upump_mgr_release(upump_mgr);
    uclock_release(uclock);
    return 0;
}
