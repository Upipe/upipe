/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 * Copyright (c) 2016-2018 OpenHeadend S.A.R.L.
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

#undef NDEBUG

#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <libgen.h>
#include <ctype.h>
#include <syslog.h>

#include <upipe/ulist.h>
#include <upipe/uuri.h>
#include <upipe/upipe.h>
#include <upipe/upipe_dump.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_helper_uprobe.h>
#include <upipe/uprobe_helper_urefcount.h>
#include <upipe/uprobe_helper_alloc.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_syslog.h>
#include <upipe/uprobe_loglevel.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_select_flows.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/uprobe_uclock.h>
#include <upipe/uprobe_source_mgr.h>
#include <upipe/uprobe_dejitter.h>
#include <upipe/uclock.h>
#include <upipe/uclock_std.h>
#include <upipe/umem.h>
#include <upipe/umem_pool.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_uri.h>
#include <upipe/uref_m3u.h>
#include <upipe/uref_m3u_master.h>
#include <upipe/uref_dump.h>
#include <upipe/uref_pic.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe-modules/uprobe_http_redirect.h>
#include <upipe-modules/upipe_null.h>
#include <upipe-modules/upipe_auto_source.h>
#include <upipe-modules/upipe_file_source.h>
#include <upipe-modules/upipe_http_source.h>
#include <upipe-modules/upipe_trickplay.h>
#include <upipe-modules/upipe_rtp_prepend.h>
#include <upipe-modules/upipe_file_sink.h>
#include <upipe-modules/upipe_udp_sink.h>
#include <upipe-modules/upipe_dump.h>
#include <upipe-modules/upipe_rtp_h264.h>
#include <upipe-modules/upipe_rtp_mpeg4.h>
#include <upipe-modules/upipe_probe_uref.h>
#include <upipe-modules/upipe_setflowdef.h>
#include <upipe-modules/upipe_time_limit.h>
#include <upipe-modules/upipe_worker_sink.h>
#include <upipe-modules/upipe_dejitter.h>
#include <upipe-modules/upipe_delay.h>
#include <upipe-hls/upipe_hls.h>
#include <upipe-hls/upipe_hls_master.h>
#include <upipe-hls/upipe_hls_variant.h>
#include <upipe-hls/upipe_hls_playlist.h>
#include <upipe-hls/uref_hls.h>
#include <upipe-framers/upipe_h264_framer.h>
#include <upipe-framers/upipe_mpga_framer.h>
#include <upipe-framers/upipe_mpgv_framer.h>
#include <upipe-framers/upipe_a52_framer.h>
#include <upipe-ts/upipe_ts_mux.h>
#include <upipe-pthread/uprobe_pthread_upump_mgr.h>
#include <upipe-pthread/upipe_pthread_transfer.h>
#include <upipe-pthread/umutex_pthread.h>

#include <pthread.h>

struct output {
    uint16_t port;
    uint32_t rtp_type;
    bool enabled;
    struct upipe *pipe;
    struct upipe *sink;
};

#define UMEM_POOL                       512
#define UREF_POOL_DEPTH                 500
#define UBUF_POOL_DEPTH                 3000
#define UBUF_SHARED_POOL_DEPTH          50
#define UDICT_POOL_DEPTH                500
#define UPUMP_POOL                      10
#define UPUMP_BLOCKER_POOL              10
#define WSINK_QUEUE_LENGTH              255
#define XFER_QUEUE                      255
#define XFER_POOL                       20
#define QUEUE_LENGTH                    255
#define PADDING_OCTETRATE               128000
#define TS_PAYLOAD_SIZE                 1316
#define MAX_GAP                         (UCLOCK_FREQ)
#define DEFAULT_TIME_LIMIT              (UCLOCK_FREQ * 10)

/** 2^33 (max resolution of PCR, PTS and DTS) */
#define POW2_33 UINT64_C(8589934592)
/** max resolution of PCR, PTS and DTS */
#define TS_CLOCK_MAX (POW2_33 * UCLOCK_FREQ / 90000)

static int log_level = UPROBE_LOG_NOTICE;
static uint64_t variant_id = UINT64_MAX;
static uint64_t bandwidth_max = UINT64_MAX;
static const char *url = NULL;
static const char *addr = "127.0.0.1";
static const char *dump = NULL;
static struct output video_output = {
    .port = 5004,
    .rtp_type = 96,
    .enabled = true,
    .pipe = NULL,
    .sink = NULL,
};
static struct output audio_output = {
    .port = 5006,
    .rtp_type = 97,
    .enabled = true,
    .pipe = NULL,
    .sink = NULL,
};
static bool rewrite_date = false;
static int64_t timestamp_offset = 0;
static uint64_t last_cr = TS_CLOCK_MAX;
static uint64_t timestamp_highest = TS_CLOCK_MAX;
static uint64_t seek = 0;
static uint64_t sequence = 0;
static uint64_t delay = 0;
static uint64_t mux_max_delay = UINT64_MAX;
static uint64_t min_deviation = UINT64_MAX;
static struct upipe *src = NULL;
static struct upipe *hls = NULL;
static struct upipe *variant = NULL;
static struct upipe *ts_mux = NULL;
static struct upipe *dejitter = NULL;
static struct upipe_mgr *probe_uref_mgr = NULL;
static struct upipe_mgr *time_limit_mgr = NULL;
static struct upipe_mgr *delay_mgr = NULL;
static struct upipe_mgr *rtp_prepend_mgr = NULL;
static struct upipe_mgr *udpsink_mgr = NULL;
static struct upipe_mgr *setflowdef_mgr = NULL;
static struct uprobe *main_probe = NULL;
static struct uprobe *dejitter_probe = NULL;
static struct uref_mgr *uref_mgr;
static struct ev_signal signal_watcher;
static struct ev_io stdin_watcher;

static struct uprobe *uprobe_rewrite_date_alloc(struct uprobe *next,
                                                bool video);
static struct uprobe *uprobe_variant_alloc(struct uprobe *next,
                                           uint64_t id, uint64_t at);
static struct uprobe *uprobe_audio_alloc(struct uprobe *next);
static struct uprobe *uprobe_video_alloc(struct uprobe *next);
static struct uprobe *uprobe_seek_alloc(struct uprobe *next, uint64_t at);
static struct uprobe *uprobe_playlist_alloc(struct uprobe *next,
                                            uint64_t variant_id,
                                            uint64_t at);

static void cmd_quit(void);

/** @This releases a pipe and sets the pointer to NULL.
 *
 * @param upipe_p pointer to a pipe
 */
static inline void upipe_cleanup(struct upipe **upipe_p)
{
    if (upipe_p) {
        upipe_release(*upipe_p);
        *upipe_p = NULL;
    }
}

static int select_variant(struct uprobe *uprobe)
{
    struct uref *uref_variant = NULL;
    uint64_t bandwidth_variant = 0;
    for (struct uref *uref = NULL;
         ubase_check(upipe_split_iterate(hls, &uref)) && uref;) {
        uint64_t id;
        ubase_assert(uref_flow_get_id(uref, &id));
        uint64_t bandwidth = 0;
        uref_m3u_master_get_bandwidth(uref, &bandwidth);

        if (variant_id == id) {
            uref_variant = uref;
            break;
        }

        if ((!bandwidth && !uref_variant) ||
            (bandwidth <= bandwidth_max && bandwidth > bandwidth_variant)) {
            uref_variant = uref;
            bandwidth_variant = bandwidth;
        }
    }

    if (!uref_variant) {
        uprobe_warn_va(uprobe, NULL, "no variant %"PRIu64, variant_id);
        return UBASE_ERR_INVALID;
    }

    uprobe_notice_va(uprobe, NULL, "selected variant");
    uref_dump(uref_variant, uprobe);
    variant = upipe_flow_alloc_sub(
        hls,
        uprobe_pfx_alloc(
            uprobe_variant_alloc(uprobe_use(main_probe),
                                 variant_id, seek),
            UPROBE_LOG_VERBOSE, "variant"),
        uref_variant);
    seek = 0;
    UBASE_ALLOC_RETURN(variant);
    return UBASE_ERR_NONE;
}

