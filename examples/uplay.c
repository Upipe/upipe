/*
 * Copyright (C) 2014-2015 OpenHeadend S.A.R.L.
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
 *
 */

/** @file
 * @short plays a URI
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_select_flows.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_ubuf_mem_pool.h>
#include <upipe/uprobe_uclock.h>
#include <upipe/uprobe_dejitter.h>
#include <upipe/uprobe_transfer.h>
#include <upipe/umem.h>
#include <upipe/umem_pool.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/ubuf.h>
#include <upipe/uclock.h>
#include <upipe/uclock_std.h>
#include <upipe/upipe.h>
#include <upipe/upump.h>
#include <upipe-pthread/upipe_pthread_transfer.h>
#include <upipe-pthread/uprobe_pthread_upump_mgr.h>
#include <upump-ev/upump_ev.h>
#include <upipe-modules/upipe_file_source.h>
#include <upipe-modules/upipe_udp_source.h>
#include <upipe-modules/upipe_rtp_source.h>
#include <upipe-modules/upipe_http_source.h>
#include <upipe-modules/upipe_null.h>
#include <upipe-modules/upipe_play.h>
#include <upipe-modules/upipe_trickplay.h>
#include <upipe-modules/upipe_worker_source.h>
#include <upipe-modules/upipe_worker_linear.h>
#include <upipe-modules/upipe_worker_sink.h>
#include <upipe-ts/upipe_ts_demux.h>
#include <upipe-framers/upipe_mpgv_framer.h>
#include <upipe-framers/upipe_h264_framer.h>
#include <upipe-framers/upipe_mpga_framer.h>
#include <upipe-framers/upipe_a52_framer.h>
#include <upipe-filters/upipe_filter_decode.h>
#include <upipe-filters/upipe_filter_format.h>
#include <upipe-av/upipe_av.h>
#include <upipe-av/upipe_avformat_source.h>
#include <upipe-av/upipe_avcodec_decode.h>
#include <upipe-swscale/upipe_sws.h>
#include <upipe-gl/upipe_glx_sink.h>
#include <upipe-gl/uprobe_gl_sink_cube.h>
#ifdef UPIPE_HAVE_ALSA_ASOUNDLIB_H
#include <upipe-alsa/upipe_alsa_sink.h>
#endif

#include <ev.h>
#include <pthread.h>

#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG
#define UMEM_POOL               512
#define UDICT_POOL_DEPTH        500
#define UREF_POOL_DEPTH         500
#define UBUF_POOL_DEPTH         3000
#define UBUF_SHARED_POOL_DEPTH  50
#define UPUMP_POOL              10
#define UPUMP_BLOCKER_POOL      10
#define DEJITTER_DIVIDER        100
#define XFER_QUEUE              255
#define XFER_POOL               20
#define FSRC_OUT_QUEUE_LENGTH   5
#define SRC_OUT_QUEUE_LENGTH    10000
#define DEC_IN_QUEUE_LENGTH     5
#define DEC_OUT_QUEUE_LENGTH    2
#define SOUND_QUEUE_LENGTH      10

/* true if we receive raw udp */
static bool udp = false;
/** selflow string for video */
static const char *select_video = "auto";
/** selflow string for audio */
static const char *select_audio = "auto";
/** selflow string for program */
static const char *select_program = "auto";
/* upump manager for the main thread */
static struct upump_mgr *main_upump_mgr = NULL;
/* main (thread-safe) probe, whose first element is uprobe_pthread_upump_mgr */
static struct uprobe *uprobe_main = NULL;
/* probe for demux */
static struct uprobe *uprobe_dejitter = NULL;
/* probe for source worker pipe */
static struct uprobe uprobe_src_s;
/* probe for demux video subpipe */
static struct uprobe uprobe_video_s;
/* probe for demux audio subpipe */
static struct uprobe uprobe_audio_s;
/* probe for glx sink */
static struct uprobe uprobe_glx_s;
/* source thread */
static struct upipe_mgr *upipe_wsrc_mgr = NULL;
/* decoder thread */
static struct upipe_mgr *upipe_wlin_mgr = NULL;
/* sink thread */
static struct upipe_mgr *upipe_wsink_mgr = NULL;
/* play */
static struct upipe *play = NULL;
/* trick play */
static struct upipe *trickp = NULL;
/* source pipe */
static struct upipe *upipe_src = NULL;

