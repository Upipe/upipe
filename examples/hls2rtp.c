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
#include <upipe/uref_m3u_master.h>
#include <upipe/uref_m3u_playlist_flow.h>
#include <upipe/uref_dump.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe-pthread/upipe_pthread_transfer.h>
#include <upipe-pthread/uprobe_pthread_upump_mgr.h>
#include <upipe-ts/upipe_ts_demux.h>
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

#define UPROBE_LOG_LEVEL                UPROBE_LOG_NOTICE
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

static const char *url = NULL;
static const char *addr = "127.0.0.1";
static uint16_t video_port = 5004;
static uint16_t audio_port = 5006;
static uint32_t video_rtp_type = 96;
static uint32_t audio_rtp_type = 97;
static bool audio_enabled = true;
static bool video_enabled = true;

static struct upipe *src = NULL;
static struct upipe *trickp = NULL;

struct uprobe_audio {
    struct uprobe probe;
};

UPROBE_HELPER_UPROBE(uprobe_audio, probe);

static int catch_audio(struct uprobe *uprobe,
                          struct upipe *upipe,
                          int event, va_list args)
{
    switch (event) {
    case UPROBE_NEED_OUTPUT: {
        struct uref *flow_def = va_arg(args, struct uref *);
        int ret;

        UBASE_RETURN(uref_flow_match_def(flow_def, "block.aac.sound."));

        ret = snprintf(NULL, 0, "%s:%u", addr, audio_port);
        if (unlikely(ret < 0))
            return UBASE_ERR_INVALID;
        char uri[ret + 1];
        ret = snprintf(uri, sizeof (uri), "%s:%u", addr, audio_port);
        if (ret < 0 || (unsigned)ret >= sizeof (uri))
            return UBASE_ERR_INVALID;

        struct upipe *output = upipe_use(upipe);

        struct upipe_mgr *upipe_rtp_mpeg4_mgr = upipe_rtp_mpeg4_mgr_alloc();
        assert(upipe_rtp_mpeg4_mgr);
        output = upipe_void_chain_output(
            output, upipe_rtp_mpeg4_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe->next),
                             UPROBE_LOG_VERBOSE, "rtp aac"));
        upipe_mgr_release(upipe_rtp_mpeg4_mgr);
        UBASE_ALLOC_RETURN(output);

        struct upipe_mgr *upipe_rtp_prepend_mgr = upipe_rtp_prepend_mgr_alloc();
        assert(upipe_rtp_prepend_mgr);
        output = upipe_void_chain_output(
            output, upipe_rtp_prepend_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe->next),
                             UPROBE_LOG_VERBOSE, "rtp"));
        upipe_mgr_release(upipe_rtp_prepend_mgr);
        UBASE_ALLOC_RETURN(output);
        ret = upipe_rtp_prepend_set_type(output, audio_rtp_type);
        if (unlikely(!ubase_check(ret))) {
            upipe_release(output);
            return ret;
        }

        output = upipe_void_chain_output_sub(
            output, trickp,
            uprobe_pfx_alloc(uprobe_use(uprobe->next),
                             UPROBE_LOG_VERBOSE, "trickp sound"));
        UBASE_ALLOC_RETURN(output);

        struct upipe_mgr *upipe_udpsink_mgr = upipe_udpsink_mgr_alloc();
        assert(upipe_udpsink_mgr);
        output = upipe_void_chain_output(
            output, upipe_udpsink_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe->next),
                             UPROBE_LOG_VERBOSE, "udp"));
        upipe_mgr_release(upipe_udpsink_mgr);
        UBASE_ALLOC_RETURN(output);
        ret = upipe_set_uri(output, uri);
        if (unlikely(!ubase_check(ret))) {
            upipe_release(output);
            return ret;
        }

        upipe_release(output);

        return ret;
    }
    }
    return uprobe_throw_next(uprobe, upipe, event, args);
}

static struct uprobe *
uprobe_audio_init(struct uprobe_audio *probe_audio,
                     struct uprobe *next)
{
    struct uprobe *probe = uprobe_audio_to_uprobe(probe_audio);
    uprobe_init(probe, catch_audio, next);
    return probe;
}

