/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
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


#undef NDEBUG
#include "GLES2/gl2.h"

#include <upipe/ubase.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/ubuf_pic.h>
#include <upipe/ubuf_pic_mem.h>
#include <upipe/uclock.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/umem.h>
#include <upipe/umem_pool.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_source_read_size.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_dejitter.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/uprobe_output.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_upump_mgr.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_select_flows.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_uclock.h>
#include <upipe/upump.h>
#include <upipe/uref.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_dump.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_std.h>
#include <upipe/uclock.h>
#include <upipe/uclock_std.h>
#include <upipe/uprobe_transfer.h>
#include <upipe-pthread/upipe_pthread_transfer.h>
#include <upipe-pthread/uprobe_pthread_upump_mgr.h>
#include <upump-ev/upump_ev.h>

#include <upipe-av/upipe_av.h>
#include <upipe-av/upipe_avcodec_decode.h>
#include <upipe-av/upipe_avcodec_encode.h>

#include <upipe-swscale/upipe_sws.h>

#include <upipe-nacl/upipe_filter_ebur128.h>

#include <upipe-ts/upipe_ts_demux.h>

#include <upipe-framers/upipe_mpgv_framer.h>
#include <upipe-framers/upipe_h264_framer.h>
#include <upipe-framers/upipe_mpga_framer.h>
#include <upipe-framers/upipe_a52_framer.h>

#include <upump-ev/upump_ev.h>

#include <upipe-ebur128/ebur128.h>
#include <upipe-modules/upipe_file_sink.h>
#include <upipe-modules/upipe_file_source.h>
#include <upipe-modules/upipe_udp_source.h>
#include <upipe-modules/upipe_http_source.h>
#include <upipe-modules/upipe_queue_sink.h>
#include <upipe-modules/upipe_queue_source.h>
#include <upipe-modules/upipe_probe_uref.h>
#include <upipe-modules/upipe_null.h>
#include <upipe-modules/upipe_transfer.h>
#include <upipe-modules/upipe_worker_source.h>
#include <upipe-modules/upipe_worker_linear.h>
#include <upipe-modules/upipe_worker_sink.h>
#include <upipe-modules/upipe_trickplay.h>
#include <upipe-nacl/upipe_display.h>
#include <upipe-nacl/upipe_sound.h>
#include <upipe-nacl/upipe_src_udp_chrome.h>
#include <upipe-nacl/upipe_src_tcp_chrome.h>
#include <upipe-amt/upipe_amt_source.h>

#include <ppapi/c/pp_errors.h>
#include <ppapi/c/pp_module.h>
#include <ppapi/c/pp_resource.h>
#include <ppapi/c/pp_var.h>
#include <ppapi/c/ppp.h>
#include <ppapi/c/ppp_instance.h>
#include <ppapi/c/ppp_messaging.h>
#include <ppapi/c/ppb.h>
#include <ppapi/c/ppb_audio.h>
#include <ppapi/c/ppb_audio_config.h>
#include <ppapi/c/ppb_core.h>
#include <ppapi/c/ppb_fullscreen.h>
#include <ppapi/c/ppb_graphics_2d.h>
#include <ppapi/c/ppb_graphics_3d.h>
#include <ppapi/c/ppb_image_data.h>
#include <ppapi/c/ppb_input_event.h>
#include <ppapi/c/ppb_instance.h>
#include <ppapi/c/ppb_message_loop.h>
#include <ppapi/c/ppb_messaging.h>
#include <ppapi/c/ppb_net_address.h>
#include <ppapi/c/ppb_opengles2.h>
#include <ppapi/c/ppb_tcp_socket.h>
#include <ppapi/c/ppb_udp_socket.h>
#include <ppapi/c/ppb_var.h>
#include <ppapi/c/ppb_var_dictionary.h>
#include <ppapi/c/ppb_view.h>
#include <ppapi/c/ppb_websocket.h>
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
#include <sys/mount.h>
#include <sys/param.h>
#include <signal.h>
#include <assert.h>
#include <time.h>

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
#define UPROBE_LOG_LEVEL        UPROBE_LOG_DEBUG

