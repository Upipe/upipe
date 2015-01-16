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

/** @file
 * @short Upipe NaCl module to play audio samples
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uqueue.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_sound.h>
#include <upipe/ubuf_sound_common.h>
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
#include <upipe/uqueue.h>
#include <upipe-av/upipe_av_pixfmt.h>
#include <upipe-nacl/upipe_nacl_audio.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <time.h>

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
#include <ppapi_simple/ps.h>
#include <nacl_io/nacl_io.h>

#define UQUEUE_SIZE 255
#define COUNT 512

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

/** @internal upipe_nacl_audio private structure */
struct upipe_nacl_audio {
    /** refcount management structure */
    struct urefcount urefcount;

    /** output flow */
    struct uref *flow_def;

    /** public upipe structure */
    struct upipe upipe;

    /** PPAPI Interfaces + PP_Resources*/
    PPB_Var* var_interface;
    PPB_MessageLoop* message_loop_interface;
    PPB_Audio* audio_interface;
    PPB_AudioConfig* audio_config_interface;
    PP_Resource loop;
    PP_Resource audio_config;
    PP_Resource audio;
    
    /** */
    struct start_data startData;
    struct audio_data audioData;
    bool started;
    struct uqueue bufferAudio;
    /** extra data for the queue structure */
    uint8_t extra[];
};

UPIPE_HELPER_UPIPE(upipe_nacl_audio, upipe, UPIPE_NACL_AUDIO_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_nacl_audio, urefcount, upipe_nacl_audio_free);
UPIPE_HELPER_VOID(upipe_nacl_audio);

void startCallBack(void* user_data, int32_t result) {
    struct start_data* data = (struct start_data*)(user_data); 
    data->audio_interface->StartPlayback(data->pp_audio);
}

/** @internal @This fill the sound buffer.*/
void AudioCallback(void* sample_buffer,uint32_t buffer_size, PP_TimeDelta latency, void* user_data) {
    struct audio_data* audioData = (struct audio_data*)(user_data);
    int16_t* buff = (int16_t*)(sample_buffer);
    uint8_t nb_chan; 
    uint8_t nb_planes;
    int i=0;
    int j=0;    
    uref_sound_flow_get_planes(audioData->flow_def, &nb_planes);
    uref_sound_flow_get_channels(audioData->flow_def, &nb_chan);
    const char** chan = malloc(nb_chan*sizeof(char*));
    if(audioData->bufferTemp->size > audioData->count*nb_chan/nb_planes)
    {
        for(i=0;i < audioData->count * nb_chan;i=i+nb_planes)
        {
            for(j=0;j<nb_planes;j++)
            {
                buff[i+j]=audioData->bufferTemp->buffer[(int)(i/nb_planes)+j*audioData->nb_samples];
            }
        }
        for(j=0;j<nb_planes;j++)
        {
            for(i=0; i < audioData->bufferTemp->size - audioData->count*nb_chan/nb_planes; i++)
            {
                audioData->bufferTemp->buffer[i+j*audioData->nb_samples] = audioData->bufferTemp->buffer[i+j*audioData->nb_samples+audioData->count*nb_chan/nb_planes];
            }
        }
        audioData->bufferTemp->size -= audioData->count*nb_chan/nb_planes;
    }
    else if(unlikely(uqueue_length(audioData->bufferAudio) == 0))
    {
        for(i=0;i < audioData->count * nb_chan;i+=nb_chan)
        {
            for(j=0;j<nb_chan;j++)
            {
                buff[i+j]=0;
            }
        }
    }
    else
    {
        /*uref*/
        const int16_t** buff2 = malloc(nb_planes*sizeof(int16_t*));
        struct uref* uref = uqueue_pop(audioData->bufferAudio,struct uref*);
        for(i = 0; i< nb_planes; i++)
        {
            uref_sound_flow_get_channel(audioData->flow_def, &chan[i], i);
            ubuf_sound_plane_read_int16_t(uref->ubuf,chan[i], 0, -1, &(buff2[i]));
            ubuf_sound_unmap(uref->ubuf,0,-1,1);
        }
        if(nb_chan == nb_planes) {
            /*reading bufftemp*/
            for(i=0;i<nb_chan*audioData->bufferTemp->size;i+=nb_chan)
            {    
                for(j=0;j<nb_chan;j++)
                {
                    buff[i+j]=audioData->bufferTemp->buffer[(int)(i/nb_chan)+j*audioData->nb_samples ];
                }
            }
            /*reading beginning of uref*/
            for(i=nb_chan*audioData->bufferTemp->size;i < audioData->count * nb_chan;i=i+nb_chan)
            {
                for(j=0;j<nb_chan;j++)
                {
                    buff[i+j]=buff2[j][(int)((i/nb_chan)-audioData->bufferTemp->size)];
                }
            }
            /*saving end of uref in bufftemp*/
            for(j=0;j<nb_chan;j++)
            {
                for(i=0; i < audioData->nb_samples - audioData->count + audioData->bufferTemp->size; i++)
                {
                    audioData->bufferTemp->buffer[i+j*audioData->nb_samples] = buff2[j][i+audioData->count - audioData->bufferTemp->size];
                }
            }
            audioData->bufferTemp->size = audioData->nb_samples - audioData->count + audioData->bufferTemp->size;
        }
        else {
            /*reading bufftemp*/
            for(i=0;i<audioData->bufferTemp->size;i++)
            {    
                buff[i]=audioData->bufferTemp->buffer[i];
            }
            /*reading beginning of uref*/
            for(i=audioData->bufferTemp->size;i < audioData->count * nb_chan; i++)
            {
                buff[i]=buff2[0][i-audioData->bufferTemp->size];
            }
            /*saving end of uref in bufftemp*/
            for(i=0; i < audioData->nb_samples - audioData->count * nb_chan + audioData->bufferTemp->size; i++)
            {
                audioData->bufferTemp->buffer[i] = buff2[0][i+audioData->count * nb_chan - audioData->bufferTemp->size];
            }
            audioData->bufferTemp->size = audioData->nb_samples - audioData->count * nb_chan + audioData->bufferTemp->size;
        }
        free(chan);
        uref_free(uref);
        free(buff2);
    }    
} 

