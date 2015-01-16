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

#undef NDEBUG

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uqueue.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_uclock.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_sound.h>
#include <upipe/ubuf_sound_common.h>
#include <upipe/uclock.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_clock.h>
#include <upipe/ubuf_pic.h>
#include <upipe/uref_dump.h>
#include <upipe/uref.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_std.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_input.h>
#include <upipe-av/upipe_av_pixfmt.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <time.h>

#include <upipe-nacl/upipe_nacl_graphic2d.h>

#include <nacl_io/nacl_io.h>
    
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
#include <ppapi_simple/ps.h>

#define AssertNoGLError() \
  assert(!upipe_nacl_graphic2d->ppb_open_gles2_interface->GetError(upipe_nacl_graphic2d->context.ctx));
/* BGRA helper macro, for constructing a pixel for a BGRA buffer. */
#define UQUEUE_SIZE 255
#define MakeBGRA(b, g, r, a)  \
  (((a) << 24) | ((r) << 16) | ((g) << 8) | (b))
static void upipe_nacl_graphic2d_input(struct upipe *upipe, struct uref *uref, struct upump **upump_p);
static bool upipe_nacl_graphic2d_input_(struct upipe *upipe, struct uref *uref, struct upump **upump_p);

struct render_thread_data {
    struct upipe *upipe;
    struct uqueue *uqueue;
};

/** @internal upipe_nacl_graphic2d private structure */
struct upipe_nacl_graphic2d {
    struct Context context;
    struct uclock *uclock;
    struct urequest uclock_request;
    uint64_t latency;
    /** refcount management structure */
    struct urefcount urefcount;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** event watcher */
    struct upump *upump_watcher;
    /** write watcher */
    struct upump *upump;
    /** public upipe structure */
    struct upipe upipe;
    /** temporary uref storage */
    struct uchain urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers */
    struct uchain blockers;
    /* PPAPI Interfaces*/
    PPB_Core* ppb_core_interface;
    PPB_Fullscreen* ppb_fullscreen_interface;
    PPB_Graphics2D* ppb_graphic2d_interface;
    PPB_Graphics3D* ppb_graphic3d_interface;
    PPB_ImageData* ppb_imagedata_interface;
    PPB_Instance* ppb_instance_interface;
    PPB_View* ppb_view_interface;
    PPB_Var* ppb_var_interface;
    PPB_MessageLoop* message_loop_interface;
    struct PPB_OpenGLES2* ppb_open_gles2_interface;

    struct render_thread_data data;
    PP_Resource image;
    PP_Resource loop;
    /* Position on the context */
    int position_v;
    int position_h;
    struct uqueue queue_uref;
    uint8_t extra[];
};

UPIPE_HELPER_UPIPE(upipe_nacl_graphic2d, upipe, UPIPE_NACL_GRAPHIC2D_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_nacl_graphic2d, urefcount, upipe_nacl_graphic2d_free);
UPIPE_HELPER_VOID(upipe_nacl_graphic2d);
UPIPE_HELPER_UCLOCK(upipe_nacl_graphic2d, uclock, uclock_request, NULL, upipe_throw_provide_request, NULL)

UPIPE_HELPER_UPUMP_MGR(upipe_nacl_graphic2d, upump_mgr);
UPIPE_HELPER_UPUMP(upipe_nacl_graphic2d, upump, upump_mgr);
UPIPE_HELPER_INPUT(upipe_nacl_graphic2d, urefs, nb_urefs, max_urefs, blockers, upipe_nacl_graphic2d_input_);

static void upipe_nacl_graphic2d_watcher(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    upipe_nacl_graphic2d_set_upump(upipe, NULL);
    upipe_nacl_graphic2d_output_input(upipe);
    upipe_nacl_graphic2d_unblock_input(upipe);
}

/** @internal @This render a picture.
 *
 * @param upipe description structure of the pipe
 * @param color is the color matrix(BGR)
 * @param sizeH is the width of the picture
 * @param sizeV is the height of the picture
 * @param dx is the horizontal position on the context
 * @param dy is the vertical position on the context
 * @return an error code
 */