#ifndef GLES
    #define GLES false
#endif
#define SWS_RGB !GLES

struct Context g_Context;
void UpdateContext(uint32_t width, uint32_t height);
void ProcessEvent(PSEvent* event);
void *thread_main(void* user_data);

/* PPAPI Interfaces */
PPB_Core* ppb_core_interface= NULL;
PPB_Instance* ppb_instance_interface = NULL;
PPB_Var* ppb_var_interface = NULL;
PPB_Messaging* ppb_messaging_interface = NULL;
PPB_InputEvent* ppb_input_event_interface = NULL;
PPB_MessageLoop* ppb_message_loop_interface = NULL;
PPB_Graphics2D* ppb_graphic2d_interface =NULL;
PPB_Graphics3D* ppb_graphic3d_interface =NULL;
PPB_ImageData* ppb_imagedata_interface = NULL;
PPB_View* ppb_view_interface = NULL;
PPB_AudioConfig* ppb_audio_config_interface = NULL;
PPB_Audio* ppb_audio_interface = NULL;
PPB_NetAddress* ppb_net_address_interface = NULL;
PPB_UDPSocket* ppb_udp_socket_interface = NULL;
PPB_TCPSocket* ppb_tcp_socket_interface = NULL;
PPB_WebSocket* ppb_web_socket_interface = NULL;
struct PPB_OpenGLES2* ppb_open_gles2_interface = NULL;
PPB_VarDictionary* ppb_var_dictionary_interface = NULL;

/* main event loop */
static struct ev_loop *loop;
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
/* probe for glx sink */
struct uprobe uprobe_glx_s;
/* source thread */
struct upipe_mgr *upipe_wsrc_mgr = NULL;
/* decoder thread */
struct upipe_mgr *upipe_wlin_mgr = NULL;
/* sink thread */
struct upipe_mgr *upipe_wsink_mgr = NULL;
/* trick play */
struct upipe *trickp = NULL;
/* source pipe */
struct upipe *upipe_src = NULL;
/* video sink */
struct upipe *video_sink = NULL;
/* audio sink */
struct upipe *audio_sink = NULL;

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

    /* TODO: write a bin pipe with deint and sws */
    struct upipe_mgr *upipe_avcdec_mgr = upipe_avcdec_mgr_alloc();
    struct upipe *avcdec = upipe_void_alloc(upipe_avcdec_mgr,
            uprobe_pfx_alloc(uprobe_output_alloc(uprobe_use(uprobe_main)),
                             UPROBE_LOG_VERBOSE, "avcdec video"));
    assert(avcdec != NULL);
    upipe_mgr_release(upipe_avcdec_mgr);
    upipe_set_option(avcdec, "threads", "4");

    struct uref *uref = uref_sibling_alloc(flow_def);
    uref_flow_set_def(uref, "pic.");
    uref_pic_flow_set_macropixel(uref, 1);
    uref_pic_flow_set_planes(uref, 0);
    uref_pic_flow_add_plane(uref, 1, 1, 3, "r8g8b8");
    uref_pic_flow_set_hsize(uref, g_Context.size.width);
    uref_pic_flow_set_vsize(uref, g_Context.size.height);
    struct upipe_mgr *upipe_sws_mgr = upipe_sws_mgr_alloc();
    struct upipe *yuvrgb = upipe_flow_alloc_output(avcdec, upipe_sws_mgr,
            uprobe_pfx_alloc(uprobe_output_alloc(uprobe_use(uprobe_main)),
                             UPROBE_LOG_VERBOSE, "rgb"),
            uref);
    assert(yuvrgb != NULL);
    uref_free(uref);
    upipe_mgr_release(upipe_sws_mgr);
    upipe_release(yuvrgb);

    /* deport to the decoder thread */
    avcdec = upipe_wlin_alloc(upipe_wlin_mgr,
            uprobe_pfx_alloc(uprobe_output_alloc(uprobe_use(uprobe_main)),
                             UPROBE_LOG_VERBOSE, "wlin video"),
            avcdec,
            uprobe_pfx_alloc(uprobe_use(uprobe_main),
                             UPROBE_LOG_VERBOSE, "wlin_x video"),
            DEC_IN_QUEUE_LENGTH, DEC_OUT_QUEUE_LENGTH);
    assert(avcdec != NULL);
    upipe_set_output(upipe, avcdec);

    if (trickp != NULL)
        avcdec = upipe_void_chain_output_sub(avcdec, trickp,
                uprobe_pfx_alloc(uprobe_output_alloc(uprobe_use(uprobe_main)),
                                 UPROBE_LOG_VERBOSE, "trickp video"));

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
            uprobe_pfx_alloc(uprobe_output_alloc(uprobe_use(uprobe_main)),
                             UPROBE_LOG_VERBOSE, "avcdec audio"));
    assert(avcdec != NULL);
    upipe_mgr_release(upipe_avcdec_mgr);

    /* deport to the decoder thread */
    avcdec = upipe_wlin_alloc(upipe_wlin_mgr,
            uprobe_pfx_alloc(uprobe_output_alloc(uprobe_use(uprobe_main)),
                             UPROBE_LOG_VERBOSE, "wlin audio"),
            avcdec,
            uprobe_pfx_alloc(uprobe_use(uprobe_main),
                             UPROBE_LOG_VERBOSE, "wlin_x audio"),
            DEC_IN_QUEUE_LENGTH, DEC_OUT_QUEUE_LENGTH);
    assert(avcdec != NULL);
    upipe_set_output(upipe, avcdec);

    if (trickp != NULL)
        avcdec = upipe_void_chain_output_sub(avcdec, trickp,
                uprobe_pfx_alloc(uprobe_output_alloc(uprobe_use(uprobe_main)),
                                 UPROBE_LOG_VERBOSE, "trickp audio"));

    upipe_set_output(avcdec, audio_sink);
    upipe_release(avcdec);
    return UBASE_ERR_NONE;
}

