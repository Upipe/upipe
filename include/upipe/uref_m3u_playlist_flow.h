/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _UPIPE_UREF_M3U_PLAYLIST_FLOW_H_
# define _UPIPE_UREF_M3U_PLAYLIST_FLOW_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uref_attr.h"

UREF_ATTR_STRING(m3u_playlist_flow, type, "m3u.playlist.type", type)
UREF_ATTR_UNSIGNED(m3u_playlist_flow, target_duration,
                   "m3u.playlist.target_duration",
                   target duration)
UREF_ATTR_UNSIGNED(m3u_playlist_flow, media_sequence,
                   "m3u.playlist.media_sequence",
                   media sequence)
UREF_ATTR_UNSIGNED(m3u_playlist_flow, discontinuity_sequence,
                   "m3u.playlist.discontinuity_sequence",
                   discontinuity sequence)
UREF_ATTR_VOID(m3u_playlist_flow, endlist, "m3u.playlist.endlist",
               endlist)

static inline int uref_m3u_playlist_flow_delete(struct uref *uref)
{
    int (*list[])(struct uref *) = {
        uref_m3u_playlist_flow_delete_type,
        uref_m3u_playlist_flow_delete_target_duration,
        uref_m3u_playlist_flow_delete_media_sequence,
        uref_m3u_playlist_flow_delete_discontinuity_sequence,
        uref_m3u_playlist_flow_delete_endlist,
    };

    return uref_attr_delete_list(uref, list, UBASE_ARRAY_SIZE(list));
}

static inline int uref_m3u_playlist_flow_copy(struct uref *uref,
                                              struct uref *uref_src)
{
    int (*list[])(struct uref *, struct uref *) = {
        uref_m3u_playlist_flow_copy_type,
        uref_m3u_playlist_flow_copy_target_duration,
        uref_m3u_playlist_flow_copy_media_sequence,
        uref_m3u_playlist_flow_copy_discontinuity_sequence,
        uref_m3u_playlist_flow_copy_endlist,
    };

    return uref_attr_copy_list(uref, uref_src, list, UBASE_ARRAY_SIZE(list));
}

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_UREF_M3U_PLAYLIST_FLOW_H_ */
