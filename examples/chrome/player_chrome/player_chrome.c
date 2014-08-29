/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
 *
 * Authors: Xavier Boulet
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
#include <upipe/umem_alloc.h>
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
#include <upipe-modules/upipe_queue_sink.h>
#include <upipe-modules/upipe_queue_source.h>
#include <upipe-modules/upipe_probe_uref.h>
#include <upipe-modules/upipe_null.h>
#include <upipe-modules/upipe_trickplay.h>
#include <upipe-nacl/upipe_display.h>
#include <upipe-nacl/upipe_sound.h>
#include <upipe-nacl/upipe_src_udp_chrome.h>
#include <upipe-nacl/upipe_src_tcp_chrome.h>

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

#include "nacl_io/nacl_io.h"
#define UPUMP_POOL          1
#define UPUMP_BLOCKER_POOL  1
#define UDICT_POOL_DEPTH    5
#define UREF_POOL_DEPTH     5
#define UBUF_POOL_DEPTH     5
#define WIDTH               80
#define HEIGHT              80
#define LIMIT               10
#define UBUF_PREPEND        2
#define UBUF_APPEND         2
#define UBUF_ALIGN          16
#define UBUF_ALIGN_HOFFSET  0
#define DEJITTER_DIVIDER        100
#define UPROBE_LOG_LEVEL    5

#define UDP true
#define TCP false

#define NB_SRC 2
#ifndef GLES
    #define GLES false
#endif
#define SWS_RGB !GLES

struct Context g_Context;
void UpdateContext(uint32_t width, uint32_t height);
void ProcessEvent(PSEvent* event);
void* thread_main(void* user_data);

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

/*Probes & managers & pipes*/
struct uprobe* logger;
struct uprobe *uprobe_catch_video;
struct uprobe *uprobe_catch_audio;
struct uprobe *uprobe_uref_video;
struct uprobe *uprobe_uref_audio;
struct uprobe *uprobe_avcdec_video;
struct uprobe *uprobe_avcdec_audio;
struct uprobe* uprobe_dejitter;

struct upipe_mgr **upipe_filter_ebur128_mgr;
struct upipe_mgr *upipe_avcdec_video_mgr;
struct upipe_mgr *upipe_avcdec_audio_mgr;
struct upipe_mgr *upipe_avcenc_mgr;
struct upipe_mgr *upipe_sws_mgr;
struct upipe_mgr *upipe_probe_uref_video_mgr;
struct upipe_mgr *upipe_probe_uref_audio_mgr;
struct uref_mgr *uref_mgr;

struct upipe *upipe_source;
struct upipe **r128;
struct upipe *upipe_split_video_output = NULL;
struct upipe *upipe_split_audio_output = NULL;
struct upipe **display;
struct upipe **sound;
struct upipe *upipe_null;

/** catch probes from uprobe_uref_video */
static int uref_catch_video(struct uprobe *uprobe, struct upipe *upipe,
                      int event, va_list args)
{
    if (event != UPROBE_PROBE_UREF){
        return uprobe_throw_next(uprobe, upipe, event, args);}

    va_list args_copy;
    va_copy(args_copy, args);
    unsigned int signature = va_arg(args_copy, unsigned int);
    va_end(args_copy);
    if (signature != UPIPE_PROBE_UREF_SIGNATURE)
        return uprobe_throw_next(uprobe, upipe, event, args);

    int i=0;
    for(i=0; i<NB_SRC; i++)
    {
        if(uprobe == &uprobe_uref_video[i])
        {
            upipe_set_output(upipe, display[i]);
        }
    }
    return UBASE_ERR_NONE;
}