void Render(struct upipe *upipe,int** color,size_t sizeH,size_t sizeV, int dx, int dy) {
    struct upipe_nacl_graphic2d *display = upipe_nacl_graphic2d_from_upipe(upipe);

    PP_Resource image = display->image;
    struct PP_ImageDataDesc desc;
    uint8_t* cell_temp;
    uint32_t x, y;

    /* If we somehow have not allocated these pointers yet, skip this frame. */
    if (!display->context.cell_in || !display->context.cell_out) return;
    /* desc.size.height/width */
    display->ppb_imagedata_interface->Describe(image, &desc);
    uint8_t* pixels = display->ppb_imagedata_interface->Map(image);      
    for (y = 0; y < sizeV; y++) {
    uint32_t *pixel_line =  (uint32_t*) (pixels + (y+dy) * desc.stride);
        for (x = 0; x < sizeH; x++) {
            *(pixel_line+x+dx) = color[y][x];
        }
    }
    cell_temp = display->context.cell_in;
    display->context.cell_in = display->context.cell_out;
    display->context.cell_out = cell_temp;

    display->ppb_imagedata_interface->Unmap(image);  

    display->ppb_graphic2d_interface->ReplaceContents(display->context.ctx, image);
    struct PP_CompletionCallback cb_null = PP_MakeCompletionCallback(NULL,NULL);
    display->ppb_graphic2d_interface->Flush(display->context.ctx, cb_null);
}

/** @internal @This read a picture from a uqueue.
 *
 * @param user_data is a struct render_thread_data
 * @return an error code
 */
void startCallBack_display(void* user_data, int32_t result) {
    struct render_thread_data* Data = (struct render_thread_data*)(user_data); 
    struct upipe *upipe = Data->upipe;
    if(unlikely(uqueue_length(Data->uqueue) == 0))
    {
        return;
    }
    struct uref *uref = uqueue_pop(Data->uqueue,struct uref*);
    struct upipe_nacl_graphic2d *upipe_nacl_graphic2d = upipe_nacl_graphic2d_from_upipe(upipe);
    const uint8_t *data = NULL;
    size_t sizeH, sizeV;
    int i,j;
    if(uref_pic_size(uref, &sizeH, &sizeV, NULL)!=0) return;
    if(uref_pic_plane_read(uref, "r8g8b8", 0, 0, -1, -1, &data)!=0) return;
    uref_pic_plane_unmap(uref, "r8g8b8", 0, 0, -1, -1);
    int** color = malloc(sizeof(int*)*sizeV);
    for(i=0;i<sizeV;i++) {
        color[i] = malloc(sizeof(int)*sizeH);
        for(j = 0; j < sizeH; j++) {
            color[i][j] = MakeBGRA(data[3*(sizeH*i+j)+2], data[3*(sizeH*i+j)+1], data[3*(sizeH*i+j)], 0xff);
        }
    }
    upipe = upipe_nacl_graphic2d_to_upipe(upipe_nacl_graphic2d);
    Render(upipe,color,sizeH,sizeV,upipe_nacl_graphic2d->position_h,upipe_nacl_graphic2d->position_v);
    for(i=0;i<sizeV;i++) {
        free(color[i]);
    }

    free(color);
    uref_free(uref);
    return;
}

/** @internal @This handles input.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 */
static void upipe_nacl_graphic2d_input(struct upipe *upipe, struct uref *uref, 
                struct upump **upump_p)
{
    if(!upipe_nacl_graphic2d_check_input(upipe) || !upipe_nacl_graphic2d_input_(upipe, uref, upump_p))
    {
        upipe_nacl_graphic2d_hold_input(upipe, uref);
        upipe_nacl_graphic2d_block_input(upipe, upump_p);
    }
}

static bool upipe_nacl_graphic2d_input_(struct upipe *upipe, struct uref *uref, 
                struct upump **upump_p)
{
    struct upipe_nacl_graphic2d *upipe_nacl_graphic2d = upipe_nacl_graphic2d_from_upipe(upipe);

    if (upipe_nacl_graphic2d->uclock != NULL) {
        uint64_t pts = 0;
        uint64_t now = uclock_now(upipe_nacl_graphic2d->uclock);

        if(!ubase_check(uref_clock_get_pts_sys(uref, &pts)))
        {
            upipe_dbg(upipe, "packet without pts");
            uref_free(uref);
            return true;
        }
        pts += upipe_nacl_graphic2d->latency;
        if (now < pts)
        {
            upipe_nacl_graphic2d_wait_upump(upipe, pts - now, upipe_nacl_graphic2d_watcher);
            return false;
        }
        else if (now > pts + UCLOCK_FREQ / 10)
        {
            upipe_warn_va(upipe, "late picture dropped %"PRIu64, now - pts);
            uref_free(uref);
            return true;
        }
    }