/* generic source probe */
static int catch_src(struct uprobe *uprobe, struct upipe *upipe,
                     int event, va_list args)
{
    if (event == UPROBE_SOURCE_END && main_upump_mgr != NULL) {
        upipe_dbg(upipe, "caught source end, dying (or not)");
#if 0 /* FIXME */
        struct upump *idler_stop = upump_alloc_idler(main_upump_mgr,
                                                     demo_stop, (void *)0);
        upump_start(idler_stop);
#endif
        return UBASE_ERR_NONE;
    }
    return uprobe_throw_next(uprobe, upipe, event, args);
}

static void demo_start(const char *uri, const char *relay, const char *mode)
{
    uprobe_notice_va(uprobe_main, NULL, "running URI %s", uri);
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
                uprobe_pfx_alloc(uprobe_output_alloc(uprobe_src),
                                 UPROBE_LOG_VERBOSE, "udpsrc"));
        upipe_mgr_release(upipe_udpsrc_mgr);

        if (upipe_src == NULL || !ubase_check(upipe_set_uri(upipe_src, uri)))
            return;
        upipe_attach_uclock(upipe_src);

    } else {
        struct upipe_mgr *upipe_amtsrc_mgr = upipe_amtsrc_mgr_alloc(relay);
        assert(upipe_amtsrc_mgr != NULL);
        upipe_src = upipe_void_alloc(upipe_amtsrc_mgr,
                uprobe_pfx_alloc(uprobe_output_alloc(uprobe_src),
                                 UPROBE_LOG_VERBOSE, "amtsrc"));
        upipe_mgr_release(upipe_amtsrc_mgr);

        char real_uri[strlen(uri) + strlen(mode) + sizeof("://")];
        sprintf(real_uri, "%s://%s", mode, uri);
        if (upipe_src == NULL ||
            !ubase_check(upipe_set_uri(upipe_src, real_uri)))
            return;
        upipe_attach_uclock(upipe_src);
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

    /* deport to the source thread */
    upipe_src = upipe_wsrc_alloc(upipe_wsrc_mgr,
            uprobe_pfx_alloc(uprobe_output_alloc(uprobe_use(&uprobe_src_s)),
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
                            uprobe_output_alloc(uprobe_use(&uprobe_video_s)),
                            UPROBE_SELFLOW_PIC, "auto"),
                        uprobe_output_alloc(uprobe_use(&uprobe_audio_s)),
                        UPROBE_SELFLOW_SOUND, "auto"),
                    UPROBE_SELFLOW_VOID, "auto"),
                UPROBE_LOG_VERBOSE, "ts demux"));
    upipe_release(ts_demux);
    upipe_mgr_release(upipe_ts_demux_mgr);

    /* ev-loop */
    ev_loop(loop, 0);
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

