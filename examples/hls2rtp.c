#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <libgen.h>
#include <ctype.h>

#include <upipe/ulist.h>
#include <upipe/uuri.h>
#include <upipe/upipe.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_helper_uprobe.h>
#include <upipe/uprobe_helper_urefcount.h>
#include <upipe/uprobe_helper_alloc.h>
#include <upipe/uprobe_stdio_color.h>
#include <upipe/uprobe_loglevel.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_select_flows.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/uprobe_uclock.h>
#include <upipe/uprobe_upump_mgr.h>
#include <upipe/uprobe_source_mgr.h>
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
#include <upipe/uref_dump.h>
#include <upipe/uref_pic.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe-pthread/upipe_pthread_transfer.h>
#include <upipe-pthread/uprobe_pthread_upump_mgr.h>
#include <upipe-modules/uprobe_http_redirect.h>
#include <upipe-modules/upipe_null.h>
#include <upipe-modules/upipe_auto_source.h>
#include <upipe-modules/upipe_file_source.h>
#include <upipe-modules/upipe_http_source.h>
#include <upipe-modules/upipe_trickplay.h>
#include <upipe-modules/upipe_rtp_prepend.h>
#include <upipe-modules/upipe_udp_sink.h>
#include <upipe-modules/upipe_dump.h>
#include <upipe-modules/upipe_rtp_h264.h>
#include <upipe-modules/upipe_rtp_mpeg4.h>
#include <upipe-modules/upipe_probe_uref.h>
#include <upipe-hls/upipe_hls.h>
#include <upipe-hls/upipe_hls_master.h>
#include <upipe-hls/upipe_hls_variant.h>
#include <upipe-hls/upipe_hls_playlist.h>
#include <upipe-hls/uref_hls.h>
#include <upipe-framers/upipe_h264_framer.h>
#include <upipe-framers/upipe_mpga_framer.h>
#include <upipe-framers/upipe_mpgv_framer.h>
#include <upipe-framers/upipe_a52_framer.h>

#include <ev.h>

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

static int log_level = UPROBE_LOG_NOTICE;
static int variant_id = 0;
static const char *url = NULL;
static const char *addr = "127.0.0.1";
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
static uint64_t seek = 0;
static uint64_t sequence = 0;
static struct upipe *src = NULL;
static struct upipe *hls = NULL;
static struct upipe *variant = NULL;
static struct uprobe *main_probe = NULL;
struct ev_signal signal_watcher;
struct ev_io stdin_watcher;

static struct uprobe *uprobe_rewrite_date_alloc(struct uprobe *next);
static struct uprobe *uprobe_variant_alloc(struct uprobe *next,
                                           uint64_t id, uint64_t at);
static struct uprobe *uprobe_audio_alloc(struct uprobe *next);
static struct uprobe *uprobe_video_alloc(struct uprobe *next);
static struct uprobe *uprobe_seek_alloc(struct uprobe *next, uint64_t at);
static struct uprobe *uprobe_playlist_alloc(struct uprobe *next,
                                            uint64_t variant_id,
                                            uint64_t at);

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