    #if GLES
    #else
    if(unlikely(!uqueue_push(&upipe_nacl_graphic2d->queue_uref, uref)))   {
        upipe_err(upipe, "cannot queue");
        uref_free(uref);
        return true;
    }
    struct PP_CompletionCallback startCB_display = PP_MakeCompletionCallback(startCallBack_display,&(upipe_nacl_graphic2d->data));
    if(upipe_nacl_graphic2d->message_loop_interface->PostWork(upipe_nacl_graphic2d->loop, startCB_display,0)!=PP_OK)printf("postwork pas ok \n");
    #endif
    return true;
}

/** @internal @This allocates a display pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe* upipe_nacl_graphic2d_alloc(struct upipe_mgr *mgr,
                                     struct uprobe *uprobe,
                                     uint32_t signature, va_list args)
{
    struct upipe_nacl_graphic2d *upipe_nacl_graphic2d = malloc(sizeof(struct upipe_nacl_graphic2d)+uqueue_sizeof(UQUEUE_SIZE));
    struct upipe *upipe = upipe_nacl_graphic2d_to_upipe(upipe_nacl_graphic2d);
    upipe_init(upipe, mgr, uprobe);
    upipe_nacl_graphic2d_init_urefcount(upipe);
    upipe_nacl_graphic2d->ppb_core_interface = (PPB_Core*)PSGetInterface(PPB_CORE_INTERFACE);
    upipe_nacl_graphic2d->ppb_graphic2d_interface = (PPB_Graphics2D*)PSGetInterface(PPB_GRAPHICS_2D_INTERFACE);
    upipe_nacl_graphic2d->ppb_graphic3d_interface = (PPB_Graphics3D*)PSGetInterface(PPB_GRAPHICS_2D_INTERFACE);
    upipe_nacl_graphic2d->ppb_imagedata_interface = (PPB_ImageData*)PSGetInterface(PPB_IMAGEDATA_INTERFACE);
    upipe_nacl_graphic2d->ppb_instance_interface = (PPB_Instance*)PSGetInterface(PPB_INSTANCE_INTERFACE);
    upipe_nacl_graphic2d->ppb_view_interface = (PPB_View*)PSGetInterface(PPB_VIEW_INTERFACE);
    upipe_nacl_graphic2d->message_loop_interface = (PPB_MessageLoop*)PSGetInterface(PPB_MESSAGELOOP_INTERFACE);
    upipe_nacl_graphic2d->ppb_open_gles2_interface = (struct PPB_OpenGLES2*)PSGetInterface(PPB_OPENGLES2_INTERFACE);
    upipe_nacl_graphic2d->image = va_arg(args, PP_Resource);
    upipe_nacl_graphic2d->loop = va_arg(args, PP_Resource);
    upipe_nacl_graphic2d_init_upump_mgr(upipe);
    upipe_nacl_graphic2d_init_upump(upipe);
    upipe_nacl_graphic2d_init_input(upipe);
    upipe_nacl_graphic2d_init_uclock(upipe);
    upipe_nacl_graphic2d->latency= 0;
    upipe_nacl_graphic2d->position_v = 0;
    upipe_nacl_graphic2d->position_h = 0;
    #if GLES
    g_texture_data = malloc(kTextureDataLength*sizeof(uint8_t));
    #endif
    if(unlikely(!uqueue_init(&upipe_nacl_graphic2d->queue_uref, UQUEUE_SIZE, upipe_nacl_graphic2d->extra)))
    {
        free(upipe_nacl_graphic2d);
        return NULL;
    }
    upipe_nacl_graphic2d->data.upipe = upipe;
    upipe_nacl_graphic2d->data.uqueue = &(upipe_nacl_graphic2d->queue_uref);
    upipe_throw_ready(upipe);

    upipe_nacl_graphic2d_check_upump_mgr(upipe);

    return upipe;
}

/** @internal @This sets the context.
 *
 * @param upipe description structure of the pipe
 * @param context is the context
 * @return an error code
 */
