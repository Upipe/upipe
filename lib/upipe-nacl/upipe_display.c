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

#include <upipe/ubase.h>
#include <upipe/ulist.h>
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
#include <upipe/upipe_helper_sink.h>
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

#include <upipe-nacl/upipe_display.h>

#include <nacl_io/nacl_io.h>
    
#include <upipe-nacl/graphic_3d.h>

#define AssertNoGLError() \
  assert(!upipe_display->ppb_open_gles2_interface->GetError(upipe_display->context.ctx));
/* BGRA helper macro, for constructing a pixel for a BGRA buffer. */
#define UQUEUE_SIZE 255
#define MakeBGRA(b, g, r, a)  \
  (((a) << 24) | ((r) << 16) | ((g) << 8) | (b))
static void upipe_display_input(struct upipe *upipe, struct uref *uref, struct upump **upump_p);
static bool upipe_display_input_(struct upipe *upipe, struct uref *uref, struct upump **upump_p);

/** @internal upipe_display private structure */
struct upipe_display {
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

UPIPE_HELPER_UPIPE(upipe_display, upipe, UPIPE_DISPLAY_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_display, urefcount, upipe_display_free);
UPIPE_HELPER_VOID(upipe_display);
UPIPE_HELPER_UCLOCK(upipe_display, uclock, uclock_request, NULL, upipe_throw_provide_request, NULL)

UPIPE_HELPER_UPUMP_MGR(upipe_display, upump_mgr);
UPIPE_HELPER_UPUMP(upipe_display, upump, upump_mgr);
UPIPE_HELPER_SINK(upipe_display, urefs, nb_urefs, max_urefs, blockers, upipe_display_input_);

#if GLES
    const float kFovY = 45.0f;
    const float kZNear = 1.0f;
    const float kZFar = 10.0f;
    const float kCameraZ = -4.0f;
    const float kXAngleDelta = 2.0f;
    const float kYAngleDelta = 0.5f;

    const size_t kTextureDataLength = 128 * 128 * 3;  // 128x128, 3 Bytes/pixel.

// The decompressed data is written here.
    uint8_t *g_texture_data;
    int32_t width_;
    int32_t height_;
    GLuint frag_shader_;
    GLuint vertex_shader_;
    GLuint program_;
    GLuint vertex_buffer_;
    GLuint index_buffer_;
    GLuint texture_;

    GLuint texture_loc_;
    GLuint position_loc_;
    GLuint texcoord_loc_;
    GLuint color_loc_;
    GLuint mvp_loc_;