static void cmd_start(void)
{
    upipe_cleanup(&audio_output.pipe);
    upipe_cleanup(&video_output.pipe);
    upipe_cleanup(&variant);
    if (!ubase_check(select_variant(main_probe)))
        cmd_quit();
}

/** @This stops the current variant.  */
static void cmd_stop(void)
{
    upipe_cleanup(&audio_output.pipe);
    upipe_cleanup(&video_output.pipe);
    upipe_cleanup(&variant);
}

/** @This quits the program.
 */
static void cmd_quit(void)
{
#if 0
    /* FIXME this requires being in the sink thread */
    if (ts_mux) {
        struct upipe *superpipe;
        if (ubase_check(upipe_sub_get_super(ts_mux, &superpipe)))
            upipe_ts_mux_freeze_psi(superpipe);
    }
#endif

    if (dump != NULL)
        upipe_dump_open(NULL, NULL, dump, NULL, src, NULL);

    cmd_stop();
    upipe_cleanup(&hls);
    upipe_cleanup(&src);
    upipe_cleanup(&ts_mux);
    upipe_cleanup(&variant);
    upipe_cleanup(&src);
    upipe_cleanup(&video_output.sink);
    upipe_cleanup(&audio_output.sink);
    upipe_cleanup(&dejitter);
}

/** @This handles SIGINT and SIGTERM signal. */
static void sigint_cb(struct upump *upump)
{
    static bool graceful = true;
    if (graceful)
        cmd_quit();
    else
        exit(-1);
    graceful = false;
}

/** @This handles seek.
 *
 * @param seek seek value as a string
 */
static void cmd_seek(const char *seek)
{
}

/** @This handles select command. */
static void cmd_select(const char *id)
{
    variant_id = strtoull(id, NULL, 10);
}

/** @This handles stdin events. */
static void stdin_cb(struct upump *upump)
{
    char cmd_buffer[2048];

    int rsize = read(STDIN_FILENO, cmd_buffer, sizeof (cmd_buffer) - 1);
    if (rsize <= 0)
        return;
    cmd_buffer[rsize] = '\0';

    char *cmd = cmd_buffer;
    while (isspace(*cmd))
        cmd++;

    char *last = cmd + strlen(cmd);
    while (last != cmd && *--last == '\n')
        *last = '\0';

    if (!strcmp("quit", cmd))
        cmd_quit();
    else if (!strcmp("stop", cmd))
        cmd_stop();
    else if (!strcmp("start", cmd))
        cmd_start();
    else if (!strncmp("seek ", cmd, sizeof ("seek ") - 1))
        cmd_seek(cmd + sizeof ("seek ") - 1);
    else if (!strncmp("select ", cmd, sizeof ("select ") - 1))
        cmd_select(cmd + sizeof ("select ") - 1);
    else if (!strlen(cmd))
        return;
    else
        fprintf(stderr, "unknowned command \"%s\"\n", cmd);
}

/** @This is the private context of a seek probe */
struct uprobe_seek {
    struct uprobe probe;
    uint64_t at;
    uint64_t pts;
};

UPROBE_HELPER_UPROBE(uprobe_seek, probe);

/** @This is the private context of a video probe */
struct uprobe_video {
    struct uprobe probe;
    uint64_t at;
};

UPROBE_HELPER_UPROBE(uprobe_video, probe);
UPROBE_HELPER_UREFCOUNT(uprobe_video);

/** @This is the private context of an audio probe */
struct uprobe_audio {
    struct uprobe probe;
};

UPROBE_HELPER_UPROBE(uprobe_audio, probe);
UPROBE_HELPER_UREFCOUNT(uprobe_audio);

/** @This is the private context of a playlist probe */
struct uprobe_playlist {
    struct uprobe probe;
    uint64_t at;
    uint64_t variant_id;
    struct uprobe_video *video;
    struct uprobe_audio *audio;
};

UPROBE_HELPER_UPROBE(uprobe_playlist, probe);
UPROBE_HELPER_UREFCOUNT(uprobe_playlist);

/** @This is the private context of a variant probe  */
struct uprobe_variant {
    struct uprobe probe;
    uint64_t id;
    uint64_t at;
    struct uprobe_playlist *video;
    struct uprobe_playlist *audio;
};

UPROBE_HELPER_UPROBE(uprobe_variant, probe);

/** @This is the private context of a rewrite date probe */
struct uprobe_rewrite_date {
    struct uprobe probe;
    bool video;
};

UPROBE_HELPER_UPROBE(uprobe_rewrite_date, probe);

/** @This is the rewrite date callback */
static int catch_rewrite_date(struct uprobe *uprobe, struct upipe *upipe,
                              int event, va_list args)
{
    struct uprobe_rewrite_date *probe_rewrite_date =
        uprobe_rewrite_date_from_uprobe(uprobe);
    if (event != UPROBE_PROBE_UREF ||
        ubase_get_signature(args) != UPIPE_PROBE_UREF_SIGNATURE)
        return uprobe_throw_next(uprobe, upipe, event, args);

    UBASE_SIGNATURE_CHECK(args, UPIPE_PROBE_UREF_SIGNATURE);
    struct uref *uref = va_arg(args, struct uref *);

    int type;
    uint64_t date;
    uref_clock_get_date_orig(uref, &date, &type);
    if (type == UREF_DATE_NONE)
        return UBASE_ERR_NONE;

    if (probe_rewrite_date->video || !video_output.pipe) {
        uint64_t delta = (TS_CLOCK_MAX + date -
                          (last_cr % TS_CLOCK_MAX)) % TS_CLOCK_MAX;
        if (delta < MAX_GAP)
            last_cr += delta;
        else {
            upipe_dbg_va(upipe, "clock ref discontinuity %"PRIu64, delta);
            last_cr = date;
            timestamp_offset = timestamp_highest - date;
        }
    }

    uint64_t delta = (TS_CLOCK_MAX + date -
                      (last_cr % TS_CLOCK_MAX)) % TS_CLOCK_MAX;
    if (delta > MAX_GAP) {
        /* This should not happen */
        upipe_warn_va(upipe, "timestamp discontinuity %"PRIu64, delta);
        uref_clock_delete_date_prog(uref);
        return UBASE_ERR_NONE;
    }

    upipe_verbose_va(upipe, "rewrite %"PRIu64" -> %"PRIu64, date,
                     timestamp_offset + last_cr + delta);
    date = timestamp_offset + last_cr + delta;
    uref_clock_set_date_prog(uref, date, type);
    if (date > timestamp_highest)
        timestamp_highest = date;

    return UBASE_ERR_NONE;
}

/** @This initializes a rewrite date probe. */
static struct uprobe *
uprobe_rewrite_date_init(struct uprobe_rewrite_date *probe_rewrite_date,
                         struct uprobe *next, bool video)
{
    struct uprobe *probe = uprobe_rewrite_date_to_uprobe(probe_rewrite_date);
    uprobe_init(probe, catch_rewrite_date, next);
    probe_rewrite_date->video = video;
    return probe;
}

/** @This cleans a rewrite date probe */
static void uprobe_rewrite_date_clean(struct uprobe_rewrite_date *probe)
{
    uprobe_clean(uprobe_rewrite_date_to_uprobe(probe));
}

#define ARGS_DECL struct uprobe *next, bool video
#define ARGS next, video
UPROBE_HELPER_ALLOC(uprobe_rewrite_date);
#undef ARGS
#undef ARGS_DECL

