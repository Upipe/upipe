/*
 * Copyright (C) 2018 OpenHeadend S.A.R.L.
 * Copyright (C) 2019-2020 EasyTools
 *
 * Authors: Clément Vasseur
 *          Arnaud de Turckheim
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
 * @short Upipe avfilter module
 */

#ifndef _UPIPE_AV_UPIPE_AVFILTER_H_
/** @hidden */
#define _UPIPE_AV_UPIPE_AVFILTER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_AVFILT_SIGNATURE UBASE_FOURCC('a', 'v', 'f', 'i')
#define UPIPE_AVFILT_SUB_SIGNATURE UBASE_FOURCC('a', 'v', 'f', 's')

/** @This extends upipe_command with specific commands for avfilt. */
enum upipe_avfilt_command {
    UPIPE_AVFILT_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** set the filter graph description (const char *) */
    UPIPE_AVFILT_SET_FILTERS_DESC,
    /** set the hardware config (const char *, const char *) */
    UPIPE_AVFILT_SET_HW_CONFIG,
};

/** @This converts @ref upipe_avfilt_command to a string.
 *
 * @param command command to convert
 * @return a string or NULL if invalid
 */
static inline const char *upipe_avfilt_command_str(int command)
{
    switch ((enum upipe_avfilt_command)command) {
        UBASE_CASE_TO_STR(UPIPE_AVFILT_SET_FILTERS_DESC);
        UBASE_CASE_TO_STR(UPIPE_AVFILT_SET_HW_CONFIG);
        case UPIPE_AVFILT_SENTINEL: break;
    }
    return NULL;
}

/** @This sets the filter graph description.
 *
 * @param upipe description structure of the pipe
 * @param filters filter graph description
 * @return an error code
 */
static inline int upipe_avfilt_set_filters_desc(struct upipe *upipe,
                                                const char *filters_desc)
{
    return upipe_control(upipe, UPIPE_AVFILT_SET_FILTERS_DESC,
                         UPIPE_AVFILT_SIGNATURE, filters_desc);
}

/** @This sets the hardware configuration.
 *
 * @param upipe description structure of the pipe
 * @param hw_type hardware type
 * @param hw_device hardware device (use NULL for default)
 * @return an error code
 */
static inline int upipe_avfilt_set_hw_config(struct upipe *upipe,
                                             const char *hw_type,
                                             const char *hw_device)
{
    return upipe_control(upipe, UPIPE_AVFILT_SET_HW_CONFIG,
                         UPIPE_AVFILT_SIGNATURE,
                         hw_type, hw_device);
}

/** @This returns the management structure for all avfilter pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_avfilt_mgr_alloc(void);

/** @This extends upipe_mgr_command with specific commands for avfilt. */
enum upipe_avfilt_mgr_command {
    UPIPE_AVFILT_MGR_SENTINEL = UPIPE_MGR_CONTROL_LOCAL,

    /** gets the pixel format name from flow def (struct uref *, const char **) */
    UPIPE_AVFILT_MGR_GET_PIXFMT_NAME
};

/** @This returns the pixel format name for the given flow definition.
 *
 * @param mgr pointer to manager
 * @param flow_def flow definition packet
 * @param name pixel format name
 * @return an error code
 */
static inline int upipe_avfilt_mgr_get_pixfmt_name(struct upipe_mgr *mgr,
                                                   struct uref *flow_def,
                                                   const char **name)
{
    return upipe_mgr_control(mgr, UPIPE_AVFILT_MGR_GET_PIXFMT_NAME,
                             UPIPE_AVFILT_SIGNATURE, flow_def, name);
}

#ifdef __cplusplus
}
#endif
#endif
