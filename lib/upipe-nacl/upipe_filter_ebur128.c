/*
 * Copyright (C) 2013-2014 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen, Xavier Boulet
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

/** @file
 * @short Upipe ebur128
 */

#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/udict.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_sound.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-nacl/upipe_filter_ebur128.h>
#include <upipe-ebur128/ebur128.h>
#include <stdlib.h>
#include <strings.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>

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
#include <ppapi/c/ppb_url_loader.h>
#include <ppapi/c/ppb_url_request_info.h>
#include <ppapi/c/ppb_url_response_info.h>
#include <ppapi/c/ppb_var.h>
#include <ppapi/c/ppb_view.h>
#include <ppapi/c/ppb_websocket.h>
#include <ppapi_simple/ps_event.h>
#include <ppapi_simple/ps_main.h>

/** @internal upipe_filter_ebur128 private structure */
struct upipe_filter_ebur128 {
    /** refcount management structure */
    struct urefcount urefcount;

    /** output */
    struct upipe *output;
    /** output flow */
    struct uref *output_flow;
    struct uref *flow_def;
    /** has flow def been sent ?*/
    bool output_flow_sent;
    int n_uref;
    double** loudness_t;
    int n_pipe;
    int time_ms ;
    /** ebur128 state */
    ebur128_state **stM;
    PPB_Messaging* messaging_interface;
    PPB_Var* var_interface;
    /** public structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_filter_ebur128, upipe,
                   UPIPE_FILTER_EBUR128_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_filter_ebur128, urefcount,
                       upipe_filter_ebur128_free)
UPIPE_HELPER_VOID(upipe_filter_ebur128)
UPIPE_HELPER_OUTPUT(upipe_filter_ebur128, output,
                    output_flow, output_flow_sent)

/** @internal @This allocates a filter pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_filter_ebur128_alloc(struct upipe_mgr *mgr,
                                                struct uprobe *uprobe,
                                                uint32_t signature,
                                                va_list args)
{
    struct upipe_filter_ebur128 *upipe_filter_ebur128 = malloc(sizeof(struct upipe_filter_ebur128));
    struct upipe *upipe = upipe_filter_ebur128_to_upipe(upipe_filter_ebur128);
    upipe_filter_ebur128->messaging_interface = (PPB_Messaging*)PSGetInterface(PPB_MESSAGING_INTERFACE);
    upipe_filter_ebur128->var_interface = (PPB_Var*)PSGetInterface(PPB_VAR_INTERFACE);
    upipe_init(upipe, mgr, uprobe);
    upipe_filter_ebur128->stM = NULL;
    upipe_filter_ebur128_init_urefcount(upipe);
    upipe_filter_ebur128_init_output(upipe);
    upipe_filter_ebur128->n_uref = 0;
    upipe_filter_ebur128->n_pipe = va_arg(args,int);
    upipe_filter_ebur128->time_ms = 1000;

    upipe_throw_ready(upipe);
    return upipe;
}


static double max(double* t, int taille)
{
    int i = 0;
    double max = -999;
    for(i = 0 ;i<taille;i++)
    {
        if(t[i]>max)
        {
            max = t[i];
        }
    }
    return max;
}

/** @internal @This handles input.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 */
static void upipe_filter_ebur128_input(struct upipe *upipe, struct uref *uref,
                                       struct upump **upump_p)
{
    struct upipe_filter_ebur128 *upipe_filter_ebur128 = upipe_filter_ebur128_from_upipe(upipe);
    double    *loudM = NULL;
    size_t samples;
    uint8_t nb_planes = 0;
    int i = 0;
    const char **channels = NULL;
    const uint8_t **buf = NULL;
    uint64_t rate = 0;
    uref_sound_size(uref, &samples, NULL);
    uref_sound_flow_get_planes(upipe_filter_ebur128->flow_def,&nb_planes);
    buf = malloc(nb_planes*sizeof(uint8_t*));
    loudM = malloc(nb_planes*sizeof(double));
    channels = malloc(nb_planes*sizeof(char*));