static void uplay_stop(struct upump *upump);

/* probe for glx sink */
static int catch_glx(struct uprobe *uprobe, struct upipe *upipe,
                     int event, va_list args)
{
    if (event == UPROBE_GLX_SINK_KEYRELEASE)
        return UBASE_ERR_NONE;

    if (event != UPROBE_GLX_SINK_KEYPRESS)
        return uprobe_throw_next(uprobe, upipe, event, args);

    UBASE_SIGNATURE_CHECK(args, UPIPE_GLX_SINK_SIGNATURE);
    unsigned long key = va_arg(args, unsigned long);

    switch (key) {
        case 27:
        case 'q': {
            if (main_upump_mgr != NULL) {
                upipe_notice_va(upipe, "exit key pressed (%d), exiting",
                                key);
                struct upump *idler_stop = upump_alloc_idler(main_upump_mgr,
                                                             uplay_stop,
                                                             (void *)1);
                upump_start(idler_stop);
            }
            break;
        }
        case ' ': {
            if (trickp != NULL) {
                struct urational rate;
                upipe_trickp_get_rate(trickp, &rate);
                if (!rate.num) { /* paused */
                    rate.num = 1;
                    rate.den = 1;
                } else {
                    rate.num = 0;
                    rate.den = 0;
                }
                upipe_trickp_set_rate(trickp, rate);
            }
            break;
        }
        default:
            upipe_dbg_va(upipe, "key pressed (%d)", key);
            break;
    }
    return UBASE_ERR_NONE;
}

/* probe for video subpipe of demux */
static int catch_video(struct uprobe *uprobe, struct upipe *upipe,
                       int event, va_list args)
{
    struct uref *flow_def;
    const char *def;
    if (!uprobe_plumber(event, args, &flow_def, &def))
        return uprobe_throw_next(uprobe, upipe, event, args);

    if (upipe_wlin_mgr == NULL) /* we're dying */
        return UBASE_ERR_UNHANDLED;

    struct upipe_mgr *fdec_mgr = upipe_fdec_mgr_alloc();
    struct upipe_mgr *avcdec_mgr = upipe_avcdec_mgr_alloc();
    upipe_fdec_mgr_set_avcdec_mgr(fdec_mgr, avcdec_mgr);
    upipe_mgr_release(avcdec_mgr);
    struct upipe *avcdec = upipe_void_alloc(fdec_mgr,
        uprobe_pfx_alloc_va(uprobe_use(uprobe_main),
                            UPROBE_LOG_VERBOSE, "avcdec video"));
    assert(avcdec != NULL);
    upipe_mgr_release(fdec_mgr);
    upipe_set_option(avcdec, "threads", "4");

    struct upipe_mgr *ffmt_mgr = upipe_ffmt_mgr_alloc();
    struct upipe_mgr *sws_mgr = upipe_sws_mgr_alloc();
    upipe_ffmt_mgr_set_sws_mgr(ffmt_mgr, sws_mgr);
    upipe_mgr_release(sws_mgr);

    struct uref *uref = uref_sibling_alloc(flow_def);
    uref_flow_set_def(uref, "pic.");

    struct upipe *ffmt = upipe_flow_alloc_output(avcdec, ffmt_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_main),
                             UPROBE_LOG_VERBOSE, "ffmt"),
            uref);
    assert(ffmt != NULL);
    uref_free(uref);
    upipe_mgr_release(ffmt_mgr);
    upipe_release(ffmt);

    /* deport to the decoder thread */
    avcdec = upipe_wlin_alloc(upipe_wlin_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_main),
                             UPROBE_LOG_VERBOSE, "wlin video"),
            avcdec,
            uprobe_pfx_alloc(uprobe_use(uprobe_main),
                             UPROBE_LOG_VERBOSE, "wlin_x video"),
            DEC_IN_QUEUE_LENGTH, DEC_OUT_QUEUE_LENGTH);
    assert(avcdec != NULL);
    upipe_set_output(upipe, avcdec);

    if (trickp != NULL)
        avcdec = upipe_void_chain_output_sub(avcdec, trickp,
                uprobe_pfx_alloc(uprobe_use(uprobe_main),
                                 UPROBE_LOG_VERBOSE, "trickp video"));

    avcdec = upipe_void_chain_output_sub(avcdec, play,
            uprobe_pfx_alloc(uprobe_use(uprobe_main),
                             UPROBE_LOG_VERBOSE, "play video"));

    struct upipe_mgr *upipe_glx_mgr = upipe_glx_sink_mgr_alloc();
    avcdec = upipe_void_chain_output(avcdec, upipe_glx_mgr,
            uprobe_gl_sink_cube_alloc(
                uprobe_pfx_alloc(uprobe_use(&uprobe_glx_s),
                                 UPROBE_LOG_VERBOSE, "glx")));
    assert(avcdec != NULL);
    upipe_mgr_release(upipe_glx_mgr);
    upipe_glx_sink_init(avcdec, 0, 0, 800, 480);
    upipe_attach_uclock(avcdec);

    upipe_release(avcdec);
    return UBASE_ERR_NONE;
}