int upipe_demo(int argc, char *argv[]) {
    printf("example_main running\n");
    /* PPAPI Interfaces */
    ppb_core_interface = (PPB_Core*)PSGetInterface(PPB_CORE_INTERFACE);
    ppb_graphic2d_interface = (PPB_Graphics2D*)PSGetInterface(PPB_GRAPHICS_2D_INTERFACE);
    ppb_graphic3d_interface = (PPB_Graphics3D*)PSGetInterface(PPB_GRAPHICS_3D_INTERFACE);
    ppb_instance_interface = (PPB_Instance*)PSGetInterface(PPB_INSTANCE_INTERFACE);
    ppb_imagedata_interface = (PPB_ImageData*)PSGetInterface(PPB_IMAGEDATA_INTERFACE);
    ppb_view_interface = (PPB_View*)PSGetInterface(PPB_VIEW_INTERFACE);
    ppb_var_interface = (PPB_Var*)PSGetInterface(PPB_VAR_INTERFACE);
    ppb_input_event_interface = (PPB_InputEvent*) PSGetInterface(PPB_INPUT_EVENT_INTERFACE);
    ppb_message_loop_interface = (PPB_MessageLoop*)PSGetInterface(PPB_MESSAGELOOP_INTERFACE);
    ppb_messaging_interface = (PPB_Messaging*)PSGetInterface(PPB_MESSAGING_INTERFACE);
    ppb_udp_socket_interface = (PPB_UDPSocket*)PSGetInterface(PPB_UDPSOCKET_INTERFACE);
    ppb_tcp_socket_interface = (PPB_TCPSocket*)PSGetInterface(PPB_TCPSOCKET_INTERFACE);
    ppb_web_socket_interface = (PPB_WebSocket*)PSGetInterface(PPB_WEBSOCKET_INTERFACE);
    ppb_net_address_interface = (PPB_NetAddress*)PSGetInterface(PPB_NETADDRESS_INTERFACE);
    ppb_audio_config_interface = (PPB_AudioConfig*)PSGetInterface(PPB_AUDIO_CONFIG_INTERFACE);
    ppb_audio_interface = (PPB_Audio*)PSGetInterface(PPB_AUDIO_INTERFACE);
    ppb_open_gles2_interface = (struct PPB_OpenGLES2*)PSGetInterface(PPB_OPENGLES2_INTERFACE);
    ppb_var_dictionary_interface = (PPB_VarDictionary*)PSGetInterface(PPB_VAR_DICTIONARY_INTERFACE);

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

    /*default probe */
    uprobe_main = uprobe_stdio_alloc(NULL, stdout, UPROBE_LOG_LEVEL);
    uprobe_main = uprobe_uclock_alloc(uprobe_main, uclock);
    uprobe_main = uprobe_uref_mgr_alloc(uprobe_main, uref_mgr);
    uprobe_main = uprobe_ubuf_mem_alloc(uprobe_main, umem_mgr, UBUF_POOL_DEPTH,
                                        UBUF_POOL_DEPTH);
    uprobe_main = uprobe_pthread_upump_mgr_alloc(uprobe_main);
    uref_mgr_release(uref_mgr);
    uclock_release(uclock);
    umem_mgr_release(umem_mgr);
    uprobe_pthread_upump_mgr_set(uprobe_main, main_upump_mgr);

    /* probes audio & video & dejitter*/
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

    PSEventSetFilter(PSE_ALL);
    while (!g_Context.bound) {
        PSEvent* event;
        while ((event = PSEventTryAcquire()) != NULL) {
            ProcessEvent(event);
            PSEventRelease(event);
        }
    }

    PP_Resource display_loop =
        ppb_message_loop_interface->Create(PSGetInstanceId());

    struct thread_data *display_threadData = malloc(sizeof(struct thread_data));
    display_threadData->loop = display_loop;
    display_threadData->message_loop_interface = ppb_message_loop_interface;
    display_threadData->instance_id = PSGetInstanceId();
    pthread_t display_thread;
    pthread_create(&display_thread, NULL, &thread_main, display_threadData);

    PP_Resource image = ppb_imagedata_interface->Create(PSGetInstanceId(),
            PP_IMAGEDATAFORMAT_BGRA_PREMUL, &(g_Context.size), PP_FALSE);

    /* upipe_display */
    struct upipe_mgr *display_mgr = upipe_display_mgr_alloc();
    video_sink = _upipe_display_alloc(display_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_main), UPROBE_LOG_VERBOSE,
                            "display"), image, display_loop);
    assert(video_sink != NULL);
    upipe_mgr_release(display_mgr);
    upipe_attach_uclock(video_sink);

    upipe_display_set_hposition(video_sink, 0);
    upipe_display_set_vposition(video_sink, 0);
    upipe_display_set_context(video_sink, g_Context);

    PP_Resource sound_loop =
        ppb_message_loop_interface->Create(PSGetInstanceId());

    struct thread_data *sound_threadData = malloc(sizeof(struct thread_data));
    sound_threadData->loop = sound_loop;
    sound_threadData->message_loop_interface = ppb_message_loop_interface;
    sound_threadData->instance_id = PSGetInstanceId();
    pthread_t sound_thread;
    pthread_create(&sound_thread, NULL, &thread_main, sound_threadData);

    /* upipe_sound */
    struct upipe_mgr *sound_mgr = upipe_sound_mgr_alloc();
    audio_sink = _upipe_sound_alloc(sound_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_main), UPROBE_LOG_LEVEL,
                             "sound"), sound_loop);

    /* wait for an event asking to open a URI */
    printf("entering event loop\n");

    while (!g_Context.quit) {
        PSEvent* event;
        while ((event = PSEventWaitAcquire()) != NULL) {
            ProcessEvent(event);
            PSEventRelease(event);
        }
    }
    printf("exiting event loop\n");

    /* release & free*/
    upump_mgr_release(main_upump_mgr);
    uprobe_release(uprobe_dejitter);
    uprobe_release(uprobe_main);

    ppb_message_loop_interface->PostQuit(display_loop, PP_FALSE);
    ppb_core_interface->ReleaseResource(display_loop);
    ppb_message_loop_interface->PostQuit(sound_loop, PP_FALSE);
    ppb_core_interface->ReleaseResource(sound_loop);
    ppb_core_interface->ReleaseResource(image);
    ev_default_destroy();
    return 0;
}

