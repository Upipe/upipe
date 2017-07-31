/*
 * Copyright (C) 2016-2017 OpenHeadend S.A.R.L.
 * Copyright (C) 2016 DVMR
 *
 * Author: Christophe Massiot
 *         Franck Roger
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
 * @short Upipe implementation of a multicat-like ts mp3 record/replay
 *
 * It converts a TS stream to plain mp3 files, cut at regular intervals.
 * The same utility can then replay a back-to-back mp3 file from a given
 * time, at normal rate.
 *
 * To record:
 * ts2mpthreemulticat -r 1620000000 @239.255.255.255:5004 /tmp/mp3/
 *
 * To replay (to stdout):
 * ts2mpthreemulticat -s 39689293981234567 -r 1620000000 /tmp/mp3/
 */

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_upump_mgr.h>
#include <upipe/uprobe_uclock.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/uprobe_select_flows.h>
#include <upipe/uprobe_dejitter.h>
#include <upipe/uclock.h>
#include <upipe/uclock_std.h>
#include <upipe/umem.h>
#include <upipe/umem_pool.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe/upipe.h>
#include <upipe-modules/upipe_udp_source.h>
#include <upipe-modules/upipe_rtp_source.h>
#include <upipe-modules/upipe_file_source.h>
#include <upipe-modules/upipe_file_sink.h>
#include <upipe-modules/upipe_multicat_sink.h>
#include <upipe-modules/upipe_probe_uref.h>
#include <upipe-ts/upipe_ts_demux.h>
#include <upipe-framers/upipe_auto_framer.h>
#include <upipe-framers/upipe_mpga_framer.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <signal.h>

#include <bitstream/mpeg/mpga.h>

#define UMEM_POOL               512
#define UDICT_POOL_DEPTH        500
#define UREF_POOL_DEPTH         500
#define UBUF_POOL_DEPTH         3000
#define UBUF_SHARED_POOL_DEPTH  50
#define UPUMP_POOL              20
#define UPUMP_BLOCKER_POOL      30
#define READ_SIZE               4096
#define UPROBE_LOG_LEVEL        UPROBE_LOG_WARNING
#define LATENCY                 (5 * UCLOCK_FREQ / 1000)

static struct uclock *uclock;
static struct uprobe *logger;
static struct upipe *source;
static struct upipe *sink;
static char *dirpath;

/* parameters for record */
size_t frame_size = 0;

/* parameters for replay */
static uint64_t file;
static uint64_t start_cr;
static uint64_t frame_duration;
static uint64_t next_cr;

static void usage(const char *argv0) {
    fprintf(stderr, "Usage: %s [-d] [-r <rotate>] [-O <rotate offset>] [[-u] [-k <TS conformance>] <udp source> | -s <start>] <dest dir>\n", argv0);
    fprintf(stderr, "   -d: force debug log level\n");
    fprintf(stderr, "   -u: source has no RTP header\n");
    fprintf(stderr, "   -k: TS conformance\n");
    fprintf(stderr, "   -r: rotate interval in 27MHz unit\n");
    fprintf(stderr, "   -r: rotate offset in 27MHz unit\n");
    fprintf(stderr, "   -s: start time in 27MHz unit for replay\n");
    exit(EXIT_FAILURE);
}

static void stop(void)
{
    upipe_release(source);
    source = NULL;
    upipe_release(sink);
    sink = NULL;
}

static void sighandler(struct upump *upump)
{
    int signal = (int)upump_get_opaque(upump, ptrdiff_t);
    uprobe_err_va(logger, NULL, "signal %s received, exiting",
                  strsignal(signal));
    stop();
}

static int catch_src(struct uprobe *uprobe, struct upipe *upipe,
                     int event, va_list args)
{
    if (event == UPROBE_SOURCE_END) {
        upipe_dbg(upipe, "caught source end, dying");
        stop();
    }
    return uprobe_throw_next(uprobe, upipe, event, args);
}

static int catch_demux_output(struct uprobe *uprobe, struct upipe *upipe,
                              int event, va_list args)
{
    struct uref *flow_def;
    const char *def;
    if (!uprobe_plumber(event, args, &flow_def, &def))
        return uprobe_throw_next(uprobe, upipe, event, args);

    if (sink != NULL)
        upipe_set_output(upipe, sink);
    return UBASE_ERR_NONE;
}