static int select_variant(struct uprobe *uprobe, uint64_t variant_id)
{
    struct uref *uref_variant = NULL;
    for (struct uref *uref = NULL;
         ubase_check(upipe_split_iterate(hls, &uref)) && uref;) {
        uint64_t id;
        ubase_assert(uref_flow_get_id(uref, &id));
        const char *uri = "(none)";
        uref_m3u_get_uri(uref, &uri);

        uprobe_notice_va(uprobe, NULL, "%"PRIu64" - %s", id, uri);
        uref_dump(uref, uprobe);

        if (variant_id == id)
            uref_variant = uref;
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
    select_variant(main_probe, variant_id);
}

/** @This stops the current variant.  */
static void cmd_stop(void)
{
    upipe_cleanup(&audio_output.pipe);
    upipe_cleanup(&video_output.pipe);
    upipe_cleanup(&variant);
}

/** @This quits the program.
 *
 * @param loop event loop
 */
static void cmd_quit(struct ev_loop *loop)
{
    cmd_stop();
    upipe_cleanup(&hls);
    upipe_cleanup(&src);
    ev_signal_stop(loop, &signal_watcher);
    ev_io_stop(loop, &stdin_watcher);
}

/** @This handles SIGINT signal. */
static void sigint_cb(struct ev_loop *loop, struct ev_signal *w, int revents)
{
    cmd_quit(loop);
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
static void stdin_cb(struct ev_loop *loop, struct ev_io *io, int revents)
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
        cmd_quit(loop);
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
};

UPROBE_HELPER_UPROBE(uprobe_rewrite_date, probe);

/** @This is the rewrite date callback */
static int catch_rewrite_date(struct uprobe *uprobe, struct upipe *upipe,
                              int event, va_list args)
{
    if (event >= UPROBE_LOCAL) {
        switch (ubase_get_signature(args)) {
        case UPIPE_PROBE_UREF_SIGNATURE:
            UBASE_SIGNATURE_CHECK(args, UPIPE_PROBE_UREF_SIGNATURE);
            struct uref *uref = va_arg(args, struct uref *);
            bool *drop = va_arg(args, bool *);

            *drop = false;
            int type;
            uint64_t date;
            uref_clock_get_date_orig(uref, &date, &type);
            if (type != UREF_DATE_NONE) {
                uprobe_verbose_va(uprobe, NULL, "rewrite %p orig -> prog",
                                  uref);
                uref_clock_set_date_prog(uref, date, type);
            }
            return UBASE_ERR_NONE;
        }
    }

    return uprobe_throw_next(uprobe, upipe, event, args);
}

/** @This initializes a rewrite date probe. */
static struct uprobe *
uprobe_rewrite_date_init(struct uprobe_rewrite_date *probe_rewrite_date,
                         struct uprobe *next)
{
    struct uprobe *probe = uprobe_rewrite_date_to_uprobe(probe_rewrite_date);
    uprobe_init(probe, catch_rewrite_date, next);
    return probe;
}

/** @This cleans a rewrite date probe */
static void uprobe_rewrite_date_clean(struct uprobe_rewrite_date *probe)
{
    uprobe_clean(uprobe_rewrite_date_to_uprobe(probe));
}

#define ARGS_DECL struct uprobe *next
#define ARGS next
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
        struct uref *flow_def = va_arg(args, struct uref *);
        UBASE_RETURN(uref_flow_match_def(flow_def, "block.aac.sound."));

        struct upipe *output = upipe_use(upipe);
        if (rewrite_date) {
            struct upipe_mgr *upipe_probe_uref_mgr =
                upipe_probe_uref_mgr_alloc();
            assert(upipe_probe_uref_mgr);
            output = upipe_void_chain_output(
                output, upipe_probe_uref_mgr,
                uprobe_pfx_alloc(
                    uprobe_rewrite_date_alloc(uprobe_use(main_probe)),
                    UPROBE_LOG_VERBOSE, "uref"));
            upipe_mgr_release(upipe_probe_uref_mgr);
            assert(output);
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
        struct uref *flow_def = va_arg(args, struct uref *);

        UBASE_RETURN(uref_flow_match_def(flow_def, "block.h264.pic."));

        struct upipe *output = upipe_use(upipe);

        if (rewrite_date) {
            struct upipe_mgr *upipe_probe_uref_mgr =
                upipe_probe_uref_mgr_alloc();
            assert(upipe_probe_uref_mgr);
            output = upipe_void_chain_output(
                output, upipe_probe_uref_mgr,
                uprobe_pfx_alloc(
                    uprobe_rewrite_date_alloc(uprobe_use(main_probe)),
                    UPROBE_LOG_VERBOSE, "uref"));
            upipe_mgr_release(upipe_probe_uref_mgr);
            assert(output);
        }

        struct upipe_mgr *upipe_probe_uref_mgr = upipe_probe_uref_mgr_alloc();
        assert(upipe_probe_uref_mgr);
        output = upipe_void_chain_output(
            output, upipe_probe_uref_mgr,
            uprobe_pfx_alloc(
                uprobe_seek_alloc(uprobe_use(main_probe), probe_video->at),
                UPROBE_LOG_VERBOSE, "seek"));
        upipe_mgr_release(upipe_probe_uref_mgr);
        UBASE_ALLOC_RETURN(output);
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

    if (event < UPROBE_LOCAL)
        return uprobe_throw_next(uprobe, upipe, event, args);

    if (ubase_get_signature(args) != UPIPE_HLS_PLAYLIST_SIGNATURE)
        return uprobe_throw_next(uprobe, upipe, event, args);

    switch (event) {
    case UPROBE_HLS_PLAYLIST_RELOADED: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_HLS_PLAYLIST_SIGNATURE);
        uprobe_notice(uprobe, NULL, "reloading playlist");
        uint64_t at = probe_playlist->at;
        if (at) {
            uint64_t remain = 0;
            uprobe_notice_va(uprobe, NULL, "seek at %"PRIu64, at);
            UBASE_RETURN(upipe_hls_playlist_seek(upipe, at, &remain));
            if (probe_playlist->video)
                probe_playlist->video->at = remain;
            probe_playlist->at = 0;
        }
        else if (sequence) {
            UBASE_RETURN(upipe_hls_playlist_set_index(upipe, sequence));
        }
        return upipe_hls_playlist_play(upipe);
    }

    case UPROBE_HLS_PLAYLIST_ITEM_END:
        UBASE_SIGNATURE_CHECK(args, UPIPE_HLS_PLAYLIST_SIGNATURE);
        UBASE_RETURN(upipe_hls_playlist_next(upipe));
        UBASE_RETURN(upipe_hls_playlist_get_index(upipe, &sequence));
        if (variant_id != probe_playlist->variant_id) {
            upipe_cleanup(&video_output.pipe);
            upipe_cleanup(&audio_output.pipe);
            upipe_cleanup(&variant);
            return select_variant(uprobe, variant_id);
        }
        return upipe_hls_playlist_play(upipe);
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
                if (ubase_check(uref_hls_get_default(uref)))
                    uref_audio = uref;
            }
            else if (ubase_check(uref_flow_match_def(uref, "pic."))) {
                if (ubase_check(uref_hls_get_default(uref)))
                    uref_video = uref;
            }
            else {
                uprobe_warn_va(uprobe, NULL, "unhandle flow %s", def);
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
        }
        else {
            if (uref_audio) {
                struct uprobe *probe_audio =
                    uprobe_audio_alloc(uprobe_use(main_probe));

                struct uprobe *probe_playlist =
                    uprobe_playlist_alloc(
                        uprobe_selflow_alloc(
                            uprobe_use(main_probe),
                            probe_audio,
                            UPROBE_SELFLOW_SOUND, "auto"),
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

        return select_variant(uprobe, variant_id);
    }
    }

    return uprobe_throw_next(uprobe, upipe, event, args);
}