/* probe for audio subpipe of demux */
static int catch_audio(struct uprobe *uprobe, struct upipe *upipe,
                       int event, va_list args)
{
    struct uref *flow_def;
    const char *def;
    if (!uprobe_plumber(event, args, &flow_def, &def))
        return uprobe_throw_next(uprobe, upipe, event, args);

    if (upipe_wlin_mgr == NULL) /* we're dying */
        return UBASE_ERR_UNHANDLED;

    struct upipe_mgr *upipe_avcdec_mgr = upipe_avcdec_mgr_alloc();
    struct upipe *avcdec = upipe_void_alloc(upipe_avcdec_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_main),
                             UPROBE_LOG_VERBOSE, "avcdec audio"));
    assert(avcdec != NULL);
    upipe_mgr_release(upipe_avcdec_mgr);

    /* deport to the decoder thread */
    avcdec = upipe_wlin_alloc(upipe_wlin_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_main),
                             UPROBE_LOG_VERBOSE, "wlin audio"),
            avcdec,
            uprobe_pfx_alloc(uprobe_use(uprobe_main),
                             UPROBE_LOG_VERBOSE, "wlin_x audio"),
            DEC_IN_QUEUE_LENGTH, DEC_OUT_QUEUE_LENGTH);
    assert(avcdec != NULL);
    upipe_set_output(upipe, avcdec);

    if (trickp != NULL)
        avcdec = upipe_void_chain_output_sub(avcdec, trickp,
                uprobe_pfx_alloc(uprobe_use(uprobe_main),
                                 UPROBE_LOG_VERBOSE, "trickp audio"));

    avcdec = upipe_void_chain_output_sub(avcdec, play,
            uprobe_pfx_alloc(uprobe_use(uprobe_main),
                             UPROBE_LOG_VERBOSE, "play audio"));

    struct upipe *sink;
#ifdef UPIPE_HAVE_ALSA_ASOUNDLIB_H
    struct upipe_mgr *upipe_alsink_mgr = upipe_alsink_mgr_alloc();
    sink = upipe_void_alloc(upipe_alsink_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_main), UPROBE_LOG_VERBOSE,
                             "alsink"));
    assert(sink != NULL);
    upipe_mgr_release(upipe_alsink_mgr);
    upipe_attach_uclock(sink);
