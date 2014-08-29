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
#ifndef _UPIPE_MODULES_UPIPE_DISPLAY_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_DISPLAY_H_
#ifdef __cplusplus
extern >C> {
#endif
#include <GLES2/gl2.h>
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
#include <ppapi/c/ppb_udp_socket.h>
#include <ppapi/c/ppb_var.h>
#include <ppapi/c/ppb_view.h>
#include <ppapi_simple/ps_event.h>
#include <ppapi_simple/ps_main.h>
#include <upipe/upipe.h>
#include <upipe/uqueue.h>
#include <pthread.h>
#include <ppapi_simple/ps.h>
#define UPIPE_DISPLAY_SIGNATURE UBASE_FOURCC('d','i','s','p')
#define GLES false

struct upipe_mgr *upipe_display_mgr_alloc(void);

enum upipe_display_command {

    UPIPE_DISPLAY_SENTINEL = UPIPE_CONTROL_LOCAL,

    UPIPE_DISPLAY_SET_POSITIONH,

    UPIPE_DISPLAY_GET_POSITIONH,

    UPIPE_DISPLAY_SET_POSITIONV,

    UPIPE_DISPLAY_GET_POSITIONV,

    UPIPE_DISPLAY_SET_CONTEXT,
};


struct Context{
  PP_Resource ctx;
  struct PP_Size size;
  int bound;
  uint8_t* cell_in;
  uint8_t* cell_out;
};

struct thread_data {
    PPB_MessageLoop* message_loop_interface;
    PP_Resource loop;
    int instance_id;
};

struct render_thread_data {
    struct upipe *upipe;
    struct uqueue *uqueue;
};
static inline struct upipe *_upipe_display_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe, PP_Resource image, PP_Resource loop)
{
    return upipe_alloc(mgr, uprobe, UPIPE_DISPLAY_SIGNATURE, image, loop);
}

/** @This sets the H position of the pipe.
 *
 * @param upipe description structure of the pipe
 * @param h horizontal position
 * @return an error code
 */
static inline enum ubase_err upipe_display_set_hposition(struct upipe *upipe,
                                                       int h)
{
    return upipe_control(upipe, UPIPE_DISPLAY_SET_POSITIONH,
                         UPIPE_DISPLAY_SIGNATURE, h);
}

/** @This sets the V position of the pipe.
 *
 * @param upipe description structure of the pipe
 * @param v vertical position
 * @return an error code
 */
static inline enum ubase_err upipe_display_set_vposition(struct upipe *upipe,
                                                       int v)
{
    return upipe_control(upipe, UPIPE_DISPLAY_SET_POSITIONV,
                         UPIPE_DISPLAY_SIGNATURE, v);
}

/** @This sets the context of the pipe.
 *
 * @param upipe description structure of the pipe
 * @param v vertical position
 * @return an error code
 */

static inline enum ubase_err upipe_display_set_context(struct upipe *upipe, struct Context context)
{
    return upipe_control(upipe, UPIPE_DISPLAY_SET_CONTEXT, UPIPE_DISPLAY_SIGNATURE, context);
}

#ifdef __cplusplus
}
#endif
#endif