static void uprobe_audio_clean(struct uprobe_audio *probe_audio)
{
    struct uprobe *probe = uprobe_audio_to_uprobe(probe_audio);
    uprobe_clean(probe);
}

struct uprobe *uprobe_audio_alloc(struct uprobe *next);

#define ARGS next
#define ARGS_DECL struct uprobe *next
UPROBE_HELPER_ALLOC(uprobe_audio);
#undef ARGS_DECL
#undef ARGS

struct uprobe_video {
    struct uprobe probe;
};

UPROBE_HELPER_UPROBE(uprobe_video, probe);

static int catch_video(struct uprobe *uprobe,
                          struct upipe *upipe,
                          int event, va_list args)
{
    switch (event) {
    case UPROBE_NEED_OUTPUT: {
        struct uref *flow_def = va_arg(args, struct uref *);
        int ret;

        UBASE_RETURN(uref_flow_match_def(flow_def, "block.h264.pic."));

        ret = snprintf(NULL, 0, "%s:%u", addr, video_port);
        if (unlikely(ret < 0))
            return UBASE_ERR_INVALID;
        char uri[ret + 1];
        ret = snprintf(uri, sizeof (uri), "%s:%u", addr, video_port);
        if (ret < 0 || (unsigned)ret >= sizeof (uri))
            return UBASE_ERR_INVALID;

        struct upipe *output = upipe_use(upipe);

        struct upipe_mgr *upipe_rtp_h264_mgr = upipe_rtp_h264_mgr_alloc();
        assert(upipe_rtp_h264_mgr);
        output = upipe_void_chain_output(
            output, upipe_rtp_h264_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe->next),
                             UPROBE_LOG_VERBOSE, "rtp h264"));
        upipe_mgr_release(upipe_rtp_h264_mgr);
        UBASE_ALLOC_RETURN(output);

        struct upipe_mgr *upipe_rtp_prepend_mgr = upipe_rtp_prepend_mgr_alloc();
        assert(upipe_rtp_prepend_mgr);
        output = upipe_void_chain_output(
            output, upipe_rtp_prepend_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe->next),
                             UPROBE_LOG_VERBOSE, "rtp"));
        upipe_mgr_release(upipe_rtp_prepend_mgr);
        UBASE_ALLOC_RETURN(output);
        ret = upipe_rtp_prepend_set_type(output, video_rtp_type);
        if (unlikely(!ubase_check(ret))) {
            upipe_release(output);
            return ret;
        }

        output = upipe_void_chain_output_sub(
            output, trickp,
            uprobe_pfx_alloc(uprobe_use(uprobe->next),
                             UPROBE_LOG_VERBOSE, "trickp pic"));
        UBASE_ALLOC_RETURN(output);

        struct upipe_mgr *upipe_udpsink_mgr = upipe_udpsink_mgr_alloc();
        assert(upipe_udpsink_mgr);
        output = upipe_void_chain_output(
            output, upipe_udpsink_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe->next),
                             UPROBE_LOG_VERBOSE, "udp"));
        upipe_mgr_release(upipe_udpsink_mgr);
        UBASE_ALLOC_RETURN(output);
        ret = upipe_set_uri(output, uri);
        if (unlikely(!ubase_check(ret))) {
            upipe_release(output);
            return ret;
        }

        upipe_release(output);

        return ret;
    }
    }
    return uprobe_throw_next(uprobe, upipe, event, args);
}

static struct uprobe *
uprobe_video_init(struct uprobe_video *probe_video,
                     struct uprobe *next)
{
    struct uprobe *probe = uprobe_video_to_uprobe(probe_video);
    uprobe_init(probe, catch_video, next);
    return probe;
}

static void uprobe_video_clean(struct uprobe_video *probe_video)
{
    struct uprobe *probe = uprobe_video_to_uprobe(probe_video);
    uprobe_clean(probe);
}

struct uprobe *uprobe_video_alloc(struct uprobe *next);

#define ARGS next
#define ARGS_DECL struct uprobe *next
UPROBE_HELPER_ALLOC(uprobe_video);
#undef ARGS_DECL
#undef ARGS

struct uprobe_playlist {
    struct uprobe probe;
};