#else
    struct upipe_mgr *upipe_null_mgr = upipe_null_mgr_alloc();
    sink = upipe_void_alloc(upipe_null_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_main), UPROBE_LOG_VERBOSE,
                             "null"));
    upipe_mgr_release(upipe_null_mgr);
#endif

    /* deport to the sink thread */
    sink = upipe_wsink_alloc(upipe_wsink_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_main),
                             UPROBE_LOG_VERBOSE, "wsink audio"),
            sink,
            uprobe_pfx_alloc(uprobe_use(uprobe_main),
                             UPROBE_LOG_VERBOSE, "wsink_x audio"),
            SOUND_QUEUE_LENGTH);
    assert(sink != NULL);
    upipe_set_output(avcdec, sink);
    upipe_release(avcdec);
    upipe_release(sink);
    return UBASE_ERR_NONE;
}

/* generic source probe */
static int catch_src(struct uprobe *uprobe, struct upipe *upipe,
                     int event, va_list args)
{
    if (event == UPROBE_SOURCE_END && main_upump_mgr != NULL) {
        upipe_dbg(upipe, "caught source end, dying");
        struct upump *idler_stop = upump_alloc_idler(main_upump_mgr,
                                                     uplay_stop, (void *)0);
        upump_start(idler_stop);
        return UBASE_ERR_NONE;
    }
    return uprobe_throw_next(uprobe, upipe, event, args);
}