static int catch_uref_check(struct uprobe *uprobe, struct upipe *upipe,
                            int event, va_list args)
{
    if (event != UPROBE_PROBE_UREF)
        return uprobe_throw_next(uprobe, upipe, event, args);

    va_list args_copy;
    va_copy(args_copy, args);
    unsigned int signature = va_arg(args_copy, unsigned int);
    struct uref *uref = va_arg(args_copy, struct uref *);
    va_end(args_copy);
    if (signature != UPIPE_PROBE_UREF_SIGNATURE)
        return uprobe_throw_next(uprobe, upipe, event, args);

    size_t uref_size;
    if (ubase_check(uref_block_size(uref, &uref_size)) &&
        uref_size != frame_size) {
        if (frame_size)
            upipe_warn_va(upipe, "frame size going from %zu to %zu",
                          frame_size, uref_size);
        frame_size = uref_size;
    }
    return UBASE_ERR_NONE;
}

static int catch_multicat_src(struct uprobe *uprobe, struct upipe *upipe,
                              int event, va_list args)
{
    if (event != UPROBE_SOURCE_END)
        return uprobe_throw_next(uprobe, upipe, event, args);

    file++;
    char path[strlen(dirpath) + sizeof(".mp3") +
              sizeof(".18446744073709551615")];
    sprintf(path, "%s/%"PRIu64".mp3", dirpath, file);

    if (!ubase_check(upipe_set_uri(source, path))) {
        upipe_err(upipe, "invalid stream");
        stop();
    }
    return UBASE_ERR_NONE;
}

static int catch_mpgaf(struct uprobe *uprobe, struct upipe *upipe,
                       int event, va_list args)
{
    if (event != UPROBE_NEW_FLOW_DEF)
        return uprobe_throw_next(uprobe, upipe, event, args);

    struct uref *flow_def = va_arg(args, struct uref *);
    uint64_t rate, samples, octetrate;
    if (unlikely(!ubase_check(uref_sound_flow_get_rate(flow_def, &rate)) ||
                 !ubase_check(uref_sound_flow_get_samples(flow_def,
                         &samples)) ||
                 !ubase_check(uref_block_flow_get_octetrate(flow_def,
                         &octetrate)))) {
        upipe_err(upipe, "invalid stream");
        stop();
        return UBASE_ERR_NONE;
    }

    uint64_t frame_size = octetrate * samples / rate;
    upipe_set_output_size(source, frame_size);
    upipe_dbg_va(upipe, "setting frame size to %"PRIu64, frame_size);

    uint64_t offset = octetrate * start_cr / UCLOCK_FREQ;
    offset -= offset % frame_size;
    if (unlikely(!ubase_check(upipe_src_set_position(source, offset)))) {
        upipe_err(upipe, "position not found");
        stop();
    }
    upipe_dbg_va(source, "seeking to position %"PRIu64, offset);
    frame_duration = samples * UCLOCK_FREQ / rate;

    upipe_set_output(source, sink);
    next_cr = uclock_now(uclock) + LATENCY;
    return UBASE_ERR_NONE;
}

static int catch_uref_date(struct uprobe *uprobe, struct upipe *upipe,
                           int event, va_list args)
{
    if (event != UPROBE_PROBE_UREF)
        return uprobe_throw_next(uprobe, upipe, event, args);

    va_list args_copy;
    va_copy(args_copy, args);
    unsigned int signature = va_arg(args_copy, unsigned int);
    struct uref *uref = va_arg(args_copy, struct uref *);
    va_end(args_copy);
    if (signature != UPIPE_PROBE_UREF_SIGNATURE)
        return uprobe_throw_next(uprobe, upipe, event, args);

    uref_clock_set_cr_sys(uref, next_cr);
    next_cr += frame_duration;
    return UBASE_ERR_NONE;
}

