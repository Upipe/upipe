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

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_UREF_M3U_PLAYLIST_H_ */
