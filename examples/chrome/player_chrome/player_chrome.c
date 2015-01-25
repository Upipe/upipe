/*
 * Copyright (C) 2014-2015 OpenHeadend S.A.R.L.
 *
 * Authors: Xavier Boulet
 *          Christophe Massiot
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


#include <upipe/ubase.h>
#include <upipe/uclock.h>
#include <upipe/uclock_std.h>
#include <upipe/umem.h>
#include <upipe/umem_pool.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_std.h>
#include <upipe/upipe.h>
#include <upipe/uprobe_dejitter.h>
#include <upipe/uprobe_ubuf_mem_pool.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_upump_mgr.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_select_flows.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_uclock.h>
#include <upipe/uprobe_transfer.h>
#include <upipe/uprobe.h>
#include <upipe/upump.h>
#include <upipe-pthread/upipe_pthread_transfer.h>
#include <upipe-pthread/uprobe_pthread_upump_mgr.h>
#include <upump-ev/upump_ev.h>
#include <upipe-modules/upipe_file_source.h>
#include <upipe-modules/upipe_udp_source.h>
#include <upipe-modules/upipe_rtp_decaps.h>
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
#include <upipe-av/upipe_avcodec_decode.h>
#include <upipe-swscale/upipe_sws.h>
#include <upipe-swresample/upipe_swr.h>
#include <upipe-amt/upipe_amt_source.h>

#include <upipe-nacl/upipe_nacl_graphics2d.h>
#include <upipe-nacl/upipe_nacl_audio.h>

#include <ppapi/c/pp_resource.h>
#include <ppapi/c/pp_var.h>
#include <ppapi/c/ppb_message_loop.h>
#include <ppapi/c/ppb_messaging.h>
#include <ppapi/c/ppb_var.h>
#include <ppapi/c/ppb_var_dictionary.h>
#include <ppapi/c/ppb_view.h>
#include <ppapi_simple/ps_event.h>
#include <ppapi_simple/ps_main.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <ev.h>
#include <pthread.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <assert.h>
#include <time.h>
#include <netinet/in.h>

#include <libswscale/swscale.h>

#define UPROBE_LOG_LEVEL        UPROBE_LOG_DEBUG
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
#define SRC_OUT_QUEUE_LENGTH    10000
#define DEC_IN_QUEUE_LENGTH     50
#define DEC_OUT_QUEUE_LENGTH    5

static void demo_stop(void);

/* PPAPI Interfaces */
PPB_Var *ppb_var_interface = NULL;
PPB_Messaging *ppb_messaging_interface = NULL;
PPB_MessageLoop *ppb_message_loop_interface = NULL;
PPB_View *ppb_view_interface = NULL;
PPB_VarDictionary *ppb_var_dictionary_interface = NULL;

/* main event loop */
static struct ev_loop *loop;
/** NaCl event loop timer */
static struct upump *event_upump;
/* upump manager for the main thread */
struct upump_mgr *main_upump_mgr = NULL;
/* main (thread-safe) probe, whose first element is uprobe_pthread_upump_mgr */
static struct uprobe *uprobe_main = NULL;
/* probe for demux */
struct uprobe *uprobe_dejitter = NULL;
/* probe for source worker pipe */
struct uprobe uprobe_src_s;
/* probe for demux video subpipe */
struct uprobe uprobe_video_s;
/* probe for demux audio subpipe */
struct uprobe uprobe_audio_s;
/* source thread */
struct upipe_mgr *upipe_wsrc_mgr = NULL;
/* decoder thread */
struct upipe_mgr *upipe_wlin_mgr = NULL;
/* sink thread */
struct upipe_mgr *upipe_wsink_mgr = NULL;
/* play */
static struct upipe *play = NULL;
/* trick play */
struct upipe *trickp = NULL;
/* source pipe */
struct upipe *upipe_src = NULL;
/* video sink */
struct upipe *video_sink = NULL;
/* audio sink */
struct upipe *audio_sink = NULL;
/** true if we got the DidChangeView event */
static bool inited = false;

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
    upipe_sws_set_flags(ffmt, SWS_FAST_BILINEAR);

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

    upipe_set_output(avcdec, video_sink);
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

    struct upipe_mgr *ffmt_mgr = upipe_ffmt_mgr_alloc();
    struct upipe_mgr *swr_mgr = upipe_swr_mgr_alloc();
    upipe_ffmt_mgr_set_swr_mgr(ffmt_mgr, swr_mgr);
    upipe_mgr_release(swr_mgr);

    struct uref *uref = uref_sibling_alloc(flow_def);
    uref_flow_set_def(uref, "sound.");

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

    upipe_set_output(avcdec, audio_sink);
    upipe_release(avcdec);
    return UBASE_ERR_NONE;
}

