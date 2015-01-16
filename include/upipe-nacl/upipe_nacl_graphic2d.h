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
 * @short Upipe NaCl module to play video frames
 */

#ifndef _UPIPE_MODULES_UPIPE_NACL_GRAPHIC2D_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_NACL_GRAPHIC2D_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>
#include <ppapi/c/pp_resource.h>
#include <ppapi/c/pp_size.h>

#define UPIPE_NACL_GRAPHIC2D_SIGNATURE UBASE_FOURCC('d','i','s','p')

/** @This returns the management structure for nacl_graphic2d pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_nacl_graphic2d_mgr_alloc(void);

enum upipe_nacl_graphic2d_command {

    UPIPE_NACL_GRAPHIC2D_SENTINEL = UPIPE_CONTROL_LOCAL,

    UPIPE_NACL_GRAPHIC2D_SET_POSITIONH,

    UPIPE_NACL_GRAPHIC2D_GET_POSITIONH,

    UPIPE_NACL_GRAPHIC2D_SET_POSITIONV,

    UPIPE_NACL_GRAPHIC2D_GET_POSITIONV,

    UPIPE_NACL_GRAPHIC2D_SET_CONTEXT,
};


struct Context{
  PP_Resource ctx;
  struct PP_Size size;
  int bound;
  int quit;
  uint8_t* cell_in;
  uint8_t* cell_out;
};

static inline struct upipe *_upipe_nacl_graphic2d_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe, PP_Resource image, PP_Resource loop)
{
    return upipe_alloc(mgr, uprobe, UPIPE_NACL_GRAPHIC2D_SIGNATURE, image, loop);
}

/** @This sets the H position of the pipe.
 *
 * @param upipe description structure of the pipe
 * @param h horizontal position
 * @return an error code
 */
static inline int upipe_nacl_graphic2d_set_hposition(struct upipe *upipe,
                                                       int h)
{
    return upipe_control(upipe, UPIPE_NACL_GRAPHIC2D_SET_POSITIONH,
                         UPIPE_NACL_GRAPHIC2D_SIGNATURE, h);
}

/** @This sets the V position of the pipe.
 *
 * @param upipe description structure of the pipe
 * @param v vertical position
 * @return an error code
 */
static inline int upipe_nacl_graphic2d_set_vposition(struct upipe *upipe,
                                                       int v)
{
    return upipe_control(upipe, UPIPE_NACL_GRAPHIC2D_SET_POSITIONV,
                         UPIPE_NACL_GRAPHIC2D_SIGNATURE, v);
}

/** @This sets the context of the pipe.
 *
 * @param upipe description structure of the pipe
 * @param v vertical position
 * @return an error code
 */

static inline int upipe_nacl_graphic2d_set_context(struct upipe *upipe, struct Context context)
{
    return upipe_control(upipe, UPIPE_NACL_GRAPHIC2D_SET_CONTEXT, UPIPE_NACL_GRAPHIC2D_SIGNATURE, context);
}

#ifdef __cplusplus
}
#endif
#endif
