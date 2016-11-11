/*
 * Copyright (C) 2016 Open Broadcast Systems Ltd
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
 *
 * Authors: Rafaël Carré
 *          Christophe Massiot
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe module generating audio meters subpictures
 */

#ifndef _UPIPE_FILTERS_UPIPE_AUDIO_BAR_H_
/** @hidden */
#define _UPIPE_FILTERS_UPIPE_AUDIO_BAR_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_AUDIO_BAR_SIGNATURE UBASE_FOURCC('a','b','a','r')

/** @This returns the management structure for audiobar pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_audiobar_mgr_alloc(void);

/** @This extends upipe_command with specific commands for upipe_audiobar pipes.
 */
enum upipe_audiobar_command {
    UPIPE_AUDIOBAR_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** gets the value of the alpha channel (uint8_t *) */
    UPIPE_AUDIOBAR_GET_ALPHA,
    /** sets the value of the alpha channel (uint8_t) */
    UPIPE_AUDIOBAR_SET_ALPHA
};

/** @This gets the value of the alpha channel.
 *
 * @param upipe description structure of the pipe
 * @param alpha_p filled in with the value of the alpha channel
 * @return an error code
 */
static inline int upipe_audiobar_get_alpha(struct upipe *upipe,
        uint8_t *alpha_p)
{
    return upipe_control(upipe, UPIPE_AUDIOBAR_GET_ALPHA,
                         UPIPE_AUDIO_BAR_SIGNATURE, alpha_p);
}

/** @This sets the value of the alpha channel.
 *
 * @param upipe description structure of the pipe
 * @param alpha value of the alpha channel
 * @return an error code
 */
static inline int upipe_audiobar_set_alpha(struct upipe *upipe,
        uint8_t alpha)
{
    return upipe_control(upipe, UPIPE_AUDIOBAR_SET_ALPHA,
                         UPIPE_AUDIO_BAR_SIGNATURE, (unsigned)alpha);
}

#ifdef __cplusplus
}
#endif
#endif