/** @This is the seek probe callback */
static int catch_seek(struct uprobe *uprobe, struct upipe *upipe,
                      int event, va_list args)
{
    struct uprobe_seek *probe_seek = uprobe_seek_from_uprobe(uprobe);

    if (event < UPROBE_LOCAL ||
        ubase_get_signature(args) != UPIPE_PROBE_UREF_SIGNATURE)
        return uprobe_throw_next(uprobe, upipe, event, args);

    UBASE_SIGNATURE_CHECK(args, UPIPE_PROBE_UREF_SIGNATURE);
    struct uref *uref = va_arg(args, struct uref *);
    va_arg(args, struct upump **);
    bool *drop = va_arg(args, bool *);
    *drop = false;

    uint64_t pts;
    if (!ubase_check(uref_clock_get_pts_prog(uref, &pts))) {
        uprobe_warn(uprobe, NULL, "no PTS prog, drop...");
        *drop = true;
        return UBASE_ERR_NONE;
    }

    if (probe_seek->at) {
        probe_seek->pts = pts + probe_seek->at;
        uprobe_notice_va(uprobe, NULL,
                         "seek PTS %"PRIu64" (%"PRIu64")",
                         probe_seek->pts, probe_seek->at);
        probe_seek->at = 0;
    }
    if (probe_seek->pts) {
        if (pts < probe_seek->pts || !ubase_check(uref_pic_get_key(uref)))
            *drop = true;
        else
            probe_seek->pts = 0;
    }
    return UBASE_ERR_NONE;
}

static struct uprobe *uprobe_seek_init(struct uprobe_seek *probe_seek,
                                       struct uprobe *next,
                                       uint64_t at)
{
    struct uprobe *probe = uprobe_seek_to_uprobe(probe_seek);
    uprobe_init(probe, catch_seek, next);
    probe_seek->at = at;
    probe_seek->pts = 0;
    return probe;
}

static void uprobe_seek_clean(struct uprobe_seek *probe_seek)
{
    uprobe_clean(uprobe_seek_to_uprobe(probe_seek));
}

#define ARGS_DECL struct uprobe *next, uint64_t at
#define ARGS next, at
UPROBE_HELPER_ALLOC(uprobe_seek);
#undef ARGS
#undef ARGS_DECL

/*
 * catch audio stream
 */
static int catch_audio(struct uprobe *uprobe,
                          struct upipe *upipe,
                          int event, va_list args)
{
    switch (event) {
    case UPROBE_NEED_OUTPUT: {
        struct upipe *output = upipe_use(upipe);
        if (rewrite_date) {
            output = upipe_void_chain_output(
                output, probe_uref_mgr,
                uprobe_pfx_alloc(
                    uprobe_rewrite_date_alloc(uprobe_use(main_probe), false),
                    UPROBE_LOG_VERBOSE, "rewrite sound"));
            assert(output);
        }

        if (dejitter) {
            if (!video_output.pipe) {
                upipe_set_output(output, dejitter);
                upipe_release(output);
                output = upipe_use(dejitter);
            }
            else {
                output = upipe_void_chain_output_sub(
                    output, dejitter,
                    uprobe_pfx_alloc(
                        uprobe_use(dejitter_probe),
                        UPROBE_LOG_VERBOSE, "dejitter sound"));
                assert(output);
            }
        }
        int ret = upipe_set_output(output, audio_output.sink);
        upipe_release(output);
        return ret;
    }
    }
    return uprobe_throw_next(uprobe, upipe, event, args);
}

static struct uprobe *uprobe_audio_init(struct uprobe_audio *probe_audio,
                                        struct uprobe *next)
{
    struct uprobe *probe = uprobe_audio_to_uprobe(probe_audio);
    uprobe_init(probe, catch_audio, next);
    return probe;
}

static void uprobe_audio_clean(struct uprobe_audio *probe_audio)
{
    uprobe_clean(uprobe_audio_to_uprobe(probe_audio));
}

#define ARGS_DECL struct uprobe *next
#define ARGS next
UPROBE_HELPER_ALLOC(uprobe_audio);
#undef ARGS
#undef ARGS_DECL

/*
 * catch video stream
 */
static int catch_video(struct uprobe *uprobe,
                       struct upipe *upipe,
                       int event, va_list args)
{
    struct uprobe_video *probe_video = uprobe_video_from_uprobe(uprobe);

    switch (event) {
    case UPROBE_NEED_OUTPUT: {
        struct upipe *output = upipe_use(upipe);

        if (rewrite_date) {
            output = upipe_void_chain_output(
                output, probe_uref_mgr,
                uprobe_pfx_alloc(
                    uprobe_rewrite_date_alloc(uprobe_use(main_probe), true),
                    UPROBE_LOG_VERBOSE, "rewrite pic"));
            assert(output);
        }

        output = upipe_void_chain_output(
            output, probe_uref_mgr,
            uprobe_pfx_alloc(
                uprobe_seek_alloc(uprobe_use(main_probe), probe_video->at),
                UPROBE_LOG_VERBOSE, "seek"));
        UBASE_ALLOC_RETURN(output);

        if (dejitter) {
            upipe_set_output(output, dejitter);
            upipe_release(output);
            output = upipe_use(dejitter);
        }
        int ret = upipe_set_output(output, video_output.sink);
        upipe_release(output);
        return ret;
    }
    }
    return uprobe_throw_next(uprobe, upipe, event, args);
}

static struct uprobe *uprobe_video_init(struct uprobe_video *probe_video,
                                        struct uprobe *next)
{
    struct uprobe *probe = uprobe_video_to_uprobe(probe_video);
    uprobe_init(probe, catch_video, next);
    probe_video->at = 0;
    return probe;
}

static void uprobe_video_clean(struct uprobe_video *probe_video)
{
    uprobe_clean(uprobe_video_to_uprobe(probe_video));
}

#define ARGS_DECL struct uprobe *next
#define ARGS next
UPROBE_HELPER_ALLOC(uprobe_video);
#undef ARGS
#undef ARGS_DECL

/*
 * playlist events
 */
static int catch_playlist(struct uprobe *uprobe,
                          struct upipe *upipe,
                          int event, va_list args)
{
    struct uprobe_playlist *probe_playlist =
        uprobe_playlist_from_uprobe(uprobe);
    int ret;

    if (event < UPROBE_LOCAL)
        return uprobe_throw_next(uprobe, upipe, event, args);

    if (ubase_get_signature(args) != UPIPE_HLS_PLAYLIST_SIGNATURE)
        return uprobe_throw_next(uprobe, upipe, event, args);

    switch (event) {
    case UPROBE_HLS_PLAYLIST_RELOADED: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_HLS_PLAYLIST_SIGNATURE);
        uprobe_notice(uprobe, NULL, "playlist reloaded");
        uint64_t at = probe_playlist->at;
        if (at) {
            uint64_t remain = 0;
            uprobe_notice_va(uprobe, NULL, "seek at %"PRIu64, at);
            ret = upipe_hls_playlist_seek(upipe, at, &remain);
            if (!ubase_check(ret)) {
                cmd_quit();
                return ret;
            }

            if (probe_playlist->video)
                probe_playlist->video->at = remain;
            probe_playlist->at = 0;
        }
        else if (sequence) {
            ret = upipe_hls_playlist_set_index(upipe, sequence);
            if (!ubase_check(ret))
                cmd_quit();
        }

        ret = upipe_hls_playlist_play(upipe);
        if (!ubase_check(ret))
            cmd_quit();
        return ret;
    }

    case UPROBE_HLS_PLAYLIST_ITEM_END:
        UBASE_SIGNATURE_CHECK(args, UPIPE_HLS_PLAYLIST_SIGNATURE);
        UBASE_RETURN(upipe_hls_playlist_next(upipe));
        UBASE_RETURN(upipe_hls_playlist_get_index(upipe, &sequence));
        if (variant_id != probe_playlist->variant_id) {
            upipe_cleanup(&video_output.pipe);
            upipe_cleanup(&audio_output.pipe);
            upipe_cleanup(&variant);
            ret = select_variant(uprobe);
            if (!ubase_check(ret))
                cmd_quit();
            return ret;
        }

        ret = upipe_hls_playlist_play(upipe);
        if (!ubase_check(ret))
            cmd_quit();
        return ret;
    }
    return uprobe_throw_next(uprobe, upipe, event, args);
}

static struct uprobe *
uprobe_playlist_init(struct uprobe_playlist *probe_playlist,
                     struct uprobe *next,
                     uint64_t variant_id,
                     uint64_t at)
{
    struct uprobe *probe = uprobe_playlist_to_uprobe(probe_playlist);
    uprobe_init(probe, catch_playlist, next);
    probe_playlist->variant_id = variant_id;
    probe_playlist->at = at;
    probe_playlist->video = NULL;
    probe_playlist->audio = NULL;
    return probe;
}