/** avcdec_video callback */
static int avcdec_catch_video(struct uprobe *uprobe, struct upipe *upipe,
                        int event, va_list args)
{
    if (event != UPROBE_NEW_FLOW_DEF)
        return uprobe_throw_next(uprobe, upipe, event, args);
    struct uref *flow_def = va_arg(args, struct uref *);
    uint64_t hsize, vsize;
    struct urational sar;
    struct urational fps;
    if (unlikely(!ubase_check(uref_pic_flow_get_hsize(flow_def, &hsize)) || !ubase_check(uref_pic_flow_get_vsize(flow_def, &vsize)) || !ubase_check(uref_pic_flow_get_sar(flow_def, &sar)) || !ubase_check(uref_pic_flow_get_fps(flow_def, &fps)))) {
        upipe_err_va(upipe, "incompatible flow def");
        upipe_release(upipe_source);
        return UBASE_ERR_UNHANDLED;
    }
    /* pic flow definition */
    struct uref *flow = uref_pic_flow_alloc_def(uref_mgr, 1);
    ubase_assert(uref_pic_flow_add_plane(flow, 1, 1, 3, "r8g8b8"));

    ubase_assert(uref_pic_flow_set_hsize(flow, hsize));
    ubase_assert(uref_pic_flow_set_vsize(flow, vsize));
    ubase_assert(uref_pic_flow_set_fps(flow, fps));
    #if SWS_RGB
    struct upipe *sws = upipe_flow_alloc_output(upipe, upipe_sws_mgr, uprobe_pfx_alloc_va(uprobe_output_alloc(uprobe_use(logger)),UPROBE_LOG_LEVEL, "sws"), flow);
    assert(sws != NULL);
    upipe_release(upipe);
    if (sws == NULL) {
        upipe_err_va(upipe, "incompatible flow def");
        uref_free(flow);
        upipe_release(upipe_source);
        return true;
    }

    upipe = sws;
    #endif
    /*Trickplay */
    struct upipe_mgr *upipe_trickp_mgr = upipe_trickp_mgr_alloc();
    struct upipe *trickp = upipe_void_alloc(upipe_trickp_mgr,uprobe_pfx_alloc_va(uprobe_use(logger), UPROBE_LOG_LEVEL, "trickp"));
    upipe_mgr_release(upipe_trickp_mgr);
    upipe_attach_uclock(trickp);
    struct upipe *trickp_video = upipe_void_alloc_output_sub(upipe, trickp,uprobe_pfx_alloc_va(uprobe_output_alloc(uprobe_use(logger)),UPROBE_LOG_LEVEL, "trickp video"));
    upipe_release(trickp);
    upipe_release(upipe);
    upipe = trickp_video;
    int i = 0;
    for(i=0; i<NB_SRC; i++)
    {
        if(uprobe == &uprobe_avcdec_video[i])
        {
            struct upipe *urefprobe = upipe_void_alloc_output(upipe,
            upipe_probe_uref_video_mgr,
            uprobe_pfx_alloc_va(uprobe_output_alloc(uprobe_use(&uprobe_uref_video[i])),UPROBE_LOG_LEVEL, "urefprobe")); 
            upipe_release(upipe);
        }
    }
    return UBASE_ERR_NONE;
}
/** catch probes from uprobe_catch_video */
static int video_catch(struct uprobe *uprobe, struct upipe *upipe, int event, va_list args)
{
    if (event != UPROBE_NEW_FLOW_DEF)  
        return uprobe_throw_next(uprobe, upipe, event, args);
    int i=0;
    struct uprobe *u_avc_vid = NULL;
    for(i=0; i<NB_SRC; i++)
    {
        if(uprobe == &uprobe_catch_video[i])
        {
            u_avc_vid = &uprobe_avcdec_video[i];
        }
    }

    struct upipe *avcdec = upipe_void_alloc_output(upipe, upipe_avcdec_video_mgr, uprobe_pfx_alloc_va(uprobe_output_alloc(uprobe_use(u_avc_vid)),UPROBE_LOG_LEVEL, "avcdec_video"));
    if (avcdec == NULL) {
        upipe_err_va(upipe, "incompatible flow def");
        upipe_release(upipe_source);
        return UBASE_ERR_UNHANDLED;
    }
    upipe_release(avcdec); 
    return UBASE_ERR_NONE;
}

/* catch probes from uprobe_uref_audio */
static int uref_catch_audio(struct uprobe *uprobe, struct upipe *upipe,
                      int event, va_list args)
{    
    if (event != UPROBE_PROBE_UREF) 
        return uprobe_throw_next(uprobe, upipe, event, args);

    va_list args_copy;
    va_copy(args_copy, args);
    unsigned int signature = va_arg(args_copy, unsigned int);
    va_end(args_copy);

    if (signature != UPIPE_PROBE_UREF_SIGNATURE) return uprobe_throw_next(uprobe, upipe, event, args); 

    int i=0;
    for(i=0; i<NB_SRC; i++)
    {
        if(uprobe == &uprobe_uref_audio[i])
        {
            upipe_set_output(upipe,r128[i]);
            upipe_set_output(r128[i],sound[i]);
        }
    }

    return UBASE_ERR_NONE;
}