static struct upipe *hls2rtp_video_sink(struct uprobe *probe,
                                        struct upipe *trickp)
{
    uint16_t port = video_output.port;
    int ret = snprintf(NULL, 0, "%s:%u", addr, port);
    assert(ret > 0);
    char uri[ret + 1];
    assert(snprintf(uri, sizeof (uri), "%s:%u", addr, port) > 0);

    struct upipe_mgr *upipe_rtp_h264_mgr = upipe_rtp_h264_mgr_alloc();
    assert(upipe_rtp_h264_mgr);
    struct upipe *sink = upipe_void_alloc(
        upipe_rtp_h264_mgr,
        uprobe_pfx_alloc(uprobe_use(probe),
                         UPROBE_LOG_VERBOSE, "rtp h264"));
    upipe_mgr_release(upipe_rtp_h264_mgr);
    assert(sink);

    struct upipe *output = upipe_use(sink);
    struct upipe_mgr *upipe_rtp_prepend_mgr = upipe_rtp_prepend_mgr_alloc();
    assert(upipe_rtp_prepend_mgr);
    output = upipe_void_chain_output(
        output, upipe_rtp_prepend_mgr,
        uprobe_pfx_alloc(uprobe_use(probe),
                         UPROBE_LOG_VERBOSE, "rtp"));
    upipe_mgr_release(upipe_rtp_prepend_mgr);
    assert(output);
    ubase_assert(upipe_rtp_prepend_set_type(output, video_output.rtp_type));