UPROBE_HELPER_UPROBE(uprobe_playlist, probe);

static int catch_playlist(struct uprobe *uprobe,
                          struct upipe *upipe,
                          int event, va_list args)
{
    if (event > UPROBE_LOCAL) {
        switch (ubase_get_signature(args)) {
        case UPIPE_HLS_PLAYLIST_SIGNATURE:
            if (event == UPROBE_HLS_PLAYLIST_RELOADED)
                return upipe_hls_playlist_play(upipe);
            if (event == UPROBE_HLS_PLAYLIST_ITEM_END) {
                UBASE_RETURN(upipe_hls_playlist_next(upipe));
                return upipe_hls_playlist_play(upipe);
            }
        }
    }
    return uprobe_throw_next(uprobe, upipe, event, args);
}

static struct uprobe *
uprobe_playlist_init(struct uprobe_playlist *probe_playlist,
                     struct uprobe *next)
{
    struct uprobe *probe = uprobe_playlist_to_uprobe(probe_playlist);
    uprobe_init(probe, catch_playlist, next);
    return probe;
}

static void uprobe_playlist_clean(struct uprobe_playlist *probe_playlist)
{
    struct uprobe *probe = uprobe_playlist_to_uprobe(probe_playlist);
    uprobe_clean(probe);
}

struct uprobe *uprobe_playlist_alloc(struct uprobe *next);

#define ARGS next
#define ARGS_DECL struct uprobe *next
UPROBE_HELPER_ALLOC(uprobe_playlist);
#undef ARGS_DECL
#undef ARGS

struct uprobe_variant {
    struct uprobe probe;
};

UPROBE_HELPER_UPROBE(uprobe_variant, probe);

struct upipe *audio = NULL;
struct upipe *video = NULL;

