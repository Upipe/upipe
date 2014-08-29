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
#ifndef _UPIPE_MODULES_UPIPE_SOUND_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_SOUND_H_
#ifdef __cplusplus
extern "C" {
#endif
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
#include <ppapi/c/ppb_image_data.h>
#include <ppapi/c/ppb_input_event.h>
#include <ppapi/c/ppb_instance.h>
#include <ppapi/c/ppb_message_loop.h>
#include <ppapi/c/ppb_messaging.h>
#include <ppapi/c/ppb_net_address.h>
#include <ppapi/c/ppb_udp_socket.h>
#include <ppapi/c/ppb_var.h>
#include <ppapi/c/ppb_view.h>
#include <ppapi_simple/ps_event.h>
#include <ppapi_simple/ps_main.h>
#include <upipe/upipe.h>
#include <upipe/uqueue.h>
#include <pthread.h>
#include <ppapi_simple/ps.h>
#define UPIPE_SOUND_SIGNATURE UBASE_FOURCC('s','n','d','d')

struct upipe_mgr *upipe_sound_mgr_alloc(void);

struct buffer_temp {
    int16_t* buffer;
    int size;
};

struct audio_data {
    int count;
    int nb_samples;
    struct uref *flow_def;
    struct uqueue* bufferAudio;
    struct buffer_temp* bufferTemp;
};

struct start_data {
    PP_Resource loop;
    PPB_Audio* audio_interface;
    PP_Resource pp_audio;
};


static inline struct upipe *_upipe_sound_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe, PP_Resource loop)
{
    return upipe_alloc(mgr, uprobe, UPIPE_SOUND_SIGNATURE, loop);
}

#ifdef __cplusplus
}
#endif
#endif