    output = upipe_void_chain_output_sub(
        output, trickp,
        uprobe_pfx_alloc(uprobe_use(probe),
                         UPROBE_LOG_VERBOSE, "trickp pic"));
    assert(output);

    struct upipe_mgr *upipe_udpsink_mgr = upipe_udpsink_mgr_alloc();
    assert(upipe_udpsink_mgr);
    output = upipe_void_chain_output(
        output, upipe_udpsink_mgr,
        uprobe_pfx_alloc(uprobe_use(probe),
                         UPROBE_LOG_VERBOSE, "udp"));
    upipe_mgr_release(upipe_udpsink_mgr);
    assert(output);
    ubase_assert(upipe_attach_uclock(output));
    ubase_assert(upipe_set_uri(output, uri));
    upipe_release(output);

    return sink;
}

static struct upipe *hls2rtp_audio_sink(struct uprobe *probe,
                                        struct upipe *trickp)
{
    uint16_t port = audio_output.port;
    int ret = snprintf(NULL, 0, "%s:%u", addr, port);
    assert(ret > 0);
    char uri[ret + 1];
    assert(snprintf(uri, sizeof (uri), "%s:%u", addr, port) > 0);

    struct upipe_mgr *upipe_rtp_mpeg4_mgr = upipe_rtp_mpeg4_mgr_alloc();
    assert(upipe_rtp_mpeg4_mgr);
    struct upipe *sink = upipe_void_alloc(
        upipe_rtp_mpeg4_mgr,
        uprobe_pfx_alloc(uprobe_use(probe),
                         UPROBE_LOG_VERBOSE, "rtp aac"));
    upipe_mgr_release(upipe_rtp_mpeg4_mgr);
    assert(sink);

    struct upipe *output = upipe_use(sink);
    struct upipe_mgr *upipe_rtp_prepend_mgr = upipe_rtp_prepend_mgr_alloc();
    assert(upipe_rtp_prepend_mgr);
    output = upipe_void_chain_output(
        output, upipe_rtp_prepend_mgr,
        uprobe_pfx_alloc(uprobe_use(probe),
                         UPROBE_LOG_VERBOSE, "rtp"));
    upipe_mgr_release(upipe_rtp_prepend_mgr);
    assert(output);
    ubase_assert(upipe_rtp_prepend_set_type(output, audio_output.rtp_type));

    output = upipe_void_chain_output_sub(
        output, trickp,
        uprobe_pfx_alloc(uprobe_use(probe),
                         UPROBE_LOG_VERBOSE, "trickp sound"));
    assert(output);

    struct upipe_mgr *upipe_udpsink_mgr = upipe_udpsink_mgr_alloc();
    assert(upipe_udpsink_mgr);
    output = upipe_void_chain_output(
        output, upipe_udpsink_mgr,
        uprobe_pfx_alloc(uprobe_use(probe),
                         UPROBE_LOG_VERBOSE, "udp"));
    upipe_mgr_release(upipe_udpsink_mgr);
    assert(output);
    ubase_assert(upipe_attach_uclock(output));
    ubase_assert(upipe_set_uri(output, uri));
    upipe_release(output);

    return sink;
}

enum opt {
    OPT_INVALID = '?',
    OPT_MISSING_ARG = ':',

    OPT_VERBOSE = 'v',