    for(i = 0; i< nb_planes; i++)
    {
        uref_sound_flow_get_channel(upipe_filter_ebur128->flow_def, &channels[i], i);
        ubuf_sound_plane_read_uint8_t(uref->ubuf,channels[i], 0, -1, &(buf[i]));
        ubuf_sound_unmap(uref->ubuf,0,-1,1);
        ebur128_add_frames_short(upipe_filter_ebur128->stM[i],(const short*) buf[i], samples);
        ebur128_loudness_momentary(upipe_filter_ebur128->stM[i], &(loudM[i]));
    }
    uref_sound_flow_get_rate(upipe_filter_ebur128->flow_def, &rate);
    int nb_urefs = (upipe_filter_ebur128->time_ms*rate)/(samples*1000);
    bool b = true;
    for(i = 0; i< nb_planes; i++)
    {
        if(loudM[i] == INFINITY || loudM[i] == -INFINITY) {
            b=false;
        }
    }
    if(b) {
        if(upipe_filter_ebur128->n_uref <nb_urefs)
        {
            for(i = 0; i< nb_planes; i++)
            {
                upipe_filter_ebur128->loudness_t[i][upipe_filter_ebur128->n_uref] = loudM[i];    
            }
            upipe_filter_ebur128->n_uref++;
        }
        else
        {
            for(i = 0; i< nb_planes; i++)
            {
                memcpy(upipe_filter_ebur128->loudness_t[i],upipe_filter_ebur128->loudness_t[i]+1,sizeof(double)*(nb_urefs-1));
                upipe_filter_ebur128->loudness_t[i][nb_urefs-1] = loudM[i];    
            }
        }

        double * loudness_max = malloc(2*sizeof(double));
        loudness_max[0] = max(upipe_filter_ebur128->loudness_t[0],upipe_filter_ebur128->n_uref);
        loudness_max[1] = max(upipe_filter_ebur128->loudness_t[1],upipe_filter_ebur128->n_uref);
        char* post_loudness = calloc(4096,sizeof(char));
        char* substring = calloc(1024,sizeof(char));
        snprintf(substring, 1024, "%d:%d:",upipe_filter_ebur128->n_pipe,nb_planes);
        strcat(post_loudness,substring);
        for(i = 0; i< nb_planes; i++)
        {
            snprintf(substring, 1024, "%4.8f:%4.8f:",loudM[i],loudness_max[i]);
              strcat(post_loudness,substring);
        }    
        struct PP_Var pp_message = upipe_filter_ebur128->var_interface->VarFromUtf8(post_loudness, strlen(post_loudness));
        upipe_filter_ebur128->messaging_interface->PostMessage(PSGetInstanceId(), pp_message);


        free(substring);
        free(post_loudness);
        free(loudness_max);
    }
    free(channels);
    free(buf);
    free(loudM);
    upipe_filter_ebur128_output(upipe, uref, upump_p);
}


/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow flow definition packet
 * @return an error code
 */
static int upipe_filter_ebur128_set_flow_def(struct upipe *upipe,
                                             struct uref *flow)
{

    struct upipe_filter_ebur128 *upipe_filter_ebur128 =
                                 upipe_filter_ebur128_from_upipe(upipe);
    if (flow == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow, "sound."))
    upipe_filter_ebur128->flow_def = flow;
    uint8_t channels;
    uint8_t planes;
    uint64_t rate;
    if (unlikely(!ubase_check(uref_sound_flow_get_rate(flow, &rate)) || !ubase_check(uref_sound_flow_get_channels(flow, &channels)))) {
        return UBASE_ERR_INVALID;
    }
    ubase_check(uref_sound_flow_get_planes(flow,&planes)); 
    struct uref *flow_dup;
    if (unlikely((flow_dup = uref_dup(flow)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    upipe_filter_ebur128->stM = malloc(planes*sizeof(ebur128_state*));
    upipe_filter_ebur128->loudness_t = malloc(planes*sizeof(double*));
    int i = 0;
    for(i=0;i<planes;i++)
    {
        upipe_filter_ebur128->loudness_t[i] = malloc(255*sizeof(double));
        upipe_filter_ebur128->stM[i] = ebur128_init(channels/planes, rate,EBUR128_MODE_SAMPLE_PEAK);
    }

    upipe_filter_ebur128_store_flow_def(upipe, flow_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on the pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_filter_ebur128_control(struct upipe *upipe,
                                        int command, va_list args)
{
    struct upipe_filter_ebur128 *upipe_filter_ebur128 = upipe_filter_ebur128_from_upipe(upipe);
    switch (command) {
        case UPIPE_AMEND_FLOW_FORMAT:
            return UBASE_ERR_NONE;
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_filter_ebur128_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_filter_ebur128_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_filter_ebur128_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_filter_ebur128_set_output(upipe, output);
        }
        case UPIPE_FILTER_EBUR127_SET_TIME_LENGTH: {
            int signature = va_arg(args,int);
            int time_ms = va_arg(args,int);
            upipe_filter_ebur128->time_ms = time_ms;
            return UBASE_ERR_NONE;
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_filter_ebur128_free(struct upipe *upipe)
{
    struct upipe_filter_ebur128 *upipe_filter_ebur128 = upipe_filter_ebur128_from_upipe(upipe);
    if (likely(upipe_filter_ebur128->stM)) {
        ebur128_destroy(&upipe_filter_ebur128->stM[0]);
    }
    upipe_throw_dead(upipe);

    upipe_filter_ebur128_clean_output(upipe);
    upipe_filter_ebur128_clean_urefcount(upipe);
    upipe_filter_ebur128_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_filter_ebur128_mgr = {
    .refcount = NULL,
    .signature = UPIPE_FILTER_EBUR128_SIGNATURE,

    .upipe_alloc = upipe_filter_ebur128_alloc,
    .upipe_input = upipe_filter_ebur128_input,
    .upipe_control = upipe_filter_ebur128_control
};

/** @This returns the management structure for ebur128 pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_filter_ebur128_mgr_alloc(void)
{
    return &upipe_filter_ebur128_mgr;
}