static void uplay_start(struct upump *upump)
{
    const char *uri = upump_get_opaque(upump, const char *);
    upump_stop(upump);
    upump_free(upump);

    uprobe_notice(uprobe_main, NULL, "running start idler");
    bool need_trickp = false;
    unsigned int src_out_queue_length = FSRC_OUT_QUEUE_LENGTH;
    uprobe_throw(uprobe_main, NULL, UPROBE_FREEZE_UPUMP_MGR);

    struct uprobe *uprobe_src = uprobe_xfer_alloc(uprobe_use(uprobe_main));
    uprobe_xfer_add(uprobe_src, UPROBE_XFER_VOID, UPROBE_SOURCE_END, 0);

    /* try file source */
    struct upipe_mgr *upipe_fsrc_mgr = upipe_fsrc_mgr_alloc();
    upipe_src = upipe_void_alloc(upipe_fsrc_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_src),
                             UPROBE_LOG_VERBOSE, "fsrc"));
    upipe_mgr_release(upipe_fsrc_mgr);

    if (upipe_src != NULL && ubase_check(upipe_set_uri(upipe_src, uri))) {
        need_trickp = true;
    } else {
        upipe_release(upipe_src);
        uprobe_dejitter_set(uprobe_dejitter, DEJITTER_DIVIDER);
        src_out_queue_length = SRC_OUT_QUEUE_LENGTH;

        /* try rtp source */
        struct upipe_mgr *upipe_rtpsrc_mgr;
        if (!udp)
            upipe_rtpsrc_mgr = upipe_rtpsrc_mgr_alloc();
        else
            upipe_rtpsrc_mgr = upipe_udpsrc_mgr_alloc();
        upipe_src = upipe_void_alloc(upipe_rtpsrc_mgr,
                uprobe_pfx_alloc(uprobe_use(uprobe_src),
                                 UPROBE_LOG_VERBOSE,
                                 udp ? "udpsrc" : "rtpsrc"));
        upipe_mgr_release(upipe_rtpsrc_mgr);

        if (upipe_src != NULL && ubase_check(upipe_set_uri(upipe_src, uri))) {
            upipe_attach_uclock(upipe_src);
        } else {
            upipe_release(upipe_src);

            /* try http source */
            struct upipe_mgr *upipe_http_src_mgr = upipe_http_src_mgr_alloc();
            upipe_src = upipe_void_alloc(upipe_http_src_mgr,
                uprobe_pfx_alloc(uprobe_use(uprobe_src),
                                 UPROBE_LOG_VERBOSE, "httpsrc"));
            upipe_mgr_release(upipe_http_src_mgr);

            if (upipe_src == NULL ||
                !ubase_check(upipe_set_uri(upipe_src, uri))) {
                upipe_release(upipe_src);
                uprobe_err_va(uprobe_main, NULL, "unable to open \"%s\"", uri);
                exit(EXIT_FAILURE);
            }
        }
    }
    uprobe_release(uprobe_src);
    uprobe_throw(uprobe_main, NULL, UPROBE_THAW_UPUMP_MGR);

    if (need_trickp) {
        struct upipe_mgr *upipe_trickp_mgr = upipe_trickp_mgr_alloc();
        trickp = upipe_void_alloc(upipe_trickp_mgr,
                uprobe_pfx_alloc(uprobe_use(uprobe_main),
                                 UPROBE_LOG_VERBOSE, "trickp"));
        assert(trickp != NULL);
        upipe_mgr_release(upipe_trickp_mgr);
        upipe_attach_uclock(trickp);
    }

    struct upipe_mgr *upipe_play_mgr = upipe_play_mgr_alloc();
    play = upipe_void_alloc(upipe_play_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_main),
                             UPROBE_LOG_VERBOSE, "play"));
    assert(play != NULL);
    upipe_mgr_release(upipe_play_mgr);

    /* deport to the source thread */
    upipe_src = upipe_wsrc_alloc(upipe_wsrc_mgr,
            uprobe_pfx_alloc(uprobe_use(&uprobe_src_s),
                             UPROBE_LOG_VERBOSE, "wsrc"),
            upipe_src,
            uprobe_pfx_alloc(uprobe_use(uprobe_main),
                             UPROBE_LOG_VERBOSE, "wsrc_x"),
            src_out_queue_length);

    /* ts demux */
    struct upipe_mgr *upipe_ts_demux_mgr = upipe_ts_demux_mgr_alloc();
    struct upipe_mgr *upipe_mpgvf_mgr = upipe_mpgvf_mgr_alloc();
    upipe_ts_demux_mgr_set_mpgvf_mgr(upipe_ts_demux_mgr, upipe_mpgvf_mgr);
    upipe_mgr_release(upipe_mpgvf_mgr);
    struct upipe_mgr *upipe_h264f_mgr = upipe_h264f_mgr_alloc();
    upipe_ts_demux_mgr_set_h264f_mgr(upipe_ts_demux_mgr, upipe_h264f_mgr);
    upipe_mgr_release(upipe_h264f_mgr);
    struct upipe_mgr *upipe_mpgaf_mgr = upipe_mpgaf_mgr_alloc();
    upipe_ts_demux_mgr_set_mpgaf_mgr(upipe_ts_demux_mgr, upipe_mpgaf_mgr);
    upipe_mgr_release(upipe_mpgaf_mgr);
    struct upipe_mgr *upipe_a52f_mgr = upipe_a52f_mgr_alloc();
    upipe_ts_demux_mgr_set_a52f_mgr(upipe_ts_demux_mgr, upipe_a52f_mgr);
    upipe_mgr_release(upipe_a52f_mgr);
    struct upipe *ts_demux = upipe_void_alloc_output(upipe_src,
            upipe_ts_demux_mgr,
            uprobe_pfx_alloc(
                uprobe_selflow_alloc(uprobe_use(uprobe_main),
                    uprobe_selflow_alloc(
                        uprobe_selflow_alloc(uprobe_use(uprobe_dejitter),
                            uprobe_use(&uprobe_video_s),
                            UPROBE_SELFLOW_PIC, select_video),
                        uprobe_use(&uprobe_audio_s),
                        UPROBE_SELFLOW_SOUND, select_audio),
                    UPROBE_SELFLOW_VOID, select_program),
                UPROBE_LOG_VERBOSE, "ts demux"));
    upipe_release(ts_demux);
    upipe_mgr_release(upipe_ts_demux_mgr);
}

