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

#include "upipe/uref_attr.h"

UREF_ATTR_UNSIGNED(m3u_playlist, seq_duration, "m3u.playlist.seq_duration",
                   sequence duration)
UREF_ATTR_UNSIGNED(m3u_playlist, byte_range_len, "m3u.playlist.byte_range_len",
                      length of the sub range)
UREF_ATTR_UNSIGNED(m3u_playlist, byte_range_off, "m3u.playlist.byte_range_off",
                   offset of the sub range)
UREF_ATTR_UNSIGNED(m3u_playlist, program_date_time, "m3u.playlist.program_date_time",
                   program date time)
UREF_ATTR_BOOL(m3u_playlist, discontinuity, "m3u.playlist.discontinuity",
               discontinuity)
UREF_ATTR_BOOL(m3u_playlist, gap, "m3u.playlist.gap", gap)

UREF_ATTR_STRING(m3u_playlist_key, method, "m3u.playlist.key.method",
                 key method);
UREF_ATTR_STRING(m3u_playlist_key, uri, "m3u.playlist.key.uri",
                 key uri);
UREF_ATTR_STRING(m3u_playlist_key, iv, "m3u.playlist.key.iv",
                 key initialization vector);
UREF_ATTR_STRING(m3u_playlist_map, uri, "m3u.playlist.map.uri", map uri);

UREF_ATTR_STRING(m3u_playlist_daterange, id,
                 "m3u.playlist.daterange.id",
                 date range id);
UREF_ATTR_UNSIGNED(m3u_playlist_daterange, start_date,
                   "m3u.playlist.daterange.start_date",
                   date range start date);
UREF_ATTR_UNSIGNED(m3u_playlist_daterange, end_date,
                   "m3u.playlist.daterange.end_date",
                   date range end date);
UREF_ATTR_UNSIGNED(m3u_playlist_daterange, duration,
                   "m3u.playlist.daterange.duration",
                   date range duration);
UREF_ATTR_UNSIGNED(m3u_playlist_daterange, planned_duration,
                   "m3u.playlist.daterange.planned_duration",
                   date range planned duration);
UREF_ATTR_OPAQUE(m3u_playlist_daterange, scte35_cmd,
                 "m3u.playlist.daterange.scte35_cmd",
                 date range SCTE-35 command data);
UREF_ATTR_OPAQUE(m3u_playlist_daterange, scte35_out,
                 "m3u.playlist.daterange.scte35_out",
                 date range SCTE-35 out data);
UREF_ATTR_OPAQUE(m3u_playlist_daterange, scte35_in,
                 "m3u.playlist.daterange.scte35_in",
                 date range SCTE-35 in data);

static inline int uref_m3u_playlist_key_delete(struct uref *uref)
{
    int (*list[])(struct uref *) = {
        uref_m3u_playlist_key_delete_method,
        uref_m3u_playlist_key_delete_uri,
        uref_m3u_playlist_key_delete_iv,
    };
    return uref_attr_delete_list(uref, list, UBASE_ARRAY_SIZE(list));
}

static inline int uref_m3u_playlist_map_delete(struct uref *uref)
{
    int (*list[])(struct uref *) = {
        uref_m3u_playlist_map_delete_uri,
    };
    return uref_attr_delete_list(uref, list, UBASE_ARRAY_SIZE(list));
}

static inline int uref_m3u_playlist_daterange_delete(struct uref *uref)
{
    int (*list[])(struct uref *) = {
        uref_m3u_playlist_daterange_delete_id,
        uref_m3u_playlist_daterange_delete_start_date,
        uref_m3u_playlist_daterange_delete_end_date,
        uref_m3u_playlist_daterange_delete_duration,
        uref_m3u_playlist_daterange_delete_planned_duration,
        uref_m3u_playlist_daterange_delete_scte35_cmd,
        uref_m3u_playlist_daterange_delete_scte35_out,
        uref_m3u_playlist_daterange_delete_scte35_in,
    };
    return uref_attr_delete_list(uref, list, UBASE_ARRAY_SIZE(list));
}

static inline int uref_m3u_playlist_delete(struct uref *uref)
{
    int (*list[])(struct uref *) = {
        uref_m3u_playlist_delete_seq_duration,
        uref_m3u_playlist_delete_byte_range_len,
        uref_m3u_playlist_delete_byte_range_off,
        uref_m3u_playlist_delete_program_date_time,
        uref_m3u_playlist_delete_discontinuity,
        uref_m3u_playlist_delete_gap,
        uref_m3u_playlist_key_delete,
        uref_m3u_playlist_map_delete,
        uref_m3u_playlist_daterange_delete,
    };
    return uref_attr_delete_list(uref, list, UBASE_ARRAY_SIZE(list));
}

static inline int uref_m3u_playlist_key_copy(struct uref *uref,
                                             struct uref *uref_src)
{
    int (*list[])(struct uref *, struct uref *) = {
        uref_m3u_playlist_key_copy_method,
        uref_m3u_playlist_key_copy_uri,
        uref_m3u_playlist_key_copy_iv,
    };
    return uref_attr_copy_list(uref, uref_src, list, UBASE_ARRAY_SIZE(list));
}

static inline int uref_m3u_playlist_map_copy(struct uref *uref,
                                             struct uref *uref_src)
{
    int (*list[])(struct uref *, struct uref *) = {
        uref_m3u_playlist_map_copy_uri,
    };
    return uref_attr_copy_list(uref, uref_src, list, UBASE_ARRAY_SIZE(list));
}

static inline int uref_m3u_playlist_daterange_copy(struct uref *uref,
                                                   struct uref *uref_src)
{
    int (*list[])(struct uref *, struct uref *) = {
        uref_m3u_playlist_daterange_copy_id,
        uref_m3u_playlist_daterange_copy_start_date,
        uref_m3u_playlist_daterange_copy_end_date,
        uref_m3u_playlist_daterange_copy_duration,
        uref_m3u_playlist_daterange_copy_planned_duration,
        uref_m3u_playlist_daterange_copy_scte35_cmd,
        uref_m3u_playlist_daterange_copy_scte35_out,
        uref_m3u_playlist_daterange_copy_scte35_in,
    };
    return uref_attr_copy_list(uref, uref_src, list, UBASE_ARRAY_SIZE(list));
}

static inline int uref_m3u_playlist_copy(struct uref *uref,
                                         struct uref *uref_src)
{
    int (*list[])(struct uref *, struct uref *) = {
        uref_m3u_playlist_copy_seq_duration,
        uref_m3u_playlist_copy_byte_range_len,
        uref_m3u_playlist_copy_byte_range_off,
        uref_m3u_playlist_copy_program_date_time,
        uref_m3u_playlist_copy_discontinuity,
        uref_m3u_playlist_copy_gap,
        uref_m3u_playlist_key_copy,
        uref_m3u_playlist_map_copy,
        uref_m3u_playlist_daterange_copy,
    };
    return uref_attr_copy_list(uref, uref_src, list, UBASE_ARRAY_SIZE(list));
}

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_UREF_M3U_PLAYLIST_H_ */