/** @internal @This handles input.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 */
static void upipe_nacl_audio_input(struct upipe *upipe, struct uref *uref, 
                struct upump **upump_p)
{    
    struct upipe_nacl_audio *upipe_nacl_audio = upipe_nacl_audio_from_upipe(upipe);
    size_t nb_samples = 0;
    uint8_t nb_planes = 0;
    uint8_t nb_chan = 0;
    uint8_t sample_size;

    if(unlikely(!ubase_check(ubuf_sound_size(uref->ubuf, &nb_samples, NULL))))    {
        printf("error ubuf_sound_size\n");
        uref_free(uref);
        return;
    }
    if(unlikely(!uqueue_push(&upipe_nacl_audio->bufferAudio, uref)))   {
            uref_free(uref);
            return;
    }
    if(unlikely(upipe_nacl_audio->audioData.bufferTemp->buffer == NULL))   {
        uref_sound_flow_get_planes(upipe_nacl_audio->flow_def, &nb_planes);
        uref_sound_flow_get_channels(upipe_nacl_audio->flow_def, &nb_chan);
        uref_sound_flow_get_sample_size(upipe_nacl_audio->flow_def, &sample_size);
        upipe_nacl_audio->audioData.nb_samples = nb_samples*(nb_chan/nb_planes);
        upipe_nacl_audio->audioData.bufferTemp->buffer = malloc(nb_planes* upipe_nacl_audio->audioData.nb_samples * sizeof(int16_t));
        upipe_nacl_audio->audioData.bufferTemp->size = 0;
    }
    if(unlikely(!upipe_nacl_audio->started && uqueue_length(&upipe_nacl_audio->bufferAudio) > 1))   {
        upipe_nacl_audio->started = true;
        upipe_nacl_audio->startData.loop = upipe_nacl_audio->loop;
        upipe_nacl_audio->startData.audio_interface = upipe_nacl_audio->audio_interface;
        upipe_nacl_audio->startData.pp_audio = upipe_nacl_audio->audio; 
        struct PP_CompletionCallback startCB = PP_MakeCompletionCallback(startCallBack,&(upipe_nacl_audio->startData));
        upipe_nacl_audio->message_loop_interface->PostWork(upipe_nacl_audio->loop, startCB,0);
    }
}