static void uprobe_playlist_set_video(struct uprobe_playlist *probe,
                                      struct uprobe_video *video)
{
    uprobe_video_release(probe->video);
    probe->video = uprobe_video_use(video);
}

static void uprobe_playlist_set_audio(struct uprobe_playlist *probe,
                                      struct uprobe_audio *audio)
{
    uprobe_audio_release(probe->audio);
    probe->audio = uprobe_audio_use(audio);
}

static void uprobe_playlist_clean(struct uprobe_playlist *probe_playlist)
{
    uprobe_playlist_set_video(probe_playlist, NULL);
    uprobe_playlist_set_audio(probe_playlist, NULL);
    uprobe_clean(uprobe_playlist_to_uprobe(probe_playlist));
}

#define ARGS_DECL struct uprobe *next, uint64_t variant_id, uint64_t at
#define ARGS next, variant_id, at
UPROBE_HELPER_ALLOC(uprobe_playlist);
#undef ARGS
#undef ARGS_DECL

/*
 * variant events
 */
static void uprobe_variant_set_video(struct uprobe_variant *variant,
                                     struct uprobe_playlist *video)
{
    uprobe_playlist_release(variant->video);
    variant->video = uprobe_playlist_use(video);
}

static void uprobe_variant_set_audio(struct uprobe_variant *variant,
                                     struct uprobe_playlist *audio)
{
    uprobe_playlist_release(variant->audio);
    variant->audio = uprobe_playlist_use(audio);
}

static int catch_variant(struct uprobe *uprobe,
                         struct upipe *upipe,
                         int event, va_list args)
{
    struct uprobe_variant *probe_variant =
        uprobe_variant_from_uprobe(uprobe);

    switch (event) {
    case UPROBE_SPLIT_UPDATE: {
        struct uref *uref_video = NULL;
        struct uref *uref_audio = NULL;

        /* find an audio and a video item */
        uprobe_notice_va(uprobe, NULL, "variant list:");
        for (struct uref *uref = NULL;
             ubase_check(upipe_split_iterate(upipe, &uref)) && uref;) {
            uint64_t id;
            ubase_assert(uref_flow_get_id(uref, &id));
            const char *def;
            ubase_assert(uref_flow_get_def(uref, &def));

            uprobe_notice_va(uprobe, NULL, "%"PRIu64" - %s", id, def);
            uref_dump(uref, uprobe);

            if (ubase_check(uref_flow_match_def(uref, "void."))) {
                if (uref_video == NULL)
                    uref_video = uref;
                if (uref_audio == NULL)
                    uref_audio = uref;
            }
            else if (ubase_check(uref_flow_match_def(uref, "sound."))) {
                if (ubase_check(uref_hls_get_default(uref)) || !uref_audio)
                    uref_audio = uref;
            }
            else if (ubase_check(uref_flow_match_def(uref, "pic."))) {
                if (ubase_check(uref_hls_get_default(uref)) || !uref_video)
                    uref_video = uref;
            }
            else {
                uprobe_warn_va(uprobe, NULL, "unhandled flow %s", def);
            }
        }

        if (!audio_output.enabled)
            uref_audio = NULL;

        if (!video_output.enabled)
            uref_video = NULL;

        uint64_t audio_id, video_id;
        if (uref_audio && uref_video &&
            ubase_check(uref_flow_get_id(uref_audio, &audio_id)) &&
            ubase_check(uref_flow_get_id(uref_video, &video_id)) &&
            audio_id == video_id) {

            struct uprobe *probe_video =
                uprobe_video_alloc(uprobe_use(main_probe));

            struct uprobe *probe_audio =
                uprobe_audio_alloc(uprobe_use(main_probe));

            struct uprobe *probe_playlist =
                uprobe_playlist_alloc(
                    uprobe_selflow_alloc(
                        uprobe_selflow_alloc(
                            uprobe_use(main_probe),
                            uprobe_pfx_alloc(probe_audio,
                                             UPROBE_LOG_VERBOSE, "sound"),
                            UPROBE_SELFLOW_SOUND, "auto"),
                        uprobe_pfx_alloc(probe_video,
                                         UPROBE_LOG_VERBOSE, "pic"),
                        UPROBE_SELFLOW_PIC, "auto"),
                    probe_variant->id,
                    probe_variant->at);

            uprobe_playlist_set_video(
                uprobe_playlist_from_uprobe(probe_playlist),
                uprobe_video_from_uprobe(probe_video));
            uprobe_playlist_set_audio(
                uprobe_playlist_from_uprobe(probe_playlist),
                uprobe_audio_from_uprobe(probe_audio));

            uprobe_variant_set_video(
                probe_variant, uprobe_playlist_from_uprobe(probe_playlist));
            uprobe_variant_set_audio(
                probe_variant, uprobe_playlist_from_uprobe(probe_playlist));

            video_output.pipe = upipe_flow_alloc_sub(
                upipe,
                uprobe_pfx_alloc(probe_playlist, UPROBE_LOG_VERBOSE, "mixed"),
                uref_audio);
            audio_output.pipe = upipe_use(video_output.pipe);
            upipe_attach_uclock(video_output.pipe);
        }
        else {
            if (uref_audio) {
                struct uprobe *probe_audio =
                    uprobe_audio_alloc(uprobe_use(main_probe));

                struct uprobe *probe_playlist =
                    uprobe_playlist_alloc(
                        probe_audio,
                        probe_variant->id,
                        probe_variant->at);

                uprobe_playlist_set_audio(
                    uprobe_playlist_from_uprobe(probe_playlist),
                    uprobe_audio_from_uprobe(probe_audio));

                uprobe_variant_set_audio(
                    probe_variant, uprobe_playlist_from_uprobe(probe_playlist));

                audio_output.pipe = upipe_flow_alloc_sub(
                    upipe,
                    uprobe_pfx_alloc_va(
                        probe_playlist,
                        UPROBE_LOG_VERBOSE, "audio %"PRIu64,
                        probe_variant->id),
                    uref_audio);
            }
            else
                uprobe_warn(uprobe, NULL, "no audio");

            if (uref_video) {
                struct uprobe *probe_video =
                    uprobe_video_alloc(uprobe_use(main_probe));

                struct uprobe *probe_playlist =
                    uprobe_playlist_alloc(
                        uprobe_selflow_alloc(
                            uprobe_use(main_probe),
                            probe_video,
                            UPROBE_SELFLOW_PIC, "auto"),
                        probe_variant->id,
                        probe_variant->at);

                uprobe_playlist_set_video(
                    uprobe_playlist_from_uprobe(probe_playlist),
                    uprobe_video_from_uprobe(probe_video));

                uprobe_variant_set_video(
                    probe_variant, uprobe_playlist_from_uprobe(probe_playlist));

                video_output.pipe = upipe_flow_alloc_sub(
                    upipe,
                    uprobe_pfx_alloc_va(
                        probe_playlist,
                        UPROBE_LOG_VERBOSE, "video %"PRIu64,
                        probe_variant->id),
                    uref_video);
            }
            else
                uprobe_warn(uprobe, NULL, "no video");
        }

        if (!video_output.pipe && !audio_output.pipe)
            cmd_quit();

        return UBASE_ERR_NONE;
    }
    }

    return uprobe_throw_next(uprobe, upipe, event, args);
}

static struct uprobe *uprobe_variant_init(struct uprobe_variant *probe_variant,
                                          struct uprobe *next,
                                          uint64_t id, uint64_t at)
{
    struct uprobe *probe = uprobe_variant_to_uprobe(probe_variant);
    uprobe_init(probe, catch_variant, next);
    probe_variant->id = id;
    probe_variant->at = at;
    probe_variant->video = NULL;
    probe_variant->audio = NULL;
    return probe;
}

