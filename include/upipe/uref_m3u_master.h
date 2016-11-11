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

#ifndef _UPIPE_UREF_M3U_MASTER_H_
# define _UPIPE_UREF_M3U_MASTER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uref_attr.h>

UREF_ATTR_UNSIGNED(m3u_master, bandwidth, "m3u.master.bandwidth",
                   bits per second)
UREF_ATTR_STRING(m3u_master, codecs, "m3u.master.codecs", codecs)
UREF_ATTR_STRING(m3u_master, resolution, "m3u.master.resolution", resolution)
UREF_ATTR_STRING(m3u_master, audio, "m3u.master.audio", audio group)
UREF_ATTR_STRING(m3u_master, media_type, "m3u.master.media.type", media type)
UREF_ATTR_VOID(m3u_master, media_default, "m3u.master.media.default",
               media default)
UREF_ATTR_VOID(m3u_master, media_autoselect, "m3u.master.media.autoselect",
               auto select)
UREF_ATTR_STRING(m3u_master, media_name, "m3u.master.media.name",
                 media name)
UREF_ATTR_STRING(m3u_master, media_group, "m3u.master.media.group",
                 media group id)

static inline int uref_m3u_master_delete(struct uref *uref)
{
    int (*list[])(struct uref *) = {
        uref_m3u_master_delete_bandwidth,
        uref_m3u_master_delete_codecs,
        uref_m3u_master_delete_resolution,
        uref_m3u_master_delete_audio,
        uref_m3u_master_delete_media_type,
        uref_m3u_master_delete_media_default,
        uref_m3u_master_delete_media_autoselect,
        uref_m3u_master_delete_media_name,
        uref_m3u_master_delete_media_group,
    };
    return uref_attr_delete_list(uref, list, UBASE_ARRAY_SIZE(list));
}

static inline int uref_m3u_master_copy(struct uref *uref,
                                       struct uref *uref_src)
{
    int (*list[])(struct uref *, struct uref *) = {
        uref_m3u_master_copy_bandwidth,
        uref_m3u_master_copy_codecs,
        uref_m3u_master_copy_resolution,
        uref_m3u_master_copy_audio,
        uref_m3u_master_copy_media_type,
        uref_m3u_master_copy_media_default,
        uref_m3u_master_copy_media_autoselect,
        uref_m3u_master_copy_media_name,
        uref_m3u_master_copy_media_group,
    };
    return uref_attr_copy_list(uref, uref_src, list, UBASE_ARRAY_SIZE(list));
}

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_UREF_M3U_MASTER_H_ */