/** avcdec_audio callback */
static int avcdec_catch_audio(struct uprobe *uprobe, struct upipe *upipe,
                       int event, va_list args)
{
    struct uref *flow_def;
    const char *def;
    if (!uprobe_plumber(event, args, &flow_def, &def)) 
        return uprobe_throw_next(uprobe, upipe, event, args); 
    int i = 0;
    for(i=0; i<NB_SRC; i++)
    {
        if(uprobe == &uprobe_avcdec_audio[i])
        {
            upipe_set_flow_def(r128[i],flow_def);
            upipe_set_flow_def(sound[i], flow_def);

            struct upipe *urefprobe = upipe_void_alloc_output(upipe,
                upipe_probe_uref_video_mgr,
                uprobe_pfx_alloc_va(uprobe_output_alloc(uprobe_use(&uprobe_uref_audio[i])),
                                    UPROBE_LOG_LEVEL, "urefprobe")); 
            upipe_release(upipe);
        }
    }
    return UBASE_ERR_NONE;
} 

/** catch probes from uprobe_catch_audio */
static int audio_catch (struct uprobe *uprobe, struct upipe *upipe, int event, va_list args)
{
    if (event != UPROBE_NEW_FLOW_DEF)
        return uprobe_throw_next(uprobe, upipe, event, args);

    int i=0;
    struct uprobe *u_avc_aud = NULL;
    for(i=0; i<NB_SRC; i++)
    {
        if(uprobe == &uprobe_catch_audio[i])
        {
            u_avc_aud = &uprobe_avcdec_audio[i];
        }
    }

    struct upipe *avcdec = upipe_void_alloc_output(upipe, upipe_avcdec_audio_mgr,
            uprobe_pfx_alloc_va(uprobe_output_alloc(uprobe_use(u_avc_aud)),
                                UPROBE_LOG_LEVEL, "avcdec_audio"));
    if (avcdec == NULL) {
        upipe_err_va(upipe, "incompatible flow def");
        upipe_release(upipe_source);
        return UBASE_ERR_UNHANDLED;
    }
    upipe_release(avcdec); 
    return UBASE_ERR_NONE;
}

int example_main(int argc, char *argv[]) {
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

    int i = 0;
    /* upipe env */
    struct ev_loop *loop = ev_default_loop(0);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop,UPUMP_POOL, UPUMP_BLOCKER_POOL);
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,umem_mgr, -1, -1);
    uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH,udict_mgr, 0);
    struct uclock *uclock = uclock_std_alloc(0);
    /*default probe */
    logger = uprobe_stdio_alloc(NULL, stdout, UPROBE_LOG_LEVEL);
    logger = uprobe_uclock_alloc(logger, uclock);
    logger = uprobe_uref_mgr_alloc(logger, uref_mgr);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,UBUF_POOL_DEPTH);
    logger = uprobe_upump_mgr_alloc(logger, upump_mgr);
    logger = uprobe_pthread_upump_mgr_alloc(logger);
    uprobe_pthread_upump_mgr_set(logger, upump_mgr);

    /* probes audio & video & dejitter*/
    /* audio */
    uprobe_catch_audio = malloc(NB_SRC*sizeof(struct uprobe));
    uprobe_avcdec_audio = malloc(NB_SRC*sizeof(struct uprobe));
    uprobe_uref_audio = malloc(NB_SRC*sizeof(struct uprobe));
    for(i = 0; i< NB_SRC;i++)
    {
        uprobe_init(&uprobe_catch_audio[i], audio_catch, uprobe_use(logger));
        uprobe_init(&uprobe_avcdec_audio[i], avcdec_catch_audio, uprobe_use(logger));
        uprobe_init(&uprobe_uref_audio[i],  uref_catch_audio, uprobe_use(logger));
    }
    /* video */
    uprobe_catch_video = malloc(NB_SRC*sizeof(struct uprobe));
    uprobe_avcdec_video = malloc(NB_SRC*sizeof(struct uprobe));
    uprobe_uref_video = malloc(NB_SRC*sizeof(struct uprobe));
    for(i = 0; i< NB_SRC;i++)
    {
        uprobe_init(&uprobe_catch_video[i], video_catch, uprobe_use(logger));
        uprobe_init(&uprobe_avcdec_video[i], avcdec_catch_video, uprobe_use(logger));
        uprobe_init(&uprobe_uref_video[i],  uref_catch_video, uprobe_use(logger));
    }
    /* dejitter */
    uprobe_dejitter_set(uprobe_dejitter, DEJITTER_DIVIDER);
    /* upipe-av */
    upipe_av_init(true, uprobe_use(logger));
    /* global pipe managers */
    upipe_avcdec_video_mgr = upipe_avcdec_mgr_alloc();
    upipe_avcdec_audio_mgr = upipe_avcdec_mgr_alloc();
    upipe_avcenc_mgr = upipe_avcenc_mgr_alloc();
    upipe_sws_mgr = upipe_sws_mgr_alloc();
    upipe_probe_uref_video_mgr = upipe_probe_uref_mgr_alloc();
    upipe_probe_uref_audio_mgr = upipe_probe_uref_mgr_alloc();

    /* Threads + Message loops */

    PP_Resource src_loop = ppb_message_loop_interface->Create(PSGetInstanceId());
    struct thread_data src_threadData;
    pthread_t src_thread;
    src_threadData.loop = src_loop;
    src_threadData.message_loop_interface = ppb_message_loop_interface;
    src_threadData.instance_id = PSGetInstanceId();
    pthread_create(&src_thread,NULL,&thread_main,&src_threadData);

    PP_Resource *display_loop = malloc(NB_SRC*sizeof(PP_Resource));
    struct thread_data *display_threadData = malloc(NB_SRC*sizeof(struct thread_data));
    pthread_t *display_thread = malloc(NB_SRC*sizeof(pthread_t));
    for(i=0;i<NB_SRC;i++)
    {
        display_loop[i] = ppb_message_loop_interface->Create(PSGetInstanceId());
        display_threadData[i].loop = display_loop[i];
        display_threadData[i].message_loop_interface = ppb_message_loop_interface;
        display_threadData[i].instance_id = PSGetInstanceId();
        pthread_create(&display_thread[i],NULL,&thread_main,&display_threadData[i]);
    }

    /* Pipes sources + managers*/
    struct upipe_mgr **upipe_src_mgr = malloc(NB_SRC*sizeof(struct upipe_mgr*));
    struct upipe **upipe_src = malloc(NB_SRC*sizeof(struct upipe*));
    /*UDP*/
    #if UDP
    for(i = 0; i<NB_SRC; i++)
    {
        upipe_src_mgr[i] = upipe_src_udp_chrome_mgr_alloc();
        upipe_src[i] = _upipe_src_udp_chrome_alloc(upipe_src_mgr[i], uprobe_pfx_alloc(uprobe_use(logger),UPROBE_LOG_LEVEL, "src_chrome"),src_loop);
        char* uri = calloc(1024,sizeof(char));
        snprintf(uri ,1024, "127.0.0.%d:%d", i+1,8880+i);
        ubase_assert(upipe_set_uri(upipe_src[i],uri));
    }