static void uplay_stop(struct upump *upump)
{
    void *force_quit = upump_get_opaque(upump, void *);
    upump_stop(upump);
    upump_free(upump);

    uprobe_notice(uprobe_main, NULL, "running stop idler");
    if (force_quit && upipe_src != NULL) {
        struct upipe_mgr *upipe_null_mgr = upipe_null_mgr_alloc();
        struct upipe *null = upipe_void_alloc(upipe_null_mgr,
                uprobe_pfx_alloc(uprobe_use(uprobe_main),
                                 UPROBE_LOG_VERBOSE, "null"));
        upipe_mgr_release(upipe_null_mgr);
        upipe_set_output(upipe_src, null);
        upipe_release(null);
    }
    upipe_release(upipe_src);
    upipe_src = NULL;
    upipe_mgr_release(upipe_wsrc_mgr);
    upipe_wsrc_mgr = NULL;
    upipe_mgr_release(upipe_wlin_mgr);
    upipe_wlin_mgr = NULL;
    upipe_mgr_release(upipe_wsink_mgr);
    upipe_wsink_mgr = NULL;
    upipe_release(trickp);
    trickp = NULL;
    upipe_release(play);
    play = NULL;
    uprobe_release(uprobe_main);
    uprobe_main = NULL;
    uprobe_release(uprobe_dejitter);
    uprobe_dejitter = NULL;
    upump_mgr_release(main_upump_mgr);
    main_upump_mgr = NULL;
}

static struct upump_mgr *upump_mgr_alloc(void)
{
    struct ev_loop *loop = ev_loop_new(0);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop, UPUMP_POOL,
                                                     UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);
    upump_mgr_set_opaque(upump_mgr, loop);
    return upump_mgr;
}

static void upump_mgr_work(struct upump_mgr *upump_mgr)
{
    struct ev_loop *loop = upump_mgr_get_opaque(upump_mgr, struct ev_loop *);
    ev_loop(loop, 0);
}

static void upump_mgr_free(struct upump_mgr *upump_mgr)
{
    struct ev_loop *loop = upump_mgr_get_opaque(upump_mgr, struct ev_loop *);
    ev_loop_destroy(loop);
}