void UpdateContext(uint32_t width, uint32_t height) {
    printf("UpdateContext\n");
    if (width != g_Context.size.width || height != g_Context.size.height) {
        size_t size = width * height;
        size_t index;

        free(g_Context.cell_in);
        free(g_Context.cell_out);
        /* Create a new context */
        g_Context.cell_in = (uint8_t*) malloc(size);
        g_Context.cell_out = (uint8_t*) malloc(size);

        memset(g_Context.cell_out, 0, size);
        for (index = 0; index < size; index++) {
            g_Context.cell_in[index] = rand() & 1;
        }
    }

    /* Recreate the graphics context on a view change */
    ppb_core_interface->ReleaseResource(g_Context.ctx);
    g_Context.size.width = width;
    g_Context.size.height = height;
    #if GLES
    /*
    const int32_t attributes[] = {
    PP_GRAPHICS3DATTRIB_ALPHA_SIZE, 0,
    PP_GRAPHICS3DATTRIB_BLUE_SIZE, 8,
    PP_GRAPHICS3DATTRIB_GREEN_SIZE, 8,
    PP_GRAPHICS3DATTRIB_RED_SIZE, 8,
    PP_GRAPHICS3DATTRIB_DEPTH_SIZE, 24,
    PP_GRAPHICS3DATTRIB_STENCIL_SIZE, 0,
    PP_GRAPHICS3DATTRIB_SAMPLES, 0,
    PP_GRAPHICS3DATTRIB_SAMPLE_BUFFERS, 0,
    PP_GRAPHICS3DATTRIB_WIDTH, width,
    PP_GRAPHICS3DATTRIB_HEIGHT, height,
    PP_GRAPHICS3DATTRIB_NONE,
    };
    */
    const int32_t attributes[] = {
      PP_GRAPHICS3DATTRIB_ALPHA_SIZE, 8,
      PP_GRAPHICS3DATTRIB_DEPTH_SIZE, 24,
      PP_GRAPHICS3DATTRIB_WIDTH, width,
      PP_GRAPHICS3DATTRIB_HEIGHT, height,
      PP_GRAPHICS3DATTRIB_NONE
    };

    g_Context.ctx = ppb_graphic3d_interface->Create(PSGetInstanceId(), 0, attributes);
    #else
    g_Context.ctx = ppb_graphic2d_interface->Create(PSGetInstanceId(), &g_Context.size, PP_TRUE);
    #endif
    g_Context.bound = ppb_instance_interface->BindGraphics(PSGetInstanceId(), g_Context.ctx);
}