#endif
    /*TCP*/
#if TCP
    for(i = 0; i<NB_SRC; i++)
    {
        upipe_src_mgr[i] = upipe_src_tcp_chrome_mgr_alloc();
        upipe_src[i] = _upipe_src_tcp_chrome_alloc(upipe_src_mgr[i], uprobe_pfx_alloc(uprobe_use(logger),UPROBE_LOG_LEVEL, "src_chrome"),src_loop);
        char* uri = calloc(1024,sizeof(char));
        snprintf(uri ,1024, "127.0.0.%d:%d", i+1,8880+i);
        ubase_assert(upipe_set_uri(upipe_src[i],uri));
    }
#endif

    /* Context */
    
    PSEventSetFilter(PSE_ALL);

    while (!g_Context.bound) {
        PSEvent* event;
        while ((event = PSEventTryAcquire()) != NULL) {
            ProcessEvent(event);
            PSEventRelease(event);
        }
    }
    PP_Resource image = ppb_imagedata_interface->Create(PSGetInstanceId(), PP_IMAGEDATAFORMAT_BGRA_PREMUL, &(g_Context.size), PP_FALSE);

    /* upipe_display + mgr */
    struct upipe_mgr **display_mgr = malloc(NB_SRC*sizeof(struct upipe_mgr*));
    display = malloc(NB_SRC*sizeof(struct upipe*));
    for(i=0; i<NB_SRC; i++)
    {
        display_mgr[i] = upipe_display_mgr_alloc();
        display[i] = _upipe_display_alloc(display_mgr[i], uprobe_pfx_alloc(uprobe_use(logger),UPROBE_LOG_LEVEL, "display"), image, display_loop[i]);
        upipe_attach_uclock(display[i]);
        upipe_display_set_hposition(display[i],i%2*720);
        upipe_display_set_vposition(display[i],i/2*576);
        upipe_display_set_context(display[i],g_Context);
    }
    /* upipe_sound + mgr */
    upipe_filter_ebur128_mgr = malloc(NB_SRC*sizeof(struct upipe_mgr*));
    struct upipe_mgr **sound_mgr = malloc(NB_SRC*sizeof(struct upipe_mgr*));
    r128 = malloc(NB_SRC*sizeof(struct upipe*));
    sound = malloc(NB_SRC*sizeof(struct upipe*));
    for(i=0; i<NB_SRC; i++)
    {
        upipe_filter_ebur128_mgr[i] = upipe_filter_ebur128_mgr_alloc();
        sound_mgr[i] = upipe_sound_mgr_alloc();
        sound[i] = _upipe_sound_alloc(sound_mgr[i],uprobe_pfx_alloc(uprobe_use(logger),UPROBE_LOG_LEVEL, "sound"), src_loop);
        r128[i] = _upipe_filter_ebur128_alloc(upipe_filter_ebur128_mgr[i], uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "r128"),i);
        upipe_filter_ebur128_set_time_length(r128[i], 1500);
    }            
    /* upipe-ts */

    for(i = 0; i<NB_SRC;i++)
    {
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

        struct upipe *ts_demux = upipe_void_alloc_output(upipe_src[i],
                upipe_ts_demux_mgr,
                    uprobe_pfx_alloc(
                        uprobe_selflow_alloc(uprobe_use(logger),
                            uprobe_selflow_alloc(
                                uprobe_selflow_alloc(uprobe_use(uprobe_dejitter),
                                    uprobe_output_alloc(uprobe_use(&uprobe_catch_video[i])),
                                        UPROBE_SELFLOW_PIC, "auto"),
                                    uprobe_output_alloc(uprobe_use(&uprobe_catch_audio[i])),
                                UPROBE_SELFLOW_SOUND, "auto"),
                            UPROBE_SELFLOW_VOID, "auto"),
                        UPROBE_LOG_DEBUG, "ts demux"));

        assert(ts_demux != NULL);
        upipe_mgr_release(upipe_ts_demux_mgr);
        upipe_release(ts_demux);
    }
    /*ev-loop*/
    ev_loop(loop, 0);

    /* release & free*/
    uref_mgr_release(uref_mgr);
    uprobe_release(logger);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    upump_mgr_release(upump_mgr);
    for(i=0; i<NB_SRC; i++)
    {
        upipe_mgr_release(upipe_src_mgr[i]);
        upipe_mgr_release(display_mgr[i]);
        upipe_mgr_release(upipe_filter_ebur128_mgr[i]);
        upipe_mgr_release(sound_mgr[i]);

        upipe_release(upipe_src[i]);
        upipe_release(display[i]);
        upipe_release(r128[i]);
        upipe_release(sound[i]);

        uprobe_release(&uprobe_catch_audio[i]);
        uprobe_release(&uprobe_avcdec_audio[i]);
        uprobe_release(&uprobe_uref_audio[i]);
        uprobe_release(&uprobe_catch_video[i]);
        uprobe_release(&uprobe_avcdec_video[i]);
        uprobe_release(&uprobe_uref_video[i]);

        ppb_message_loop_interface->PostQuit(display_loop[i], PP_FALSE);
        ppb_core_interface->ReleaseResource(display_loop[i]);
    }
    uprobe_release(uprobe_dejitter);
    uprobe_release(logger);
    free(upipe_src_mgr);
    free(display_mgr);
    free(upipe_filter_ebur128_mgr);
    free(sound_mgr);
    free(upipe_src);
    free(display);
    free(r128);
    free(sound);
    free(uprobe_catch_audio);
    free(uprobe_avcdec_audio);
    free(uprobe_uref_audio);
    free(uprobe_catch_video);
    free(uprobe_avcdec_video);
    free(uprobe_uref_video);
    free(display_threadData);
    free(display_thread);
    ppb_message_loop_interface->PostQuit(src_loop, PP_FALSE);
    ppb_core_interface->ReleaseResource(src_loop);
    ppb_core_interface->ReleaseResource(image);
    ev_default_destroy();

while(1){}
return 0;
}
void UpdateContext(uint32_t width, uint32_t height) {
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

void ProcessEvent(PSEvent* event) {
    switch(event->type) {
        /* If the view updates, build a new Graphics 2D Context */
        case PSE_INSTANCE_DIDCHANGEVIEW: {
            struct PP_Rect rect;
            ppb_view_interface->GetRect(event->as_resource, &rect);
            UpdateContext(rect.size.width, rect.size.height);
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
PPAPI_SIMPLE_REGISTER_MAIN(example_main);