/** @internal @This allocates a sound pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe* upipe_nacl_audio_alloc(struct upipe_mgr *mgr,
                                     struct uprobe *uprobe,
                                     uint32_t signature, va_list args)
{
    struct upipe_nacl_audio *upipe_nacl_audio = malloc(sizeof(struct upipe_nacl_audio)+uqueue_sizeof(UQUEUE_SIZE));
    struct upipe *upipe = upipe_nacl_audio_to_upipe(upipe_nacl_audio);
    upipe_init(upipe, mgr, uprobe);

    upipe_nacl_audio_init_urefcount(upipe);
    upipe_nacl_audio->var_interface = (PPB_Var*)PSGetInterface(PPB_VAR_INTERFACE);
    upipe_nacl_audio->message_loop_interface = (PPB_MessageLoop*)PSGetInterface(PPB_MESSAGELOOP_INTERFACE);
    upipe_nacl_audio->audio_config_interface = (PPB_AudioConfig*)PSGetInterface(PPB_AUDIO_CONFIG_INTERFACE);
    upipe_nacl_audio->audio_interface = (PPB_Audio*)PSGetInterface(PPB_AUDIO_INTERFACE);
    upipe_nacl_audio->loop = va_arg(args,PP_Resource);
    upipe_nacl_audio->started = false;
    
    if(unlikely(!uqueue_init(&(upipe_nacl_audio->bufferAudio), UQUEUE_SIZE, upipe_nacl_audio->extra)))
    {
        free(upipe_nacl_audio);
        return NULL;
    }

    upipe_nacl_audio->audioData.bufferTemp = malloc(sizeof(struct buffer_temp));
    upipe_nacl_audio->audioData.bufferTemp->buffer = NULL;
    upipe_nacl_audio->audioData.bufferTemp->size = 0;
    upipe_nacl_audio->audioData.nb_samples = 0;
    upipe_nacl_audio->audioData.bufferAudio = &upipe_nacl_audio->bufferAudio;
    
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_nacl_audio_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    struct upipe_nacl_audio *upipe_nacl_audio = upipe_nacl_audio_from_upipe(upipe);
    if (flow_def == NULL) {
        return UBASE_ERR_INVALID;
    }

    upipe_nacl_audio->flow_def = uref_dup(flow_def);
    uint64_t sample_rate;
    uref_sound_flow_get_rate(upipe_nacl_audio->flow_def, &sample_rate);
    uint32_t count = upipe_nacl_audio->audio_config_interface->RecommendSampleFrameCount(PSGetInstanceId(),sample_rate, COUNT);
    upipe_nacl_audio->audio_config = upipe_nacl_audio->audio_config_interface->CreateStereo16Bit(PSGetInstanceId(), sample_rate, count);
    uint8_t nb_planes = 0;
    uref_sound_flow_get_planes(upipe_nacl_audio->flow_def, &nb_planes);
    upipe_nacl_audio->audioData.count = count;
    upipe_nacl_audio->audioData.flow_def = upipe_nacl_audio->flow_def;
    upipe_nacl_audio->audio = upipe_nacl_audio->audio_interface->Create(PSGetInstanceId(),upipe_nacl_audio->audio_config,AudioCallback,&(upipe_nacl_audio->audioData));

    return UBASE_ERR_NONE;

}

/** @internal @This provides a flow format suggestion.
 *
 * @param upipe description structure of the pipe
 * @param request description structure of the request
 * @return an error code
 */
static int upipe_nacl_audio_provide_flow_format(struct upipe *upipe,
                                           struct urequest *request)
{
    struct uref *flow_format = uref_dup(request->uref);
    UBASE_ALLOC_RETURN(flow_format);
    uref_sound_flow_clear_format(flow_format);
    uref_flow_set_def(flow_format, "sound.s16.");
    uref_sound_flow_set_channels(flow_format, 2);
    uref_sound_flow_set_sample_size(flow_format, 4);
    uref_sound_flow_set_planes(flow_format, 0);
    uref_sound_flow_add_plane(flow_format, "lr");
    return urequest_provide_flow_format(request, flow_format);
}

/** @internal @This processes control commands on the pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_nacl_audio_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_FLOW_FORMAT)
                return upipe_nacl_audio_provide_flow_format(upipe, request);
            return upipe_throw_provide_request(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST:
            return UBASE_ERR_NONE;
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_nacl_audio_set_flow_def(upipe, flow_def);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_nacl_audio_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_nacl_audio_clean_urefcount(upipe);
    upipe_nacl_audio_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_nacl_audio_mgr = {
    .refcount = NULL,
    .signature = UPIPE_NACL_AUDIO_SIGNATURE,

    .upipe_alloc = upipe_nacl_audio_alloc,
    .upipe_input = upipe_nacl_audio_input,
    .upipe_control = upipe_nacl_audio_control,
    .upipe_mgr_control = NULL
};

/** @This returns the management structure for sound pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_nacl_audio_mgr_alloc(void)
{
    return &upipe_nacl_audio_mgr;
}
    