/**
 * Create a new PP_Var from a C string.
 * @param[in] str The string to convert.
 * @return A new PP_Var with the contents of |str|.
 */
struct PP_Var CStrToVar(const char* str) {
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
uint32_t VarToCStr(struct PP_Var var, char* buffer, uint32_t length) {
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

void ProcessEvent(PSEvent* event) {
    switch(event->type) {
        /* If the view updates, build a new Graphics 2D Context */
        case PSE_INSTANCE_DIDCHANGEVIEW: {
            struct PP_Rect rect;
            ppb_view_interface->GetRect(event->as_resource, &rect);
            UpdateContext(rect.size.width, rect.size.height);
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
                    struct PP_Var value = ppb_var_dictionary_interface->Get(var, CStrToVar("value"));
                    char value_string[256];
                    VarToCStr(value, value_string, sizeof(value_string));

                    struct PP_Var relay = ppb_var_dictionary_interface->Get(var, CStrToVar("relay"));
                    char relay_string[256];
                    VarToCStr(relay, relay_string, sizeof(relay_string));

                    struct PP_Var mode = ppb_var_dictionary_interface->Get(var, CStrToVar("mode"));
                    char mode_string[256];
                    VarToCStr(mode, mode_string, sizeof(mode_string));

                    demo_start(value_string, relay_string, mode_string);
                }
            }
            break;
        }
        default:
            break;
    }
} 

/*Attach & run a message loop in a thread*/
void* thread_main(void* user_data) {
    struct thread_data* data = (struct thread_data*)(user_data);
    data->message_loop_interface->AttachToCurrentThread(data->loop);
    data->message_loop_interface->Run(data->loop);
    return NULL;
}
PPAPI_SIMPLE_REGISTER_MAIN(upipe_demo);
