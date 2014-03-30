/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
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
 * @short Upipe avcodec encode wrapper module
 */

#ifndef _UPIPE_AV_UPIPE_AV_ENCODE_H_
/** @hidden */
#define _UPIPE_AV_UPIPE_AV_ENCODE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>
#include <upipe/uref_attr.h>

#define UPIPE_AVCENC_SIGNATURE UBASE_FOURCC('a', 'v', 'c', 'e')

UREF_ATTR_STRING(avcenc, codec_name, "avcenc.name", avcenc codec name)

/** @This extends upipe_command with specific commands for avcodec encode. */
enum upipe_avcenc_command {
    UPIPE_AVCENC_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** sets the content of an avcodec option (const char *, const char *) */
    UPIPE_AVCENC_SET_OPTION
};

/** @This sets the content of an avcodec option. It only has effect before the
 * first packet is sent.
 *
 * @param upipe description structure of the pipe
 * @param option name of the option
 * @param content content of the option
 * @return an error code
 */
static inline int upipe_avcenc_set_option(struct upipe *upipe,
                                          const char *option,
                                          const char *content)
{
    return upipe_control(upipe, UPIPE_AVCENC_SET_OPTION, UPIPE_AVCENC_SIGNATURE,
                         option, content);
}

/** @This returns the management structure for avcodec encoders.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_avcenc_mgr_alloc(void);

/** @This extends upipe_mgr_command with specific commands for avcenc. */
enum upipe_avcenc_mgr_command {
    UPIPE_AVCENC_MGR_SENTINEL = UPIPE_MGR_CONTROL_LOCAL,

    /** sets the flow definition from code name (struct uref *, const char *) */
    UPIPE_AVCENC_MGR_SET_FLOW_DEF_FROM_NAME
};

/** @This configures the given flow definition to be able to encode to the
 * av codec described by name.
 *
 * @param mgr pointer to manager
 * @param flow_def flow definition packet
 * @param name codec name
 * @return an error code
 */
int upipe_avcenc_mgr_set_flow_def_from_name(struct upipe_mgr *mgr,
                                            struct uref *flow_def,
                                            const char *name)
{
    return upipe_mgr_control(mgr, UPIPE_AVCENC_MGR_SET_FLOW_DEF_FROM_NAME,
                             UPIPE_AVCENC_SIGNATURE, flow_def, name);
}

#ifdef __cplusplus
}
#endif
#endif