static void uprobe_variant_clean(struct uprobe_variant *probe_variant)
{
    uprobe_variant_set_video(probe_variant, NULL);
    uprobe_variant_set_audio(probe_variant, NULL);
    uprobe_clean(uprobe_variant_to_uprobe(probe_variant));
}

#define ARGS_DECL struct uprobe *next, uint64_t id, uint64_t at
#define ARGS next, id, at
UPROBE_HELPER_ALLOC(uprobe_variant);
#undef ARGS
#undef ARGS_DECL

static int catch_hls(struct uprobe *uprobe,
                     struct upipe *upipe,
                     int event, va_list args)
{
    int ret;

    switch (event) {
    case UPROBE_SPLIT_UPDATE: {
        uprobe_notice_va(uprobe, NULL, "list:");

        for (struct uref *uref = NULL;
             ubase_check(upipe_split_iterate(upipe, &uref)) && uref;) {
            uint64_t id;
            ubase_assert(uref_flow_get_id(uref, &id));
            const char *uri = "(none)";
            uref_m3u_get_uri(uref, &uri);

            uprobe_notice_va(uprobe, NULL, "%"PRIu64" - %s", id, uri);
            uref_dump(uref, uprobe);
        }

        ret = select_variant(uprobe);
        if (!ubase_check(ret))
            cmd_quit();
        return ret;
    }
    }

    return uprobe_throw_next(uprobe, upipe, event, args);
}

static int catch_src(struct uprobe *uprobe, struct upipe *upipe,
                     int event, va_list args)
{
        if (event != UPROBE_HTTP_SRC_ERROR ||
            ubase_get_signature(args) != UPIPE_HTTP_SRC_SIGNATURE)
                return uprobe_throw_next(uprobe, upipe, event, args);

        UBASE_SIGNATURE_CHECK(args, UPIPE_HTTP_SRC_SIGNATURE);
        unsigned int code = va_arg(args, unsigned int);

        uprobe_err_va(uprobe, NULL, "http error %u", code);
        cmd_quit();
        return UBASE_ERR_NONE;
}

static struct upipe *hls2rtp_video_sink(struct uprobe *probe,
                                        struct upipe *trickp,
                                        uint64_t time_limit,
                                        struct upipe_mgr *wsink_mgr)
{
    struct upipe *sink = NULL;

    if (trickp) {
        sink = upipe_void_alloc_sub(
            trickp,
            uprobe_pfx_alloc(uprobe_use(probe),
                             UPROBE_LOG_VERBOSE, "trickp pic"));
        assert(sink);
    }

    struct upipe *output = upipe_void_alloc(
        delay_mgr,
        uprobe_pfx_alloc(uprobe_use(probe),
                         UPROBE_LOG_VERBOSE, "delay"));
    assert(output);
    upipe_delay_set_delay(output, delay);
    if (sink)
        upipe_set_output(sink, output);
    else
        sink = upipe_use(output);

    output = upipe_void_chain_output(
        output, time_limit_mgr,
        uprobe_pfx_alloc(uprobe_use(probe),
                         UPROBE_LOG_VERBOSE, "time_limit"));
    assert(output);
    upipe_time_limit_set_limit(output, time_limit);

    if (!ts_mux) {
        uint16_t port = video_output.port;
        int ret = snprintf(NULL, 0, "%s:%u", addr, port);
        assert(ret > 0);
        char uri[ret + 1];
        assert(snprintf(uri, sizeof (uri), "%s:%u", addr, port) > 0);

        struct upipe_mgr *upipe_rtp_h264_mgr = upipe_rtp_h264_mgr_alloc();
        assert(upipe_rtp_h264_mgr);
        output = upipe_void_chain_output(
            output, upipe_rtp_h264_mgr,
            uprobe_pfx_alloc(uprobe_use(probe),
                             UPROBE_LOG_VERBOSE, "rtp h264"));
        assert(output);
        upipe_mgr_release(upipe_rtp_h264_mgr);

        output = upipe_void_chain_output(
            output, rtp_prepend_mgr,
            uprobe_pfx_alloc(uprobe_use(probe),
                             UPROBE_LOG_VERBOSE, "rtp pic"));
        assert(output);
        ubase_assert(upipe_rtp_prepend_set_type(output, video_output.rtp_type));

        uprobe_throw(main_probe, NULL, UPROBE_FREEZE_UPUMP_MGR);
        struct upipe *udpsink = upipe_void_alloc(udpsink_mgr,
            uprobe_pfx_alloc(uprobe_use(probe),
                             UPROBE_LOG_VERBOSE, "udp pic"));
        assert(udpsink);
        ubase_assert(upipe_attach_uclock(udpsink));
        ubase_assert(upipe_set_uri(udpsink, uri));
        uprobe_throw(main_probe, NULL, UPROBE_THAW_UPUMP_MGR);

        output = upipe_wsink_chain_output(output, wsink_mgr,
                uprobe_pfx_alloc(
                    uprobe_use(main_probe),
                    UPROBE_LOG_VERBOSE, "wsink pic"),
                udpsink,
                uprobe_pfx_alloc(uprobe_use(main_probe),
                                 UPROBE_LOG_VERBOSE, "wsink_x pic"),
                QUEUE_LENGTH);
        assert(output);

    } else { /* ts_mux */
        output = upipe_void_chain_output(output, setflowdef_mgr,
            uprobe_pfx_alloc(uprobe_use(probe),
                             UPROBE_LOG_VERBOSE, "setflowdef pic"));
        assert(output);

        struct uref *uref = uref_alloc_control(uref_mgr);
        assert(uref != NULL);
        uref_ts_flow_set_pid(uref, 257);
        upipe_setflowdef_set_dict(output, uref);
        uref_free(uref);

        struct upipe *mux_input = upipe_void_alloc_sub(ts_mux,
            uprobe_pfx_alloc(uprobe_use(probe),
                             UPROBE_LOG_VERBOSE, "mux_input pic"));
        assert(mux_input);

        output = upipe_wsink_chain_output(output, wsink_mgr,
                uprobe_pfx_alloc(
                    uprobe_use(main_probe),
                    UPROBE_LOG_VERBOSE, "wsink pic"),
                mux_input,
                uprobe_pfx_alloc(uprobe_use(main_probe),
                                 UPROBE_LOG_VERBOSE, "wsink_x pic"),
                QUEUE_LENGTH);
        assert(output);
    }

    upipe_release(output);
    return sink;
}

static struct upipe *hls2rtp_audio_sink(struct uprobe *probe,
                                        struct upipe *trickp,
                                        uint64_t time_limit,
                                        struct upipe_mgr *wsink_mgr)
{
    struct upipe *sink = NULL;

    if (trickp) {
        sink = upipe_void_alloc_sub(
            trickp,
            uprobe_pfx_alloc(uprobe_use(probe),
                             UPROBE_LOG_VERBOSE, "trickp sound"));
        assert(sink);
    }

    struct upipe *output = upipe_void_alloc(
        delay_mgr,
        uprobe_pfx_alloc(uprobe_use(probe),
                         UPROBE_LOG_VERBOSE, "delay"));
    assert(output);
    upipe_delay_set_delay(output, delay);
    if (sink)
        upipe_set_output(sink, output);
    else
        sink = upipe_use(output);

    output = upipe_void_chain_output(
        output, time_limit_mgr,
        uprobe_pfx_alloc(uprobe_use(probe),
                         UPROBE_LOG_VERBOSE, "time_limit"));
    assert(output);
    upipe_time_limit_set_limit(output, time_limit);