    float x_angle_ = 0;
    float y_angle_ = 0;

void DecompressTexture() {

    const uint8_t* input = &kRLETextureData[0];
    const uint8_t* const input_end = &kRLETextureData[kRLETextureDataLength];
    uint8_t* output = &g_texture_data[0];
    #ifndef NDEBUG
    const uint8_t* const output_end = &g_texture_data[kTextureDataLength];
    #endif

    uint8_t decoded[4];
    int decoded_count = 0;

    while (input < input_end || decoded_count > 0) {
        if (decoded_count < 2) {
            assert(input + 4 <= input_end);
            // Grab four base-64 encoded (6-bit) bytes.
            uint32_t data = 0;
            data |= (kBase64Decode[*input++] << 18);
            data |= (kBase64Decode[*input++] << 12);
            data |= (kBase64Decode[*input++] <<  6);
            data |= (kBase64Decode[*input++]      );
            // And decode it to 3 (8-bit) bytes.
            decoded[decoded_count++] = (data >> 16) & 0xff;
            decoded[decoded_count++] = (data >>  8) & 0xff;
            decoded[decoded_count++] = (data      ) & 0xff;

            // = is the base64 end marker. Remove decoded bytes if we see any.
            if (input[-1] == '=') decoded_count--;
            if (input[-2] == '=') decoded_count--;
        }

        int value = decoded[0];
        int count = decoded[1];
        decoded_count -= 2;
        // Move the other decoded bytes (if any) down.
        decoded[0] = decoded[2];
        decoded[1] = decoded[3];

        // Expand the RLE data.
        if (count == 0) count = 256;
        assert(output <= output_end);
        memset(output, value, count);
        output += count;
  }
  assert(output == output_end);
}

/*Init Shaders and buffers*/
void upipe_display_InitShaders(struct upipe *upipe)
{
    struct upipe_display *upipe_display = upipe_display_from_upipe(upipe);
       /*Shaders*/
    GLuint vertex_shader =  upipe_display->ppb_open_gles2_interface->CreateShader(upipe_display->context.ctx,GL_VERTEX_SHADER);
    GLuint fragment_shader = upipe_display->ppb_open_gles2_interface->CreateShader(upipe_display->context.ctx,GL_FRAGMENT_SHADER);

    if(upipe_display->ppb_open_gles2_interface->IsShader(upipe_display->context.ctx, vertex_shader) != GL_TRUE || upipe_display->ppb_open_gles2_interface->IsShader(upipe_display->context.ctx, fragment_shader) != GL_TRUE)
    {
        printf("Error creating shaders\n");
        return;
    }

    const char **vShader_src = malloc(sizeof(char*));
    GLint *vShader_length = malloc(sizeof(GLint));
    vShader_src[0] = kVertexShaderSource;
    vShader_length[0] = (GLint)strlen(vShader_src[0]);
    upipe_display->ppb_open_gles2_interface->ShaderSource(upipe_display->context.ctx, vertex_shader,1, vShader_src, vShader_length);

    const char **fShader_src = malloc(sizeof(char*));
    GLint *fShader_length = malloc(sizeof(GLint));
    fShader_src[0] = kFragShaderSource;
    fShader_length[0] = (GLint)strlen(fShader_src[0]);
    upipe_display->ppb_open_gles2_interface->ShaderSource(upipe_display->context.ctx, fragment_shader,1, fShader_src, fShader_length);

    upipe_display->ppb_open_gles2_interface->CompileShader(upipe_display->context.ctx,vertex_shader);
    upipe_display->ppb_open_gles2_interface->CompileShader(upipe_display->context.ctx,fragment_shader);

    free((char**)(vShader_src));
    free(vShader_length);
    free((char**)(fShader_src));
    free(fShader_length);  

    int param1 = -42;
    int param2 = -42;
    upipe_display->ppb_open_gles2_interface->GetShaderiv(upipe_display->context.ctx, vertex_shader, GL_COMPILE_STATUS, &param1);
    upipe_display->ppb_open_gles2_interface->GetShaderiv(upipe_display->context.ctx, fragment_shader, GL_COMPILE_STATUS, &param2);
    if(param1 != GL_TRUE || param2 != GL_TRUE)
    {
        printf("Error compiling shaders param 1 = %d param2 = %d \n",param1,param2);
        char* vInfolog = malloc(2048*sizeof(char));
        char* fInfolog = malloc(2048*sizeof(char));
        int size1 = -42;
        int size2 = -42;
        upipe_display->ppb_open_gles2_interface->GetShaderInfoLog(upipe_display->context.ctx, vertex_shader, 2048, &size1, vInfolog);
        upipe_display->ppb_open_gles2_interface->GetShaderInfoLog(upipe_display->context.ctx, fragment_shader, 2048, &size2, fInfolog);

        printf("%s\n", vInfolog);
        printf("%s\n", fInfolog);
        
        free(vInfolog);
        free(fInfolog);
        return;
    }
    /* Program */
    GLuint gl_program = upipe_display->ppb_open_gles2_interface->CreateProgram(upipe_display->context.ctx); 

    if(upipe_display->ppb_open_gles2_interface->IsProgram(upipe_display->context.ctx, gl_program) != GL_TRUE)
    {
        printf("Error creating gl_program\n");
        return;
    }

    upipe_display->ppb_open_gles2_interface->AttachShader(upipe_display->context.ctx, gl_program, vertex_shader);
    upipe_display->ppb_open_gles2_interface->AttachShader(upipe_display->context.ctx, gl_program, fragment_shader);
    upipe_display->ppb_open_gles2_interface->LinkProgram(upipe_display->context.ctx, gl_program);
    
    int param3 = -42;
    upipe_display->ppb_open_gles2_interface->GetProgramiv(upipe_display->context.ctx, gl_program, GL_LINK_STATUS, &param3);
    if( param3 != GL_TRUE)
    {
        printf("param 3 = %d\n",param3);
        char* pInfolog = malloc(2048*sizeof(char));
        int size3 = -42;
        upipe_display->ppb_open_gles2_interface->GetProgramInfoLog(upipe_display->context.ctx, gl_program, 2048, &size3, pInfolog);
        printf("%s\n", pInfolog);
        free(pInfolog); 
        return;
    }

    texture_loc_ = upipe_display->ppb_open_gles2_interface->GetUniformLocation(upipe_display->context.ctx, gl_program, "u_texture");
    position_loc_ = upipe_display->ppb_open_gles2_interface->GetAttribLocation(upipe_display->context.ctx, gl_program, "a_position");
    texcoord_loc_ = upipe_display->ppb_open_gles2_interface->GetAttribLocation(upipe_display->context.ctx, gl_program, "a_texcoord");
    color_loc_ = upipe_display->ppb_open_gles2_interface->GetAttribLocation(upipe_display->context.ctx, gl_program, "a_color");
    mvp_loc_ = upipe_display->ppb_open_gles2_interface->GetUniformLocation(upipe_display->context.ctx, gl_program, "u_mvp");

    AssertNoGLError();
    /* Init Buffers */

    GLuint index_buffer_,vertex_buffer_;
    upipe_display->ppb_open_gles2_interface->GenBuffers(upipe_display->context.ctx, 1, &vertex_buffer_);
    upipe_display->ppb_open_gles2_interface->BindBuffer(upipe_display->context.ctx, GL_ARRAY_BUFFER, vertex_buffer_);
    upipe_display->ppb_open_gles2_interface->BufferData(upipe_display->context.ctx, GL_ARRAY_BUFFER, sizeof(kCubeVerts), &kCubeVerts[0], GL_STATIC_DRAW);
    upipe_display->ppb_open_gles2_interface->GenBuffers(upipe_display->context.ctx, 1, &index_buffer_);
    upipe_display->ppb_open_gles2_interface->BindBuffer(upipe_display->context.ctx, GL_ELEMENT_ARRAY_BUFFER, index_buffer_);
    upipe_display->ppb_open_gles2_interface->BufferData(upipe_display->context.ctx, GL_ELEMENT_ARRAY_BUFFER, sizeof(kCubeIndexes), &kCubeIndexes[0], GL_STATIC_DRAW);
    AssertNoGLError();
    
    /* Init Textures */
    DecompressTexture();
    upipe_display->ppb_open_gles2_interface->GenTextures(upipe_display->context.ctx, 1, &texture_);
    upipe_display->ppb_open_gles2_interface->BindTexture(upipe_display->context.ctx, GL_TEXTURE_2D, texture_);
    upipe_display->ppb_open_gles2_interface->TexParameteri(upipe_display->context.ctx, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    upipe_display->ppb_open_gles2_interface->TexParameteri(upipe_display->context.ctx, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    upipe_display->ppb_open_gles2_interface->TexParameteri(upipe_display->context.ctx, GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    upipe_display->ppb_open_gles2_interface->TexParameteri(upipe_display->context.ctx, GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    upipe_display->ppb_open_gles2_interface->TexImage2D(upipe_display->context.ctx, GL_TEXTURE_2D, 0, GL_RGB, 128, 128, 0, GL_RGB, GL_UNSIGNED_BYTE, &g_texture_data[0]);
    AssertNoGLError();
    /*Render*/
    while(1){
        upipe_display->ppb_open_gles2_interface->ClearColor(upipe_display->context.ctx, 0.5, 0.5, 0.5, 1);
        upipe_display->ppb_open_gles2_interface->ClearDepthf(upipe_display->context.ctx, 1.0f);
        upipe_display->ppb_open_gles2_interface->Clear(upipe_display->context.ctx, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        upipe_display->ppb_open_gles2_interface->Enable(upipe_display->context.ctx, GL_DEPTH_TEST);

        //set what program to use
        upipe_display->ppb_open_gles2_interface->UseProgram(upipe_display->context.ctx, gl_program);
        upipe_display->ppb_open_gles2_interface->ActiveTexture(upipe_display->context.ctx, GL_TEXTURE0);
        upipe_display->ppb_open_gles2_interface->BindTexture(upipe_display->context.ctx, GL_TEXTURE_2D, texture_);
        upipe_display->ppb_open_gles2_interface->Uniform1i(upipe_display->context.ctx, texture_loc_, 0);
        AssertNoGLError();
        //create our perspective matrix
        float mvp[16];
        float trs[16];
        float rot[16];

        identity_matrix(mvp);
        const float aspect_ratio = (float)(width_) / height_;
        glhPerspectivef2(&mvp[0], kFovY, aspect_ratio, kZNear, kZFar);
    
        translate_matrix(0, 0, kCameraZ, trs);
        rotate_matrix(x_angle_, y_angle_, 0.0f, rot);
        multiply_matrix(trs, rot, trs);
        multiply_matrix(mvp, trs, mvp);

        upipe_display->ppb_open_gles2_interface->UniformMatrix4fv(upipe_display->context.ctx, mvp_loc_, 1, GL_FALSE, mvp);
        //define the attributes of the vertex
        upipe_display->ppb_open_gles2_interface->BindBuffer(upipe_display->context.ctx, GL_ARRAY_BUFFER, vertex_buffer_);
    
        upipe_display->ppb_open_gles2_interface->VertexAttribPointer(upipe_display->context.ctx,position_loc_,3,GL_FLOAT,GL_FALSE,sizeof(struct Vertex),(void*)(offsetof(struct Vertex, loc)));
        upipe_display->ppb_open_gles2_interface->EnableVertexAttribArray(upipe_display->context.ctx, position_loc_);
        upipe_display->ppb_open_gles2_interface->VertexAttribPointer(upipe_display->context.ctx, color_loc_, 3, GL_FLOAT, GL_FALSE, sizeof(struct Vertex), (void*)(offsetof(struct Vertex, color)));
        upipe_display->ppb_open_gles2_interface->EnableVertexAttribArray(upipe_display->context.ctx, color_loc_);
        upipe_display->ppb_open_gles2_interface->VertexAttribPointer(upipe_display->context.ctx, texcoord_loc_, 2, GL_FLOAT, GL_FALSE, sizeof(struct Vertex), (void*)(offsetof(struct Vertex, tex)));
        upipe_display->ppb_open_gles2_interface->EnableVertexAttribArray(upipe_display->context.ctx, texcoord_loc_);

        upipe_display->ppb_open_gles2_interface->BindBuffer(upipe_display->context.ctx, GL_ELEMENT_ARRAY_BUFFER, index_buffer_);
        upipe_display->ppb_open_gles2_interface->DrawElements(upipe_display->context.ctx, GL_TRIANGLES, 36, GL_UNSIGNED_BYTE, 0);
        struct PP_CompletionCallback cb_null = PP_MakeCompletionCallback(NULL,NULL);
        if(upipe_display->ppb_graphic3d_interface->SwapBuffers(upipe_display->context.ctx,cb_null) != PP_OK) printf("Error swaping buffers\n");
        AssertNoGLError();
    }
    return;
}
#endif
static void upipe_display_watcher(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    upipe_display_set_upump(upipe, NULL);
    upipe_display_output_sink(upipe);
    upipe_display_unblock_sink(upipe);
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
    struct upipe_display *display = upipe_display_from_upipe(upipe);

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
    struct upipe_display *upipe_display = upipe_display_from_upipe(upipe);
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
    upipe = upipe_display_to_upipe(upipe_display);
    Render(upipe,color,sizeH,sizeV,upipe_display->position_h,upipe_display->position_v);
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
static void upipe_display_input(struct upipe *upipe, struct uref *uref, 
                struct upump **upump_p)
{
    if(!upipe_display_check_sink(upipe) || !upipe_display_input_(upipe, uref, upump_p))
    {
        upipe_display_hold_sink(upipe, uref);
        upipe_display_block_sink(upipe, upump_p);
    }
}

static bool upipe_display_input_(struct upipe *upipe, struct uref *uref, 
                struct upump **upump_p)
{
    struct upipe_display *upipe_display = upipe_display_from_upipe(upipe);

    if (upipe_display->uclock != NULL) {
        uint64_t pts = 0;
        uint64_t now = uclock_now(upipe_display->uclock);

        if(!ubase_check(uref_clock_get_pts_sys(uref, &pts)))
        {
            upipe_dbg(upipe, "packet without pts");
            uref_free(uref);
            return true;
        }
        pts += upipe_display->latency;
        if (now < pts)
        {
            upipe_display_wait_upump(upipe, pts - now, upipe_display_watcher);
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
    if(unlikely(!uqueue_push(&upipe_display->queue_uref, uref)))   {
        upipe_err(upipe, "cannot queue");
        uref_free(uref);
        return true;
    }
    struct PP_CompletionCallback startCB_display = PP_MakeCompletionCallback(startCallBack_display,&(upipe_display->data));
    if(upipe_display->message_loop_interface->PostWork(upipe_display->loop, startCB_display,0)!=PP_OK)printf("postwork pas ok \n");
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
static struct upipe* upipe_display_alloc(struct upipe_mgr *mgr,
                                     struct uprobe *uprobe,
                                     uint32_t signature, va_list args)
{
    struct upipe_display *upipe_display = malloc(sizeof(struct upipe_display)+uqueue_sizeof(UQUEUE_SIZE));
    struct upipe *upipe = upipe_display_to_upipe(upipe_display);
    upipe_init(upipe, mgr, uprobe);
    upipe_display_init_urefcount(upipe);
    upipe_display->ppb_core_interface = (PPB_Core*)PSGetInterface(PPB_CORE_INTERFACE);
    upipe_display->ppb_graphic2d_interface = (PPB_Graphics2D*)PSGetInterface(PPB_GRAPHICS_2D_INTERFACE);
    upipe_display->ppb_graphic3d_interface = (PPB_Graphics3D*)PSGetInterface(PPB_GRAPHICS_2D_INTERFACE);
    upipe_display->ppb_imagedata_interface = (PPB_ImageData*)PSGetInterface(PPB_IMAGEDATA_INTERFACE);
    upipe_display->ppb_instance_interface = (PPB_Instance*)PSGetInterface(PPB_INSTANCE_INTERFACE);
    upipe_display->ppb_view_interface = (PPB_View*)PSGetInterface(PPB_VIEW_INTERFACE);
    upipe_display->message_loop_interface = (PPB_MessageLoop*)PSGetInterface(PPB_MESSAGELOOP_INTERFACE);
    upipe_display->ppb_open_gles2_interface = (struct PPB_OpenGLES2*)PSGetInterface(PPB_OPENGLES2_INTERFACE);
    upipe_display->image = va_arg(args, PP_Resource);
    upipe_display->loop = va_arg(args, PP_Resource);
    upipe_display_init_upump_mgr(upipe);
    upipe_display_init_upump(upipe);
    upipe_display_init_sink(upipe);
    upipe_display_init_uclock(upipe);
    upipe_display->latency= 0;
    upipe_display->position_v = 0;
    upipe_display->position_h = 0;
    #if GLES
    g_texture_data = malloc(kTextureDataLength*sizeof(uint8_t));
    #endif
    if(unlikely(!uqueue_init(&upipe_display->queue_uref, UQUEUE_SIZE, upipe_display->extra)))
    {
        free(upipe_display);
        return NULL;
    }
    upipe_display->data.upipe = upipe;
    upipe_display->data.uqueue = &(upipe_display->queue_uref);
    upipe_throw_ready(upipe);

    upipe_display_check_upump_mgr(upipe);

    return upipe;
}

/** @internal @This sets the context.
 *
 * @param upipe description structure of the pipe
 * @param context is the context
 * @return an error code
 */
static int _upipe_display_set_context(struct upipe *upipe, struct Context context)
{
    struct upipe_display *upipe_display = upipe_display_from_upipe(upipe);
    upipe_display->context = context;
    #if GLES
    upipe_display_InitShaders(upipe);
    #endif

    return UBASE_ERR_NONE;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_display_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_display_clean_urefcount(upipe);
    upipe_display_clean_upump_mgr(upipe);
    upipe_display_clean_upump(upipe);
    upipe_display_clean_sink(upipe);
    upipe_display_clean_uclock(upipe);

    upipe_display_free_void(upipe);
}

static int upipe_display_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    struct upipe_display *display = upipe_display_from_upipe(upipe);
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
static int upipe_display_provide_flow_format(struct upipe *upipe,
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
static int upipe_display_control(struct upipe *upipe, int command, va_list args)
{
    struct upipe_display *display = upipe_display_from_upipe(upipe);
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR: {
            upipe_display_set_upump(upipe, NULL);
            UBASE_RETURN(upipe_display_attach_upump_mgr(upipe))
            return UBASE_ERR_NONE;
        }
        case UPIPE_ATTACH_UCLOCK:
            upipe_display_set_upump(upipe, NULL);
            upipe_display_require_uclock(upipe);
            return UBASE_ERR_NONE;
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_FLOW_FORMAT)
                return upipe_display_provide_flow_format(upipe, request);
            return upipe_throw_provide_request(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST:
            return UBASE_ERR_NONE;

        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref*);
            return upipe_display_set_flow_def(upipe, flow_def);
        }
        case UPIPE_DISPLAY_SET_POSITIONH: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_DISPLAY_SIGNATURE);
            display->position_h = va_arg(args, int);
            return UBASE_ERR_NONE;
        }
        case UPIPE_DISPLAY_SET_POSITIONV: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_DISPLAY_SIGNATURE);
            display->position_v = va_arg(args, int);
            return UBASE_ERR_NONE;
        }
        case UPIPE_DISPLAY_SET_CONTEXT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_DISPLAY_SIGNATURE);
            _upipe_display_set_context(upipe,va_arg(args,struct Context));
            return UBASE_ERR_NONE;
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** module manager static descriptor */
static struct upipe_mgr upipe_display_mgr = {
    .refcount = NULL,
    .signature = UPIPE_DISPLAY_SIGNATURE,

    .upipe_alloc = upipe_display_alloc,
    .upipe_input = upipe_display_input,

    .upipe_control = upipe_display_control,
    .upipe_mgr_control = NULL
};

/** @This returns the management structure for display pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_display_mgr_alloc(void)
{
    return &upipe_display_mgr;
}