static int _upipe_nacl_graphic2d_set_context(struct upipe *upipe, struct Context context)
{
    struct upipe_nacl_graphic2d *upipe_nacl_graphic2d = upipe_nacl_graphic2d_from_upipe(upipe);
    upipe_nacl_graphic2d->context = context;
    #if GLES
    upipe_nacl_graphic2d_InitShaders(upipe);
    #endif

    return UBASE_ERR_NONE;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_nacl_graphic2d_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_nacl_graphic2d_clean_urefcount(upipe);
    upipe_nacl_graphic2d_clean_upump_mgr(upipe);
    upipe_nacl_graphic2d_clean_upump(upipe);
    upipe_nacl_graphic2d_clean_input(upipe);
    upipe_nacl_graphic2d_clean_uclock(upipe);

    upipe_nacl_graphic2d_free_void(upipe);
}

static int upipe_nacl_graphic2d_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    struct upipe_nacl_graphic2d *display = upipe_nacl_graphic2d_from_upipe(upipe);
    uref_clock_get_latency(flow_def, &display->latency);
    display->latency = UCLOCK_FREQ / 5; /* FIXME */
    return UBASE_ERR_NONE;
}

/** @internal @This provides a flow format suggestion.
 *
 * @param upipe description structure of the pipe
 * @param request description structure of the request
 * @return an error code
 */
static int upipe_nacl_graphic2d_provide_flow_format(struct upipe *upipe,
                                             struct urequest *request)
{
    struct uref *flow_format = uref_dup(request->uref);
    UBASE_ALLOC_RETURN(flow_format);
    uref_pic_flow_clear_format(flow_format);
    uref_pic_flow_set_macropixel(flow_format, 1);
    uref_pic_flow_set_planes(flow_format, 0);
    uref_pic_flow_add_plane(flow_format, 1, 1, 3, "r8g8b8");
    uref_pic_set_progressive(flow_format);
    return urequest_provide_flow_format(request, flow_format);
}

/** @internal @This processes control commands on the pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_nacl_graphic2d_control(struct upipe *upipe, int command, va_list args)
{
    struct upipe_nacl_graphic2d *display = upipe_nacl_graphic2d_from_upipe(upipe);
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR: {
            upipe_nacl_graphic2d_set_upump(upipe, NULL);
            UBASE_RETURN(upipe_nacl_graphic2d_attach_upump_mgr(upipe))
            return UBASE_ERR_NONE;
        }
        case UPIPE_ATTACH_UCLOCK:
            upipe_nacl_graphic2d_set_upump(upipe, NULL);
            upipe_nacl_graphic2d_require_uclock(upipe);
            return UBASE_ERR_NONE;
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_FLOW_FORMAT)
                return upipe_nacl_graphic2d_provide_flow_format(upipe, request);
            return upipe_throw_provide_request(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST:
            return UBASE_ERR_NONE;

        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref*);
            return upipe_nacl_graphic2d_set_flow_def(upipe, flow_def);
        }
        case UPIPE_NACL_GRAPHIC2D_SET_POSITIONH: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_NACL_GRAPHIC2D_SIGNATURE);
            display->position_h = va_arg(args, int);
            return UBASE_ERR_NONE;
        }
        case UPIPE_NACL_GRAPHIC2D_SET_POSITIONV: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_NACL_GRAPHIC2D_SIGNATURE);
            display->position_v = va_arg(args, int);
            return UBASE_ERR_NONE;
        }
        case UPIPE_NACL_GRAPHIC2D_SET_CONTEXT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_NACL_GRAPHIC2D_SIGNATURE);
            _upipe_nacl_graphic2d_set_context(upipe,va_arg(args,struct Context));
            return UBASE_ERR_NONE;
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** module manager static descriptor */
static struct upipe_mgr upipe_nacl_graphic2d_mgr = {
    .refcount = NULL,
    .signature = UPIPE_NACL_GRAPHIC2D_SIGNATURE,

    .upipe_alloc = upipe_nacl_graphic2d_alloc,
    .upipe_input = upipe_nacl_graphic2d_input,

    .upipe_control = upipe_nacl_graphic2d_control,
    .upipe_mgr_control = NULL
};

/** @This returns the management structure for display pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_nacl_graphic2d_mgr_alloc(void)
{
    return &upipe_nacl_graphic2d_mgr;
}