    if (!ts_mux) {
        uint16_t port = audio_output.port;
        int ret = snprintf(NULL, 0, "%s:%u", addr, port);
        assert(ret > 0);
        char uri[ret + 1];
        assert(snprintf(uri, sizeof (uri), "%s:%u", addr, port) > 0);

        struct upipe_mgr *upipe_rtp_mpeg4_mgr = upipe_rtp_mpeg4_mgr_alloc();
        assert(upipe_rtp_mpeg4_mgr);
        output = upipe_void_chain_output(
            output, upipe_rtp_mpeg4_mgr,
            uprobe_pfx_alloc(uprobe_use(probe),
                             UPROBE_LOG_VERBOSE, "rtp aac"));
        upipe_mgr_release(upipe_rtp_mpeg4_mgr);
        assert(output);

        output = upipe_void_chain_output(
            output, rtp_prepend_mgr,
            uprobe_pfx_alloc(uprobe_use(probe),
                             UPROBE_LOG_VERBOSE, "rtp sound"));
        assert(output);
        ubase_assert(upipe_rtp_prepend_set_type(output, audio_output.rtp_type));

        uprobe_throw(main_probe, NULL, UPROBE_FREEZE_UPUMP_MGR);
        struct upipe *udpsink = upipe_void_alloc(udpsink_mgr,
            uprobe_pfx_alloc(uprobe_use(probe),
                             UPROBE_LOG_VERBOSE, "udp sound"));
        assert(udpsink);
        ubase_assert(upipe_attach_uclock(udpsink));
        ubase_assert(upipe_set_uri(udpsink, uri));
        uprobe_throw(main_probe, NULL, UPROBE_THAW_UPUMP_MGR);

        output = upipe_wsink_chain_output(output, wsink_mgr,
                uprobe_pfx_alloc(uprobe_use(main_probe),
                                 UPROBE_LOG_VERBOSE, "wsink sound"),
                udpsink,
                uprobe_pfx_alloc(uprobe_use(main_probe),
                                 UPROBE_LOG_VERBOSE, "wsink_x sound"),
                QUEUE_LENGTH);
        assert(output);

    } else {
        output = upipe_void_chain_output(output, setflowdef_mgr,
            uprobe_pfx_alloc(uprobe_use(probe),
                             UPROBE_LOG_VERBOSE, "setflowdef sound"));
        assert(output);

        struct uref *uref = uref_alloc_control(uref_mgr);
        assert(uref != NULL);
        uref_ts_flow_set_pid(uref, 258);
        upipe_setflowdef_set_dict(output, uref);
        uref_free(uref);

        struct upipe *mux_input = upipe_void_alloc_sub(ts_mux,
            uprobe_pfx_alloc(uprobe_use(probe),
                             UPROBE_LOG_VERBOSE, "mux_input sound"));
        assert(mux_input);

        output = upipe_wsink_chain_output(output, wsink_mgr,
                uprobe_pfx_alloc(
                    uprobe_use(main_probe),
                    UPROBE_LOG_VERBOSE, "wsink"),
                mux_input,
                uprobe_pfx_alloc(uprobe_use(main_probe),
                                 UPROBE_LOG_VERBOSE, "wsink_x sound"),
                QUEUE_LENGTH);
        assert(output);
    }

    upipe_release(output);
    return sink;
}

static int catch_error(struct uprobe *uprobe,
                     struct upipe *upipe,
                     int event, va_list args)
{
    switch (event) {
    case UPROBE_FATAL: {
        int code = va_arg(args, int);
        exit(code);
        break;
    }

    case UPROBE_ERROR:
        cmd_quit();
        return UBASE_ERR_NONE;
    }

    return uprobe_throw_next(uprobe, upipe, event, args);
}

enum opt {
    OPT_INVALID = '?',
    OPT_MISSING_ARG = ':',

    OPT_VERBOSE = 'v',
    OPT_UDP = 'U',
    OPT_MTU = 'M',
    OPT_CONFORMANCE = 'K',

    OPT_ID      = 0x100,
    OPT_ADDR,
    OPT_TS,
    OPT_VIDEO_PORT,
    OPT_AUDIO_PORT,
    OPT_NO_AUDIO,
    OPT_NO_VIDEO,
    OPT_NO_COLOR,
    OPT_REWRITE_DATE,
    OPT_SEEK,
    OPT_SEQUENCE,
    OPT_BANDWIDTH,
    OPT_TIME_LIMIT,
    OPT_RT_PRIORITY,
    OPT_SYSLOG_TAG,
    OPT_NO_STDIN,
    OPT_DUMP,
    OPT_HELP,
    OPT_MUX_MAX_DELAY,
    OPT_MIN_DEVIATION,
    OPT_DELAY,
};

static struct option options[] = {
    { "id", required_argument, NULL, OPT_ID },
    { "addr", required_argument, NULL, OPT_ADDR },
    { "ts", no_argument, NULL, OPT_TS },
    { "video-port", required_argument, NULL, OPT_VIDEO_PORT },
    { "audio-port", required_argument, NULL, OPT_AUDIO_PORT },
    { "no-video", no_argument, NULL, OPT_NO_VIDEO },
    { "no-audio", no_argument, NULL, OPT_NO_AUDIO },
    { "no-color", no_argument, NULL, OPT_NO_COLOR },
    { "rewrite-date", no_argument, NULL, OPT_REWRITE_DATE },
    { "verbose", no_argument, NULL, OPT_VERBOSE },
    { "seek", required_argument, NULL, OPT_SEEK },
    { "sequence", required_argument, NULL, OPT_SEQUENCE },
    { "bandwidth", required_argument, NULL, OPT_BANDWIDTH },
    { "time-limit", required_argument, NULL, OPT_TIME_LIMIT },
    { "rt-priority", required_argument, NULL, OPT_RT_PRIORITY },
    { "syslog-tag", required_argument, NULL, OPT_SYSLOG_TAG },
    { "mtu", required_argument, NULL, OPT_MTU },
    { "udp", no_argument, NULL, OPT_UDP },
    { "conformance", required_argument, NULL, OPT_CONFORMANCE },
    { "no-stdin", no_argument, NULL, OPT_NO_STDIN },
    { "dump", required_argument, NULL, OPT_DUMP },
    { "help", no_argument, NULL, OPT_HELP },
    { "mux-max-delay", required_argument, NULL, OPT_MUX_MAX_DELAY },
    { "min-deviation", required_argument, NULL, OPT_MIN_DEVIATION },
    { "delay", required_argument, NULL, OPT_DELAY },
    { 0, 0, 0, 0 },
};

static void usage(const char *name, const char *err, ...)
{
    if (err) {
        va_list ap;
        va_start(ap, err);
        vfprintf(stderr, err, ap);
        va_end(ap);
    }
    fprintf(stderr, "%s <url>\n", name);
    fprintf(stderr, "options:\n");
    for (size_t i = 0; options[i].name; i++)
        fprintf(stderr, "\t--%s%s%s%s\n",
                options[i].name,
                options[i].has_arg == optional_argument ? " [" :
                options[i].has_arg == required_argument ? " <" : "",
                options[i].has_arg != no_argument ? "arg" : "",
                options[i].has_arg == optional_argument ? "]" :
                options[i].has_arg == required_argument ? ">" : "");
    if (err)
        exit(1);
    exit(0);
}