    OPT_ID      = 0x100,
    OPT_ADDR,
    OPT_VIDEO_PORT,
    OPT_AUDIO_PORT,
    OPT_NO_AUDIO,
    OPT_NO_VIDEO,
    OPT_REWRITE_DATE,
    OPT_SEEK,
    OPT_SEQUENCE,
    OPT_HELP,
};

static struct option options[] = {
    { "id", required_argument, NULL, OPT_ID },
    { "addr", required_argument, NULL, OPT_ADDR },
    { "video-port", required_argument, NULL, OPT_VIDEO_PORT },
    { "audio-port", required_argument, NULL, OPT_AUDIO_PORT },
    { "no-video", no_argument, NULL, OPT_NO_VIDEO },
    { "no-audio", no_argument, NULL, OPT_NO_AUDIO },
    { "rewrite-date", no_argument, NULL, OPT_REWRITE_DATE },
    { "verbose", no_argument, NULL, OPT_VERBOSE },
    { "seek", required_argument, NULL, OPT_SEEK },
    { "sequence", required_argument, NULL, OPT_SEQUENCE },
    { "help", no_argument, NULL, OPT_HELP },
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

    /*
     * parse options
     */
    while ((opt = getopt_long(argc, argv, "v", options, &index)) != -1) {
        switch ((enum opt)opt) {
        case OPT_VERBOSE:
            switch (log_level) {
            case UPROBE_LOG_DEBUG:
                log_level = UPROBE_LOG_VERBOSE;
            case UPROBE_LOG_NOTICE:
                log_level = UPROBE_LOG_DEBUG;
            }
            break;

        case OPT_ID:
            variant_id = atoi(optarg);
            break;
        case OPT_ADDR:
            addr = optarg;
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
    struct ev_loop *loop = ev_default_loop(0);
    assert(loop);

    ev_signal_init(&signal_watcher, sigint_cb, SIGINT);
    ev_signal_start(loop, &signal_watcher);

    ev_init(&stdin_watcher, stdin_cb);
    ev_io_set(&stdin_watcher, STDIN_FILENO, EV_READ);
    ev_io_start(loop, &stdin_watcher);

    /*
     * create root probe
     */
    main_probe =
        uprobe_stdio_color_alloc(NULL, stderr, log_level);
    assert(main_probe);

    /*
     * add upump manager probe
     */
    {
        struct upump_mgr *upump_mgr =
            upump_ev_mgr_alloc(loop, UPUMP_POOL, UPUMP_BLOCKER_POOL);
        assert(upump_mgr);
        main_probe = uprobe_upump_mgr_alloc(main_probe, upump_mgr);
        upump_mgr_release(upump_mgr);
        assert(main_probe != NULL);
    }

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
            struct uref_mgr *uref_mgr = NULL;
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
            uref_mgr_release(uref_mgr);
            assert(main_probe);
        }

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
     * create trickp pipe
     */
    {
        struct upipe_mgr *upipe_trickp_mgr = upipe_trickp_mgr_alloc();
        assert(upipe_trickp_mgr);
        struct upipe *trickp = upipe_void_alloc(
            upipe_trickp_mgr,
            uprobe_pfx_alloc(uprobe_use(main_probe),
                             UPROBE_LOG_VERBOSE, "trickp"));
        upipe_mgr_release(upipe_trickp_mgr);
        assert(trickp);

        /* create video sink */
        if (video_output.enabled) {
            video_output.sink = hls2rtp_video_sink(main_probe, trickp);
            assert(video_output.sink);
        }

        /* create audio sink */
        if (audio_output.enabled) {
            audio_output.sink = hls2rtp_audio_sink(main_probe, trickp);
            assert(audio_output.sink);
        }

        upipe_release(trickp);
    }

    /*
     * create source pipe
     */
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
    ev_loop(loop, 0);

    /*
     * release ressources
     */
    upipe_release(variant);
    upipe_release(src);
    upipe_release(video_output.sink);
    upipe_release(audio_output.sink);
    uprobe_clean(&probe_hls);

    ev_loop_destroy(loop);

    return 0;
}