/* generic source probe */
static int catch_src(struct uprobe *uprobe, struct upipe *upipe,
                     int event, va_list args)
{
    if (event == UPROBE_SOURCE_END && main_upump_mgr != NULL) {
        upipe_dbg(upipe, "caught source end, dying");
        demo_stop();
        return UBASE_ERR_NONE;
    }
    return uprobe_throw_next(uprobe, upipe, event, args);
}

static int demo_start(const char *uri, const char *relay, const char *mode)
{
    uprobe_notice_va(uprobe_main, NULL, "running URI %s", uri);
    /* try opening a socket */
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1)
        return UBASE_ERR_EXTERNAL;

    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = 0;
    int err = bind(fd, (struct sockaddr *)&sin, sizeof(struct sockaddr_in));
    close(fd);
    if (err == -1)
        return UBASE_ERR_EXTERNAL;

    bool need_trickp = false;
    unsigned int src_out_queue_length = SRC_OUT_QUEUE_LENGTH;
    uprobe_throw(uprobe_main, NULL, UPROBE_FREEZE_UPUMP_MGR);

    struct uprobe *uprobe_src = uprobe_xfer_alloc(uprobe_use(uprobe_main));
    uprobe_xfer_add(uprobe_src, UPROBE_XFER_VOID, UPROBE_SOURCE_END, 0);

    uprobe_dejitter_set(uprobe_dejitter, DEJITTER_DIVIDER);

    if (!strcmp(mode, "udp")) {
        struct upipe_mgr *upipe_udpsrc_mgr = upipe_udpsrc_mgr_alloc();
        assert(upipe_udpsrc_mgr != NULL);
        upipe_src = upipe_void_alloc(upipe_udpsrc_mgr,
                uprobe_pfx_alloc(uprobe_src,
                                 UPROBE_LOG_VERBOSE, "udpsrc"));
        upipe_mgr_release(upipe_udpsrc_mgr);

        if (upipe_src == NULL || !ubase_check(upipe_set_uri(upipe_src, uri)))
            return UBASE_ERR_EXTERNAL;
        upipe_attach_uclock(upipe_src);

    } else {
        struct upipe_mgr *upipe_amtsrc_mgr = upipe_amtsrc_mgr_alloc(relay);
        assert(upipe_amtsrc_mgr != NULL);
        upipe_src = upipe_void_alloc(upipe_amtsrc_mgr,
                uprobe_pfx_alloc(uprobe_src,
                                 UPROBE_LOG_VERBOSE, "amtsrc"));
        upipe_mgr_release(upipe_amtsrc_mgr);

        char real_uri[strlen(uri) + strlen(mode) + sizeof("://")];
        sprintf(real_uri, "%s://%s", mode, uri);
        if (upipe_src == NULL ||
            !ubase_check(upipe_set_uri(upipe_src, real_uri)))
            return UBASE_ERR_EXTERNAL;
        upipe_attach_uclock(upipe_src);

        struct upipe_mgr *rtpd_mgr = upipe_rtpd_mgr_alloc();
        struct upipe *rtpd = upipe_void_alloc_output(upipe_src, rtpd_mgr,
                uprobe_pfx_alloc(uprobe_use(uprobe_main),
                                 UPROBE_LOG_VERBOSE, "rtpd"));
        assert(rtpd != NULL);
        upipe_release(rtpd);
        upipe_mgr_release(rtpd_mgr);
    }

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
                            UPROBE_SELFLOW_PIC, "auto"),
                        uprobe_use(&uprobe_audio_s),
                        UPROBE_SELFLOW_SOUND, "auto"),
                    UPROBE_SELFLOW_VOID, "auto"),
                UPROBE_LOG_VERBOSE, "ts demux"));
    upipe_release(ts_demux);
    upipe_mgr_release(upipe_ts_demux_mgr);

    /* ev-loop */
    return UBASE_ERR_NONE;
}