static void usage(const char *argv0) {
    fprintf(stderr, "Usage: %s [-d] [-q] [-u] [-A <audio>] [-V <video>] [-P <program>] <source>\n", argv0);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    enum uprobe_log_level loglevel = UPROBE_LOG_LEVEL;
    int opt;
    while ((opt = getopt(argc, argv, "udqA:V:P:")) != -1) {
        switch (opt) {
            case 'u':
                udp = true;
                break;
            case 'd':
                loglevel--;
                break;
            case 'q':
                loglevel++;
                break;
            case 'A':
                select_audio = optarg;
                break;
            case 'V':
                select_video = optarg;
                break;
            case 'P':
                select_program = optarg;
                break;
            default:
                usage(argv[0]);
                break;
        }
    }
    if (optind >= argc)
        usage(argv[0]);

    const char *uri = argv[optind++];

    /* structures managers */
    struct ev_loop *loop = ev_default_loop(0);
    main_upump_mgr = upump_ev_mgr_alloc(loop, UPUMP_POOL, UPUMP_BLOCKER_POOL);
    assert(main_upump_mgr != NULL);
    struct umem_mgr *umem_mgr = umem_pool_mgr_alloc_simple(UMEM_POOL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    udict_mgr_release(udict_mgr);
    struct uclock *uclock = uclock_std_alloc(0);

    /* probes */
    uprobe_main = uprobe_stdio_alloc(NULL, stderr, loglevel);
    assert(uprobe_main != NULL);
    uprobe_main = uprobe_uref_mgr_alloc(uprobe_main, uref_mgr);
    assert(uprobe_main != NULL);
    uprobe_main = uprobe_uclock_alloc(uprobe_main, uclock);
    assert(uprobe_main != NULL);
    uprobe_main = uprobe_ubuf_mem_pool_alloc(uprobe_main, umem_mgr,
            UBUF_POOL_DEPTH, UBUF_SHARED_POOL_DEPTH);
    assert(uprobe_main != NULL);
    uprobe_main = uprobe_pthread_upump_mgr_alloc(uprobe_main);
    assert(uprobe_main != NULL);
    uref_mgr_release(uref_mgr);
    uclock_release(uclock);
    umem_mgr_release(umem_mgr);
    uprobe_pthread_upump_mgr_set(uprobe_main, main_upump_mgr);

    uprobe_dejitter = uprobe_dejitter_alloc(uprobe_use(uprobe_main), 0);
    assert(uprobe_dejitter != NULL);
    uprobe_init(&uprobe_src_s, catch_src, uprobe_use(uprobe_main));
    uprobe_init(&uprobe_video_s, catch_video, uprobe_use(uprobe_dejitter));
    uprobe_init(&uprobe_audio_s, catch_audio, uprobe_use(uprobe_dejitter));
    uprobe_init(&uprobe_glx_s, catch_glx, uprobe_use(uprobe_main));

    /* upipe-av */
    if (unlikely(!upipe_av_init(false,
                                uprobe_pfx_alloc(uprobe_use(uprobe_main),
                                                 UPROBE_LOG_VERBOSE, "av")))) {
        uprobe_err_va(uprobe_main, NULL, "unable to init av");
        exit(EXIT_FAILURE);
    }

    /* worker threads */
    struct upipe_mgr *src_xfer_mgr = upipe_pthread_xfer_mgr_alloc(XFER_QUEUE,
            XFER_POOL, uprobe_use(uprobe_main), upump_mgr_alloc,
            upump_mgr_work, upump_mgr_free, NULL);
    assert(src_xfer_mgr != NULL);
    upipe_wsrc_mgr = upipe_wsrc_mgr_alloc(src_xfer_mgr);
    assert(upipe_wsrc_mgr != NULL);
    upipe_mgr_release(src_xfer_mgr);

    struct upipe_mgr *dec_xfer_mgr = upipe_pthread_xfer_mgr_alloc(XFER_QUEUE,
            XFER_POOL, uprobe_use(uprobe_main), upump_mgr_alloc,
            upump_mgr_work, upump_mgr_free, NULL);
    assert(dec_xfer_mgr != NULL);
    upipe_wlin_mgr = upipe_wlin_mgr_alloc(dec_xfer_mgr);
    assert(upipe_wlin_mgr != NULL);
    upipe_mgr_release(dec_xfer_mgr);

    struct upipe_mgr *sink_xfer_mgr = upipe_pthread_xfer_mgr_alloc(XFER_QUEUE,
            XFER_POOL, uprobe_use(uprobe_main), upump_mgr_alloc,
            upump_mgr_work, upump_mgr_free, NULL);
    assert(sink_xfer_mgr != NULL);
    upipe_wsink_mgr = upipe_wsink_mgr_alloc(sink_xfer_mgr);
    assert(upipe_wsink_mgr != NULL);
    upipe_mgr_release(sink_xfer_mgr);

    /* start */
    struct upump *idler_start = upump_alloc_idler(main_upump_mgr, uplay_start,
                                                  (void *)uri);
    upump_start(idler_start);

    /* main loop */
    ev_loop(loop, 0);

    uprobe_clean(&uprobe_src_s);
    uprobe_clean(&uprobe_video_s);
    uprobe_clean(&uprobe_audio_s);
    uprobe_clean(&uprobe_glx_s);

    upipe_av_clean();

    ev_default_destroy();
    return 0;
}