int main(int argc, char *argv[])
{
    const char *srcpath = NULL;
    bool udp = false;
    uint64_t rotate = 0;
    uint64_t rotate_offset = 0;
    int64_t start_time = 0;
    int opt;
    enum uprobe_log_level loglevel = UPROBE_LOG_LEVEL;
    enum upipe_ts_conformance conformance = UPIPE_TS_CONFORMANCE_AUTO;

    /* parse options */
    while ((opt = getopt(argc, argv, "r:O:uk:s:d")) != -1) {
        switch (opt) {
            case 'r':
                rotate = strtoull(optarg, NULL, 0);
                break;
            case 'O':
                rotate_offset = strtoull(optarg, NULL, 0);
                break;
            case 'u':
                udp = true;
                break;
            case 'k':
                conformance = upipe_ts_conformance_from_string(optarg);
                break;
            case 's':
                start_time = strtoll(optarg, NULL, 0);
                break;
            case 'd':
                loglevel = UPROBE_LOG_DEBUG;
                break;
            default:
                usage(argv[0]);
        }
    }
    if (argc - optind < 1)
        usage(argv[0]);
    if (argc - optind == 2)
        srcpath = argv[optind++];
    dirpath = argv[optind++];

    /* setup environnement */
    struct umem_mgr *umem_mgr = umem_pool_mgr_alloc_simple(UMEM_POOL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc_default(UPUMP_POOL,
            UPUMP_BLOCKER_POOL);
    uclock = uclock_std_alloc(UCLOCK_FLAG_REALTIME);
    logger = uprobe_stdio_alloc(NULL, stderr, loglevel);
    assert(logger != NULL);
    logger = uprobe_uref_mgr_alloc(logger, uref_mgr);
    assert(logger != NULL);
    logger = uprobe_upump_mgr_alloc(logger, upump_mgr);
    assert(logger != NULL);
    logger = uprobe_uclock_alloc(logger, uclock);
    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_SHARED_POOL_DEPTH);
    assert(logger != NULL);

    struct uprobe *uprobe_dejitter = uprobe_dejitter_alloc(uprobe_use(logger),
                                                           true, 0);
    struct uprobe uprobe_src_s;
    uprobe_init(&uprobe_src_s, catch_src, uprobe_use(logger));
    struct uprobe uprobe_multicat_src_s;
    uprobe_init(&uprobe_multicat_src_s, catch_multicat_src, uprobe_use(logger));
    struct uprobe uprobe_demux_o_s;
    uprobe_init(&uprobe_demux_o_s, catch_demux_output,
                uprobe_use(uprobe_dejitter));
    struct uprobe uprobe_uref_check_s;
    uprobe_init(&uprobe_uref_check_s, catch_uref_check, uprobe_use(logger));
    struct uprobe uprobe_mpgaf_s;
    uprobe_init(&uprobe_mpgaf_s, catch_mpgaf, uprobe_use(logger));
    struct uprobe uprobe_uref_date_s;
    uprobe_init(&uprobe_uref_date_s, catch_uref_date, uprobe_use(logger));

    /* sighandler */
    struct upump *sigint_pump = upump_alloc_signal(upump_mgr, sighandler,
            (void *)SIGINT, NULL, SIGINT);
    upump_set_status(sigint_pump, false);
    upump_start(sigint_pump);
    struct upump *sigterm_pump = upump_alloc_signal(upump_mgr, sighandler,
            (void *)SIGTERM, NULL, SIGTERM);
    upump_set_status(sigterm_pump, false);
    upump_start(sigterm_pump);

    if (srcpath != NULL) {
        /* source */
        struct upipe_mgr *mgr;
        if (udp)
            mgr = upipe_udpsrc_mgr_alloc();
        else
            mgr = upipe_rtpsrc_mgr_alloc();
        source = upipe_void_alloc(mgr,
                uprobe_pfx_alloc(uprobe_use(&uprobe_src_s),
                                 UPROBE_LOG_VERBOSE, "source"));
        assert(source != NULL);
        upipe_mgr_release(mgr);
        upipe_set_output_size(source, READ_SIZE);
        upipe_attach_uclock(source);
        if (!ubase_check(upipe_set_uri(source, srcpath)))
            return EXIT_FAILURE;

        /* ts demux */
        struct upipe_mgr *ts_demux_mgr = upipe_ts_demux_mgr_alloc();
        struct upipe_mgr *autof_mgr = upipe_autof_mgr_alloc();
        upipe_ts_demux_mgr_set_autof_mgr(ts_demux_mgr, autof_mgr);
        upipe_mgr_release(autof_mgr);
        struct upipe *ts_demux = upipe_void_alloc_output(source, ts_demux_mgr,
                uprobe_pfx_alloc(
                    uprobe_selflow_alloc(uprobe_use(logger),
                        uprobe_selflow_alloc(uprobe_use(uprobe_dejitter),
                            &uprobe_demux_o_s, UPROBE_SELFLOW_SOUND, "auto"),
                        UPROBE_SELFLOW_VOID, "auto"),
                    UPROBE_LOG_VERBOSE, "ts demux"));
        assert(ts_demux != NULL);
        upipe_mgr_release(ts_demux_mgr);
        if (conformance != UPIPE_TS_CONFORMANCE_AUTO)
            upipe_ts_demux_set_conformance(ts_demux, conformance);
        upipe_release(ts_demux);

        /* probe uref for sink */
        struct upipe_mgr *probe_uref_mgr = upipe_probe_uref_mgr_alloc();
        sink = upipe_void_alloc(probe_uref_mgr,
                uprobe_pfx_alloc_va(uprobe_use(&uprobe_uref_check_s),
                                    UPROBE_LOG_VERBOSE, "uref check"));
        assert(sink != NULL);
        upipe_mgr_release(probe_uref_mgr);

        /* mp3 files (multicat sink) */
        struct upipe_mgr *multicat_sink_mgr = upipe_multicat_sink_mgr_alloc();
        struct upipe *msink = upipe_void_alloc_output(sink, multicat_sink_mgr,
                uprobe_pfx_alloc(uprobe_use(logger),
                                 UPROBE_LOG_VERBOSE, "sink"));
        assert(msink != NULL);
        upipe_mgr_release(multicat_sink_mgr);
        struct upipe_mgr *fsink_mgr = upipe_fsink_mgr_alloc();
        upipe_multicat_sink_set_fsink_mgr(msink, fsink_mgr);
        upipe_mgr_release(fsink_mgr);
        if (rotate) {
            upipe_multicat_sink_set_rotate(msink, rotate, rotate_offset);
        }
        upipe_multicat_sink_set_path(msink, dirpath, ".mp3");
        upipe_release(msink);

    } else { /* srcpath == NULL */
        if (!rotate)
            usage(argv[0]);

        if (start_time <= 0)
            start_time += uclock_now(uclock);
        if (start_time <= 0)
            usage(argv[0]);
        file = (start_time - rotate_offset) / rotate;
        start_cr = (start_time - rotate_offset) % rotate;
        char path[strlen(dirpath) + sizeof(".mp3") +
                  sizeof(".18446744073709551615")];
        sprintf(path, "%s/%"PRIu64".mp3", dirpath, file);

        /* source */
        struct upipe_mgr *fsrc_mgr = upipe_fsrc_mgr_alloc();
        source = upipe_void_alloc(fsrc_mgr,
                uprobe_pfx_alloc(uprobe_use(&uprobe_multicat_src_s),
                                 UPROBE_LOG_VERBOSE, "source"));
        assert(source != NULL);
        upipe_mgr_release(fsrc_mgr);
        upipe_set_output_size(source, MPGA_HEADER_SIZE);
        if (!ubase_check(upipe_set_uri(source, path)))
            return EXIT_FAILURE;

        /* probe mpga framer */
        struct upipe_mgr *mpgaf_mgr = upipe_mpgaf_mgr_alloc();
        int err = upipe_void_spawn_output(source, mpgaf_mgr,
                    uprobe_pfx_alloc(uprobe_use(&uprobe_mpgaf_s),
                                     UPROBE_LOG_VERBOSE, "mpgaf"));
        ubase_assert(err);
        upipe_mgr_release(mpgaf_mgr);

        /* probe uref for sink */
        struct upipe_mgr *probe_uref_mgr = upipe_probe_uref_mgr_alloc();
        sink = upipe_void_alloc(probe_uref_mgr,
                uprobe_pfx_alloc_va(uprobe_use(&uprobe_uref_date_s),
                                    UPROBE_LOG_VERBOSE, "uref date"));
        assert(sink != NULL);
        upipe_mgr_release(probe_uref_mgr);

        /* stdout */
        struct upipe_mgr *fsink_mgr = upipe_fsink_mgr_alloc();
        struct upipe *fsink = upipe_void_alloc_output(sink, fsink_mgr,
                uprobe_pfx_alloc(uprobe_use(logger),
                                 UPROBE_LOG_VERBOSE, "sink"));
        assert(fsink != NULL);
        upipe_mgr_release(fsink_mgr);
        upipe_fsink_set_fd(fsink, STDOUT_FILENO, UPIPE_FSINK_NONE);
        upipe_attach_uclock(fsink);
        upipe_release(fsink);
    }

    /* fire loop ! */
    upump_mgr_run(upump_mgr, NULL);

    /* release everything */
    upump_stop(sigint_pump);
    upump_free(sigint_pump);
    upump_stop(sigterm_pump);
    upump_free(sigterm_pump);
    uprobe_release(logger);
    uprobe_release(uprobe_dejitter);
    uprobe_clean(&uprobe_src_s);
    uprobe_clean(&uprobe_demux_o_s);

    upump_mgr_release(upump_mgr);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uclock_release(uclock);

    return 0;
}