static void demo_stop(void)
{
    uprobe_notice(uprobe_main, NULL, "stopping");
    if (upipe_src != NULL) {
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
    upipe_release(trickp);
    trickp = NULL;
    upipe_release(play);
    play = NULL;
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

/**
 * Create a new PP_Var from a C string.
 * @param[in] str The string to convert.
 * @return A new PP_Var with the contents of |str|.
 */
static struct PP_Var CStrToVar(const char* str)
{
    if (ppb_var_interface != NULL) {
        return ppb_var_interface->VarFromUtf8(str, strlen(str));
    }
    return PP_MakeUndefined();
}

/**
 * Convert a PP_Var to a C string, given a buffer.
 * @param[in] var The PP_Var to convert.
 * @param[out] buffer The buffer to write to.
 * @param[in] length The length of |buffer|.
 * @return The number of characters written.
 */
static uint32_t VarToCStr(struct PP_Var var, char* buffer, uint32_t length)
{
    if (ppb_var_interface != NULL) {
        uint32_t var_length;
        const char* str = ppb_var_interface->VarToUtf8(var, &var_length);
        /* str is NOT NULL-terminated. Copy using memcpy. */
        uint32_t min_length = MIN(var_length, length - 1);
        memcpy(buffer, str, min_length);
        buffer[min_length] = 0;

        return min_length;
    }

    return 0;
}

static void upipe_process_event(PSEvent *event)
{
    switch(event->type) {
        /* If the view updates, build a new Graphics 2D Context */
        case PSE_INSTANCE_DIDCHANGEVIEW: {
            printf("UpdateContext\n");
            struct PP_Rect rect;
            ppb_view_interface->GetRect(event->as_resource, &rect);
            char option[256];
            sprintf(option, "%ux%u", rect.size.width, rect.size.height);
            upipe_set_option(video_sink, "size", option);
            inited = true;
            break;
        }

        case PSE_INSTANCE_HANDLEMESSAGE: {
            struct PP_Var var = event->as_var;
            if (var.type == PP_VARTYPE_DICTIONARY) {
                struct PP_Var message = ppb_var_dictionary_interface->Get(var,
                        CStrToVar("message"));
                char message_string[256];
                VarToCStr(message, message_string, sizeof(message_string));
                if (!strcmp(message_string, "set_uri")) {
                    struct PP_Var value = ppb_var_dictionary_interface->Get(var,
                            CStrToVar("value"));
                    char value_string[256];
                    VarToCStr(value, value_string, sizeof(value_string));

                    struct PP_Var relay = ppb_var_dictionary_interface->Get(var,
                            CStrToVar("relay"));
                    char relay_string[256];
                    VarToCStr(relay, relay_string, sizeof(relay_string));

                    struct PP_Var mode = ppb_var_dictionary_interface->Get(var,
                            CStrToVar("mode"));
                    char mode_string[256];
                    VarToCStr(mode, mode_string, sizeof(mode_string));

                    int err;
                    if (!inited)
                        err = UBASE_ERR_EXTERNAL;
                    else
                        err = demo_start(value_string, relay_string,
                                         mode_string);
                    if (!ubase_check(err)) {
                        /* send error message */
                        char error[256];
                        sprintf(error, "error:%d", err);
                        struct PP_Var pp_message =
                            ppb_var_interface->VarFromUtf8(error,
                                                           strlen(error));
                        ppb_messaging_interface->PostMessage(PSGetInstanceId(),
                                                             pp_message);
                    }
                } else if (!strcmp(message_string, "stop")) {
                    demo_stop();
                } else if (!strcmp(message_string, "quit")) {
                    demo_stop();
                    upump_stop(event_upump);
                    upipe_release(video_sink);
                    upipe_release(audio_sink);
                    upipe_mgr_release(upipe_wsrc_mgr);
                    upipe_mgr_release(upipe_wlin_mgr);
                }
            }
            break;
        }
        default:
            break;
    }
}

static void upipe_event_timer(struct upump *upump)
{
    PSEventSetFilter(PSE_ALL);
    PSEvent *event;
    while ((event = PSEventTryAcquire()) != NULL) {
        upipe_process_event(event);
        PSEventRelease(event);
    }
}

int upipe_demo(int argc, char *argv[])
{
    printf("upipe_demo running\n");

    /* PPAPI Interfaces */
    ppb_view_interface =
        (PPB_View *)PSGetInterface(PPB_VIEW_INTERFACE);
    ppb_var_interface =
        (PPB_Var *)PSGetInterface(PPB_VAR_INTERFACE);
    ppb_message_loop_interface =
        (PPB_MessageLoop *)PSGetInterface(PPB_MESSAGELOOP_INTERFACE);
    ppb_messaging_interface =
        (PPB_Messaging *)PSGetInterface(PPB_MESSAGING_INTERFACE);
    ppb_var_dictionary_interface =
        (PPB_VarDictionary *)PSGetInterface(PPB_VAR_DICTIONARY_INTERFACE);

    /* upipe env */
    loop = ev_default_loop(0);
    main_upump_mgr = upump_ev_mgr_alloc(loop, UPUMP_POOL, UPUMP_BLOCKER_POOL);
    assert(main_upump_mgr != NULL);
    struct umem_mgr *umem_mgr = umem_pool_mgr_alloc_simple(UMEM_POOL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    udict_mgr_release(udict_mgr);
    struct uclock *uclock = uclock_std_alloc(0);

    /* default probe */
    uprobe_main = uprobe_stdio_alloc(NULL, stdout, UPROBE_LOG_LEVEL);
    uprobe_main = uprobe_uclock_alloc(uprobe_main, uclock);
    uprobe_main = uprobe_uref_mgr_alloc(uprobe_main, uref_mgr);
    uprobe_main = uprobe_ubuf_mem_pool_alloc(uprobe_main, umem_mgr,
                                             UBUF_POOL_DEPTH, UBUF_POOL_DEPTH);
    uprobe_main = uprobe_pthread_upump_mgr_alloc(uprobe_main);
    uref_mgr_release(uref_mgr);
    uclock_release(uclock);
    umem_mgr_release(umem_mgr);
    uprobe_pthread_upump_mgr_set(uprobe_main, main_upump_mgr);

    /* probes audio & video & dejitter */
    uprobe_dejitter = uprobe_dejitter_alloc(uprobe_use(uprobe_main),
                                            DEJITTER_DIVIDER);
    uprobe_init(&uprobe_src_s, catch_src, uprobe_use(uprobe_main));
    uprobe_init(&uprobe_video_s, catch_video, uprobe_use(uprobe_dejitter));
    uprobe_init(&uprobe_audio_s, catch_audio, uprobe_use(uprobe_dejitter));

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

    /* upipe_nacl_graphics2d */
    struct upipe_mgr *nacl_g2d_mgr = upipe_nacl_g2d_mgr_alloc();
    video_sink = upipe_void_alloc(nacl_g2d_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_main), UPROBE_LOG_VERBOSE,
                            "nacl g2d"));
    assert(video_sink != NULL);
    upipe_mgr_release(nacl_g2d_mgr);
    upipe_attach_uclock(video_sink);

    /* upipe_nacl_audio */
    struct upipe_mgr *nacl_audio_mgr = upipe_nacl_audio_mgr_alloc();
    audio_sink = upipe_void_alloc(nacl_audio_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_main), UPROBE_LOG_VERBOSE,
                             "nacl audio"));
    upipe_mgr_release(nacl_audio_mgr);
    upipe_attach_uclock(audio_sink);

    /* NaCl event loop */
    event_upump = upump_alloc_timer(main_upump_mgr, upipe_event_timer, NULL,
                                    0, UCLOCK_FREQ / 25);
    assert(event_upump != NULL);
    upump_start(event_upump);

    /* wait for an event asking to open a URI */
    printf("entering event loop\n");
    ev_loop(loop, 0);
    printf("exiting event loop\n");

    /* release & free */
    upump_free(event_upump);
    upump_mgr_release(main_upump_mgr);
    uprobe_release(uprobe_dejitter);
    uprobe_release(uprobe_main);

    uprobe_clean(&uprobe_src_s);
    uprobe_clean(&uprobe_video_s);
    uprobe_clean(&uprobe_audio_s);

    upipe_av_clean();

    ev_default_destroy();
    printf("upipe_demo exiting\n");
    return 0;
}

PPAPI_SIMPLE_REGISTER_MAIN(upipe_demo);
