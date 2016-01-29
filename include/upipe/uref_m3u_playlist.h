/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * Authors: Arnaud de Turckheim
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

#ifndef _UPIPE_UREF_M3U_PLAYLIST_H_
# define _UPIPE_UREF_M3U_PLAYLIST_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uref_attr.h>

UREF_ATTR_UNSIGNED(m3u_playlist, seq_duration, "m3u.playlist.seq_duration",
                   sequence duration)
UREF_ATTR_UNSIGNED(m3u_playlist, seq_time, "m3u.playlist.seq_time",
                   sequence time)
UREF_ATTR_UNSIGNED(m3u_playlist, byte_range_len, "m3u.playlist.byte_range_len",
                      length of the sub range)
UREF_ATTR_UNSIGNED(m3u_playlist, byte_range_off, "m3u.playlist.byte_range_off",
                   offset of the sub range)

UREF_ATTR_STRING(m3u_playlist_key, method, "m3u.playlist.key.method",
                 key method);
UREF_ATTR_STRING(m3u_playlist_key, uri, "m3u.playlist.key.uri",
                 key uri);

static inline int uref_m3u_playlist_key_delete(struct uref *uref)
{
    int (*list[])(struct uref *) = {
        uref_m3u_playlist_key_delete_method,
        uref_m3u_playlist_key_delete_uri,
    };
    return uref_attr_delete_list(uref, list, UBASE_ARRAY_SIZE(list));
}

static inline int uref_m3u_playlist_delete(struct uref *uref)
{
    int (*list[])(struct uref *) = {
        uref_m3u_playlist_delete_seq_duration,
        uref_m3u_playlist_delete_seq_time,
        uref_m3u_playlist_delete_byte_range_len,
        uref_m3u_playlist_delete_byte_range_off,
        uref_m3u_playlist_key_delete,
    };
    return uref_attr_delete_list(uref, list, UBASE_ARRAY_SIZE(list));
}

static inline int uref_m3u_playlist_key_copy(struct uref *uref,
                                             struct uref *uref_src)
{
    int (*list[])(struct uref *, struct uref *) = {
        uref_m3u_playlist_key_copy_method,
        uref_m3u_playlist_key_copy_uri,
    };
    return uref_attr_copy_list(uref, uref_src, list, UBASE_ARRAY_SIZE(list));
}

static inline int uref_m3u_playlist_copy(struct uref *uref,
                                         struct uref *uref_src)
{
    int (*list[])(struct uref *, struct uref *) = {
        uref_m3u_playlist_copy_seq_duration,
        uref_m3u_playlist_copy_seq_time,
        uref_m3u_playlist_copy_byte_range_len,
        uref_m3u_playlist_copy_byte_range_off,
        uref_m3u_playlist_key_copy,
    };
    return uref_attr_copy_list(uref, uref_src, list, UBASE_ARRAY_SIZE(list));
}

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_UREF_M3U_PLAYLIST_H_ */