int main(int argc, char **argv)
{
    int opt;
    int index = 0;
    bool color = true;
    bool ts = false;
    uint64_t time_limit = DEFAULT_TIME_LIMIT;
    unsigned int rt_priority = 0;
    const char *syslog_tag = NULL;
    bool udp = false;
    int mtu = TS_PAYLOAD_SIZE;
    enum upipe_ts_conformance conformance = UPIPE_TS_CONFORMANCE_AUTO;
    bool no_stdin = false;

    /*
     * parse options
     */
    while ((opt = getopt_long(argc, argv, "vUM:K:", options, &index)) != -1) {
        switch ((enum opt)opt) {
        case OPT_VERBOSE:
            switch (log_level) {
            case UPROBE_LOG_DEBUG:
                log_level = UPROBE_LOG_VERBOSE;
                break;
            case UPROBE_LOG_NOTICE:
                log_level = UPROBE_LOG_DEBUG;
                break;
            }
            break;

        case OPT_ID:
            variant_id = strtoull(optarg, NULL, 10);
            break;
        case OPT_ADDR:
            addr = optarg;
            break;
        case OPT_TS:
            ts = true;
            break;
        case OPT_VIDEO_PORT:
            video_output.port = atoi(optarg);
            break;
        case OPT_AUDIO_PORT:
            audio_output.port = atoi(optarg);
            break;
        case OPT_NO_VIDEO:
            video_output.enabled = false;
            break;
        case OPT_NO_AUDIO:
            audio_output.enabled = false;
            break;
        case OPT_NO_COLOR:
            color = false;
            break;
        case OPT_REWRITE_DATE:
            rewrite_date = true;
            break;
        case OPT_SEEK: {
            struct ustring_time t = ustring_to_time_str(optarg);
            if (t.str.len != strlen(optarg))
                usage(argv[0], "invalid time format %s", optarg);
            seek = t.value;
            break;
        }
        case OPT_SEQUENCE:
            sequence = strtoull(optarg, NULL, 10);
            break;
        case OPT_BANDWIDTH:
            bandwidth_max = strtoull(optarg, NULL, 10);
            break;
        case OPT_TIME_LIMIT:
            time_limit = strtoull(optarg, NULL, 10);
            break;
        case OPT_RT_PRIORITY:
            rt_priority = strtoul(optarg, NULL, 10);
            break;
        case OPT_SYSLOG_TAG:
            syslog_tag = optarg;
            break;
        case OPT_UDP:
            udp = true;
            break;
        case OPT_MTU:
            mtu = strtol(optarg, NULL, 10);
            break;
        case OPT_CONFORMANCE:
            conformance = upipe_ts_conformance_from_string(optarg);
            break;
        case OPT_NO_STDIN:
            no_stdin = true;
            break;
        case OPT_DUMP:
            dump = optarg;
            break;
        case OPT_MUX_MAX_DELAY:
            mux_max_delay = strtoull(optarg, NULL, 10);
            break;
        case OPT_MIN_DEVIATION:
            min_deviation = strtoull(optarg, NULL, 10);
            break;
        case OPT_DELAY:
            delay = strtoull(optarg, NULL, 10);
            break;

        case OPT_HELP:
            usage(argv[0], NULL);

        case OPT_INVALID:
            usage(argv[0], "invalid option");

        case OPT_MISSING_ARG:
            usage(argv[0], "missing argument");
        }
    }

    /*
     * parse arguments
     */
    if (optind >= argc) {
        usage(argv[0], NULL);
        return -1;
    }
    url = argv[optind];

    /*
     * create event loop
     */
    struct upump_mgr *upump_mgr =
        upump_ev_mgr_alloc_default(UPUMP_POOL, UPUMP_BLOCKER_POOL);
    assert(upump_mgr);
    struct upump *sigint_pump = upump_alloc_signal(upump_mgr, sigint_cb,
            (void *)SIGINT, NULL, SIGINT);
    upump_set_status(sigint_pump, false);
    upump_start(sigint_pump);
    struct upump *sigterm_pump = upump_alloc_signal(upump_mgr, sigint_cb,
            (void *)SIGTERM, NULL, SIGTERM);
    upump_set_status(sigterm_pump, false);
    upump_start(sigterm_pump);

    struct upump *stdin_pump = NULL;
    if (!no_stdin) {
        stdin_pump = upump_alloc_fd_read(upump_mgr, stdin_cb, NULL, NULL,
                                         STDIN_FILENO);
        upump_set_status(stdin_pump, false);
        upump_start(stdin_pump);
    }

    /*
     * create root probe
     */
    if (syslog_tag != NULL)
        main_probe = uprobe_syslog_alloc(NULL, syslog_tag, LOG_NDELAY | LOG_PID,
                                         LOG_USER, log_level);
    else {
        main_probe =
            uprobe_stdio_alloc(NULL, stderr, log_level);
        uprobe_stdio_set_color(main_probe, color);
    }
    assert(main_probe);

    struct uprobe probe_error;
    uprobe_init(&probe_error, catch_error, uprobe_use(main_probe));
    uprobe_release(main_probe);
    main_probe = &probe_error;

    /*
     * add umem manager probe
     */
    {
        struct umem_mgr *umem_mgr = umem_pool_mgr_alloc_simple(UMEM_POOL);
        assert(umem_mgr);

        /*
         * add uref manager probe
         */
        {
            /*
             * add udict manager
             */
            struct udict_mgr *udict_mgr =
                udict_inline_mgr_alloc(UDICT_POOL_DEPTH, umem_mgr, -1, -1);
            assert(udict_mgr);
            uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
            udict_mgr_release(udict_mgr);
        }
        assert(uref_mgr);
        main_probe = uprobe_uref_mgr_alloc(main_probe, uref_mgr);
        assert(main_probe);

        main_probe =
            uprobe_ubuf_mem_alloc(main_probe, umem_mgr,
                                  UBUF_POOL_DEPTH, UBUF_POOL_DEPTH);
        umem_mgr_release(umem_mgr);
        assert(main_probe);
    }

    /*
     * add uclock probe
     */
    {
        struct uclock *uclock = uclock_std_alloc(0);
        assert(uclock);
        main_probe = uprobe_uclock_alloc(main_probe, uclock);
        assert(main_probe);
        uclock_release(uclock);
    }

    /*
     * add upump manager probe
     */
    {
        main_probe = uprobe_pthread_upump_mgr_alloc(main_probe);
        assert(main_probe != NULL);
        uprobe_pthread_upump_mgr_set(main_probe, upump_mgr);
        upump_mgr_release(upump_mgr);
    }

    /*
     * add dejitter probe
     */
    if (min_deviation != UINT64_MAX) {
        dejitter_probe = uprobe_dejitter_alloc(uprobe_use(main_probe), true, 0);
        uprobe_dejitter_set_minimum_deviation(dejitter_probe, min_deviation);
    }

    probe_uref_mgr = upipe_probe_uref_mgr_alloc();
    assert(probe_uref_mgr);
    time_limit_mgr = upipe_time_limit_mgr_alloc();
    assert(time_limit_mgr);
    delay_mgr = upipe_delay_mgr_alloc();
    assert(delay_mgr);
    rtp_prepend_mgr = upipe_rtp_prepend_mgr_alloc();
    assert(rtp_prepend_mgr);
    udpsink_mgr = upipe_udpsink_mgr_alloc();
    assert(udpsink_mgr);
    setflowdef_mgr = upipe_setflowdef_mgr_alloc();
    assert(setflowdef_mgr);

    struct upipe_mgr *wsink_mgr = NULL;
    {
        struct upipe_mgr *sink_xfer_mgr = NULL;
        pthread_attr_t attr;
        /* sink thread */
        pthread_attr_init(&attr);
        if (rt_priority) {
            pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
            pthread_attr_setschedpolicy(&attr, SCHED_RR);
            struct sched_param param;
            param.sched_priority = rt_priority;
            pthread_attr_setschedparam(&attr, &param);
        }
        struct umutex *wsink_mutex = NULL;
        if (dump)
            wsink_mutex = umutex_pthread_alloc(0);
        sink_xfer_mgr = upipe_pthread_xfer_mgr_alloc(XFER_QUEUE,
                XFER_POOL, uprobe_use(main_probe), upump_ev_mgr_alloc_loop,
                UPUMP_POOL, UPUMP_BLOCKER_POOL, wsink_mutex, NULL, &attr);
        assert(sink_xfer_mgr != NULL);
        umutex_release(wsink_mutex);

        /* deport to sink thread */
        wsink_mgr = upipe_wsink_mgr_alloc(sink_xfer_mgr);
        assert(wsink_mgr != NULL);
        upipe_mgr_release(sink_xfer_mgr);
    }

    if (ts) {
        uprobe_throw(main_probe, NULL, UPROBE_FREEZE_UPUMP_MGR);
        /* udp sink */
        struct upipe *sink = upipe_void_alloc(udpsink_mgr,
                    uprobe_pfx_alloc(uprobe_use(main_probe),
                                     UPROBE_LOG_VERBOSE, "udpsink"));
        assert(sink != NULL);
        upipe_attach_uclock(sink);
        upipe_set_max_length(sink, UINT16_MAX);

        if (!ubase_check(upipe_set_uri(sink, addr))) {
            upipe_release(sink);

            struct upipe_mgr *fsink_mgr = upipe_fsink_mgr_alloc();
            sink = upipe_void_alloc(fsink_mgr,
                    uprobe_pfx_alloc(uprobe_use(main_probe),
                                     UPROBE_LOG_VERBOSE, "fsink"));
            upipe_mgr_release(fsink_mgr);
            upipe_fsink_set_path(sink, addr, UPIPE_FSINK_OVERWRITE);

        } else if (!udp) {
            /* add rtp header */
            sink = upipe_void_chain_input(sink, rtp_prepend_mgr,
                uprobe_pfx_alloc(uprobe_use(main_probe),
                                 UPROBE_LOG_VERBOSE, "rtp encaps"));
            assert(sink != NULL);
        }

        /* ts mux */
        struct upipe_mgr *upipe_ts_mux_mgr = upipe_ts_mux_mgr_alloc();
        ts_mux = upipe_void_alloc(upipe_ts_mux_mgr,
                uprobe_pfx_alloc(uprobe_use(main_probe),
                                 UPROBE_LOG_VERBOSE, "mux"));
        assert(ts_mux);
        upipe_mgr_release(upipe_ts_mux_mgr);
        upipe_ts_mux_set_mode(ts_mux, UPIPE_TS_MUX_MODE_CAPPED);
        upipe_set_output_size(ts_mux, mtu);
        upipe_ts_mux_set_padding_octetrate(ts_mux, PADDING_OCTETRATE);
        upipe_attach_uclock(ts_mux);
        if (conformance != UPIPE_TS_CONFORMANCE_AUTO)
            upipe_ts_mux_set_conformance(ts_mux, conformance);
        if (mux_max_delay != UINT64_MAX)
            upipe_ts_mux_set_max_delay(ts_mux,
                                       mux_max_delay * (UCLOCK_FREQ / 1000));

        struct uref *flow_def = uref_alloc_control(uref_mgr);
        uref_flow_set_def(flow_def, "void.");
        upipe_set_flow_def(ts_mux, flow_def);
        uref_free(flow_def);

        upipe_set_output(ts_mux, sink);
        upipe_release(sink);

        flow_def = uref_alloc_control(uref_mgr);
        uref_flow_set_def(flow_def, "void.");
        ts_mux = upipe_void_chain_sub(ts_mux,
                uprobe_pfx_alloc(uprobe_use(main_probe), UPROBE_LOG_VERBOSE,
                                 "mux prog"));
        assert(ts_mux);
        uref_flow_set_id(flow_def, 1);
        uref_ts_flow_set_pid(flow_def, 256);
        upipe_set_flow_def(ts_mux, flow_def);
        uref_free(flow_def);
        uprobe_throw(main_probe, NULL, UPROBE_THAW_UPUMP_MGR);
    }

    /*
     * create trickp pipe
     */
    {
        struct upipe *trickp = NULL;

        if (min_deviation == UINT64_MAX) {
            struct upipe_mgr *upipe_trickp_mgr = upipe_trickp_mgr_alloc();
            assert(upipe_trickp_mgr);
            trickp = upipe_void_alloc(
                upipe_trickp_mgr,
                uprobe_pfx_alloc(uprobe_use(main_probe),
                                 UPROBE_LOG_VERBOSE, "trickp"));
            upipe_mgr_release(upipe_trickp_mgr);
            assert(trickp);
            upipe_end_preroll(trickp);
        }

        /* create video sink */
        if (video_output.enabled) {
            video_output.sink = hls2rtp_video_sink(main_probe, trickp,
                                                   time_limit, wsink_mgr);
            assert(video_output.sink);
        }

        /* create audio sink */
        if (audio_output.enabled) {
            audio_output.sink = hls2rtp_audio_sink(main_probe, trickp,
                                                   time_limit, wsink_mgr);
            assert(audio_output.sink);
        }

        upipe_release(trickp);
    }

    if (dejitter_probe) {
        struct upipe_mgr *upipe_dejitter_mgr = upipe_dejitter_mgr_alloc();
        assert(upipe_dejitter_mgr);
        dejitter = upipe_void_alloc(
            upipe_dejitter_mgr,
            uprobe_pfx_alloc(uprobe_use(dejitter_probe),
                             UPROBE_LOG_VERBOSE, "dejitter"));
        assert(dejitter);
        upipe_mgr_release(upipe_dejitter_mgr);
    }

    /*
     * deport to sink thread
     */
    if (ts_mux) {
        ts_mux = upipe_wsink_alloc(wsink_mgr,
                uprobe_pfx_alloc(
                    uprobe_use(main_probe),
                    UPROBE_LOG_VERBOSE, "wsink"),
                ts_mux,
                uprobe_pfx_alloc(uprobe_use(main_probe),
                                 UPROBE_LOG_VERBOSE, "wsink_x"),
                QUEUE_LENGTH);
        assert(ts_mux != NULL);
    }

    upipe_mgr_release(wsink_mgr);

    /*
     * create source pipe
     */
    struct uprobe probe_src;
    uprobe_init(&probe_src, catch_src, uprobe_use(main_probe));
    uprobe_release(main_probe);
    main_probe = &probe_src;
    {
        struct upipe_mgr *upipe_auto_src_mgr = upipe_auto_src_mgr_alloc();
        assert(upipe_auto_src_mgr);
        {
            struct upipe_mgr *upipe_fsrc_mgr = upipe_fsrc_mgr_alloc();
            struct upipe_mgr *upipe_http_src_mgr = upipe_http_src_mgr_alloc();
            assert(upipe_fsrc_mgr && upipe_http_src_mgr);
            ubase_assert(upipe_auto_src_mgr_set_mgr(upipe_auto_src_mgr, "file",
                                                    upipe_fsrc_mgr));
            ubase_assert(upipe_auto_src_mgr_set_mgr(upipe_auto_src_mgr, "http",
                                                    upipe_http_src_mgr));
            ubase_assert(upipe_auto_src_mgr_set_mgr(upipe_auto_src_mgr, "https",
                                                    upipe_http_src_mgr));
            upipe_mgr_release(upipe_fsrc_mgr);
            upipe_mgr_release(upipe_http_src_mgr);
        }
        main_probe = uprobe_source_mgr_alloc(main_probe, upipe_auto_src_mgr);
        assert(main_probe);

        src = upipe_void_alloc(
            upipe_auto_src_mgr,
            uprobe_pfx_alloc(uprobe_use(main_probe),
                             UPROBE_LOG_VERBOSE, "src"));
        upipe_mgr_release(upipe_auto_src_mgr);
        assert(src);
        ubase_assert(upipe_set_uri(src, url));
    }

    /*
     * add hls pipe
     */
    struct uprobe probe_hls;
    uprobe_init(&probe_hls, catch_hls, uprobe_use(main_probe));
    uprobe_release(main_probe);
    {
        struct upipe_mgr *upipe_hls_mgr = upipe_hls_mgr_alloc();
        assert(upipe_hls_mgr);
        hls = upipe_void_alloc_output(
            src, upipe_hls_mgr,
            uprobe_pfx_alloc(uprobe_use(&probe_hls),
                             UPROBE_LOG_VERBOSE, "hls"));
        upipe_mgr_release(upipe_hls_mgr);
        assert(hls);
    }

    /*
     * run main loop
     */
    upump_mgr_run(upump_mgr, NULL);

    /*
     * release resources
     */
    upump_stop(sigint_pump);
    upump_free(sigint_pump);
    upump_stop(sigterm_pump);
    upump_free(sigterm_pump);
    if (stdin_pump != NULL) {
        upump_stop(stdin_pump);
        upump_free(stdin_pump);
    }
    upipe_mgr_release(probe_uref_mgr);
    upipe_mgr_release(time_limit_mgr);
    upipe_mgr_release(delay_mgr);
    upipe_mgr_release(rtp_prepend_mgr);
    upipe_mgr_release(udpsink_mgr);
    upipe_mgr_release(setflowdef_mgr);
    uprobe_clean(&probe_hls);
    uprobe_clean(&probe_src);
    uprobe_clean(&probe_error);
    uprobe_release(dejitter_probe);
    uref_mgr_release(uref_mgr);

    return 0;
}