static int catch_variant(struct uprobe *uprobe,
                         struct upipe *upipe,
                         int event, va_list args)
{
    switch (event) {
    case UPROBE_SPLIT_UPDATE: {
        struct uref *uref_video = NULL;
        struct uref *uref_audio = NULL;

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
                    uref_video = uref_dup(uref);
            }
            else {
                uprobe_warn_va(uprobe, NULL, "unhandle flow %s", def);
            }
        }

        if (!audio_enabled)
            uref_audio = NULL;

        if (!video_enabled)
            uref_video = NULL;

        uint64_t audio_id, video_id;
        if (uref_audio && uref_video &&
            ubase_check(uref_flow_get_id(uref_audio, &audio_id)) &&
            ubase_check(uref_flow_get_id(uref_video, &video_id)) &&
            audio_id == video_id) {
            video = audio = upipe_flow_alloc_sub(
                upipe,
                uprobe_pfx_alloc(
                    uprobe_playlist_alloc(
                        uprobe_selflow_alloc(
                            uprobe_selflow_alloc(
                                uprobe_use(uprobe->next),
                                uprobe_pfx_alloc(
                                    uprobe_audio_alloc(
                                        uprobe_use(uprobe->next)),
                                    UPROBE_LOG_VERBOSE, "sound"),
                                UPROBE_SELFLOW_SOUND, "auto"),
                            uprobe_pfx_alloc(
                                uprobe_video_alloc(
                                    uprobe_use(uprobe->next)),
                                UPROBE_LOG_VERBOSE, "pic"),
                            UPROBE_SELFLOW_PIC, "auto")),
                    UPROBE_LOG_VERBOSE, "mixed"),
                uref_audio);
        }
        else {
            if (uref_audio) {
                audio = upipe_flow_alloc_sub(
                    upipe,
                    uprobe_pfx_alloc(
                        uprobe_playlist_alloc(
                            uprobe_selflow_alloc(
                                uprobe_use(uprobe->next),
                                uprobe_audio_alloc(
                                    uprobe_use(uprobe->next)),
                                UPROBE_SELFLOW_SOUND, "auto")),
                        UPROBE_LOG_VERBOSE, "audio"),
                    uref_audio);
            }
            else
                uprobe_warn(uprobe, NULL, "no audio");

            if (uref_video) {
                video = upipe_flow_alloc_sub(
                    upipe,
                    uprobe_pfx_alloc(
                        uprobe_playlist_alloc(
                            uprobe_selflow_alloc(
                                uprobe_use(uprobe->next),
                                uprobe_video_alloc(
                                    uprobe_use(uprobe->next)),
                                UPROBE_SELFLOW_PIC, "auto")),
                        UPROBE_LOG_VERBOSE, "video"),
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

static struct uprobe *
uprobe_variant_init(struct uprobe_variant *probe_variant,
                    struct uprobe *next)
{
    struct uprobe *probe = uprobe_variant_to_uprobe(probe_variant);
    uprobe_init(probe, catch_variant, next);
    return probe;
}

static void uprobe_variant_clean(struct uprobe_variant *probe_variant)
{
    struct uprobe *probe = uprobe_variant_to_uprobe(probe_variant);
    uprobe_clean(probe);
}

struct uprobe *uprobe_variant_alloc(struct uprobe *next);

#define ARGS_DECL struct uprobe *next
#define ARGS next
UPROBE_HELPER_ALLOC(uprobe_variant);
#undef ARGS
#undef ARGS_DECL

static struct upipe *variant = NULL;

static void select_variant(struct uprobe *uprobe,
                           struct upipe *upipe,
                           struct uref *uref)
{
    uprobe_notice_va(uprobe, NULL, "selected variant");
    if (uref) {
        uref_dump(uref, uprobe);
        variant = upipe_flow_alloc_sub(
            upipe,
            uprobe_pfx_alloc(
                uprobe_variant_alloc(
                    uprobe_use(uprobe->next)),
                UPROBE_LOG_VERBOSE, "variant"),
            uref);
        assert(variant);
    }
    else
        uprobe_dbg(uprobe, NULL, "(none)");
}

static int selected_variant_id = 0;

static int catch_hls(struct uprobe *uprobe,
                     struct upipe *upipe,
                     int event, va_list args)
{
    switch (event) {
    case UPROBE_SPLIT_UPDATE: {
        uprobe_notice_va(uprobe, NULL, "list:");

        struct uref *uref_variant = NULL;
        for (struct uref *uref = NULL;
             ubase_check(upipe_split_iterate(upipe, &uref)) && uref;) {
            uint64_t id;
            ubase_assert(uref_flow_get_id(uref, &id));
            const char *uri = "(none)";
            uref_m3u_get_uri(uref, &uri);

            uprobe_notice_va(uprobe, NULL, "%"PRIu64" - %s", id, uri);
            uref_dump(uref, uprobe);

            if (selected_variant_id == id)
                uref_variant = uref;
        }

        if (uref_variant)
            select_variant(uprobe, upipe, uref_variant);
        else {
            uprobe_warn_va(uprobe, NULL, "no variant with id %u",
                           selected_variant_id);
        }
        return UBASE_ERR_NONE;
    }
    }

    return uprobe_throw_next(uprobe, upipe, event, args);
}

enum opt {
    OPT_ID      = 0x100,
    OPT_ADDR,
    OPT_VIDEO_PORT,
    OPT_AUDIO_PORT,
    OPT_NO_AUDIO,
    OPT_NO_VIDEO,
};

static struct option options[] = {
    { "id", required_argument, NULL, OPT_ID },
    { "addr", required_argument, NULL, OPT_ADDR },
    { "video-port", required_argument, NULL, OPT_VIDEO_PORT },
    { "audio-port", required_argument, NULL, OPT_AUDIO_PORT },
    { "no-video", no_argument, NULL, OPT_NO_VIDEO },
    { "no-audio", no_argument, NULL, OPT_NO_AUDIO },
};

static void usage(const char *name)
{
    fprintf(stderr, "%s <url>\n", name);
}

static void sigint_cb(struct ev_loop *loop, struct ev_signal *w, int revents)
{
    ev_unloop(loop, EVUNLOOP_ALL);
}

int main(int argc, char **argv)
{
    int opt;
    int index = 0;

    /*
     * parse options
     */
    while ((opt = getopt_long(argc, argv, "", options, &index)) != -1) {
        switch ((enum opt)opt) {
        case OPT_ID:
            selected_variant_id = atoi(optarg);
            break;
        case OPT_ADDR:
            addr = optarg;
            break;
        case OPT_VIDEO_PORT:
            video_port = atoi(optarg);
            break;
        case OPT_AUDIO_PORT:
            audio_port = atoi(optarg);
            break;
        case OPT_NO_VIDEO:
            video_enabled = false;
            break;
        case OPT_NO_AUDIO:
            audio_enabled = false;
            break;
        }
    }

    /*
     * parse arguments
     */
    if (optind >= argc) {
        usage(argv[0]);
        return -1;
    }
    url = argv[optind];

    /*
     * create event loop
     */
    struct ev_loop *loop = ev_default_loop(0);
    assert(loop);
    struct ev_signal signal_watcher;
    ev_signal_init(&signal_watcher, sigint_cb, SIGINT);
    ev_signal_start(loop, &signal_watcher);

    /*
     * create root probe
     */
    struct uprobe *probe =
        uprobe_stdio_color_alloc(NULL, stderr, UPROBE_LOG_LEVEL);
    assert(probe);

    /*
     * add upump manager probe
     */
    {
        struct upump_mgr *upump_mgr =
            upump_ev_mgr_alloc(loop, UPUMP_POOL, UPUMP_BLOCKER_POOL);
        assert(upump_mgr);
        probe = uprobe_upump_mgr_alloc(probe, upump_mgr);
        upump_mgr_release(upump_mgr);
        assert(probe != NULL);
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
            probe = uprobe_uref_mgr_alloc(probe, uref_mgr);
            uref_mgr_release(uref_mgr);
            assert(probe);
        }

        probe =
            uprobe_ubuf_mem_alloc(probe, umem_mgr,
                                  UBUF_POOL_DEPTH, UBUF_POOL_DEPTH);
        umem_mgr_release(umem_mgr);
        assert(probe);
    }

    /*
     * add uclock probe
     */
    {
        struct uclock *uclock = uclock_std_alloc(0);
        assert(uclock);
        probe = uprobe_uclock_alloc(probe, uclock);
        assert(probe);
        uclock_release(uclock);
    }


    /*
     * create trickp pipe
     */
    {
        struct upipe_mgr *upipe_trickp_mgr = upipe_trickp_mgr_alloc();
        assert(upipe_trickp_mgr);
        trickp = upipe_void_alloc(
            upipe_trickp_mgr,
            uprobe_pfx_alloc(uprobe_use(probe),
                             UPROBE_LOG_VERBOSE, "trickp"));
        upipe_mgr_release(upipe_trickp_mgr);
        assert(trickp);
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
        probe = uprobe_source_mgr_alloc(probe, upipe_auto_src_mgr);
        assert(probe);

        src = upipe_void_alloc(
            upipe_auto_src_mgr,
            uprobe_pfx_alloc(uprobe_use(probe),
                             UPROBE_LOG_VERBOSE, "src"));
        upipe_mgr_release(upipe_auto_src_mgr);
        assert(src);
        ubase_assert(upipe_set_uri(src, url));
    }

    /*
     * add hls pipe
     */
    struct uprobe probe_hls;
    uprobe_init(&probe_hls, catch_hls, uprobe_use(probe));
    {
        struct upipe *hls = NULL;
        struct upipe_mgr *upipe_hls_mgr = upipe_hls_mgr_alloc();
        assert(upipe_hls_mgr);
        hls = upipe_void_alloc_output(
            src, upipe_hls_mgr,
            uprobe_pfx_alloc(uprobe_use(&probe_hls),
                             UPROBE_LOG_VERBOSE, "hls"));
        upipe_mgr_release(upipe_hls_mgr);
        assert(hls);
        upipe_release(hls);
    }

    /*
     * run main loop
     */
    ev_loop(loop, 0);

    /*
     * release ressources
     */
    if (audio == video)
        upipe_release(audio);
    else {
        upipe_release(audio);
        upipe_release(video);
    }
    upipe_release(variant);
    upipe_release(src);
    upipe_release(trickp);
    uprobe_clean(&probe_hls);
    uprobe_release(probe);

    ev_loop_destroy(loop);

    return 0;
}
