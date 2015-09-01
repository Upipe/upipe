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

/** @file
 * @short Upipe module to play output of a m3u reader pipe
 */

#ifndef _UPIPE_MODULES_UPIPE_M3U_PLAYLIST_H_
/** @hidden */
# define _UPIPE_MODULES_UPIPE_M3U_PLAYLIST_H_
#ifdef __cplusplus
extern "C" {
#endif

#define UPIPE_M3U_PLAYLIST_SIGNATURE UBASE_FOURCC('m','3','u','p')

/** @This extends @ref upipe_command with specific m3u playlist command. */
enum upipe_m3u_playlist_command {
    UPIPE_M3U_PLAYLIST_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** set the source manager */
    UPIPE_M3U_PLAYLIST_SET_SOURCE_MGR,
    /** get the current index */
    UPIPE_M3U_PLAYLIST_GET_INDEX,
    /** set the current index */
    UPIPE_M3U_PLAYLIST_SET_INDEX,
};

/** @This converts m3u playlist specific command to a string.
 *
 * @param cmd @ref upipe_m3u_playlist_command to convert
 * @return the corresponding string or @ref NULL if not a valid
 * @ref upipe_m3u_playlist_command
 */
static inline const char *upipe_m3u_playlist_command_str(int cmd)
{
    switch ((enum upipe_m3u_playlist_command)cmd) {
    UBASE_CASE_TO_STR(UPIPE_M3U_PLAYLIST_SET_SOURCE_MGR);
    UBASE_CASE_TO_STR(UPIPE_M3U_PLAYLIST_GET_INDEX);
    UBASE_CASE_TO_STR(UPIPE_M3U_PLAYLIST_SET_INDEX);
    case UPIPE_M3U_PLAYLIST_SENTINEL: break;
    }
    return NULL;
}

/** @This sets the source manager used by m3u playlist to open files.
 *
 * @param upipe description structure of the pipe
 * @param mgr pointer to upipe manager
 * @return an error code
 */
static inline int upipe_m3u_playlist_set_source_mgr(struct upipe *upipe,
                                                    struct upipe_mgr *mgr)
{
    return upipe_control(upipe, UPIPE_M3U_PLAYLIST_SET_SOURCE_MGR,
                         UPIPE_M3U_PLAYLIST_SIGNATURE, mgr);
}

/** @This gets the current index in the playlist.
 *
 * @param upipe description structure of the pipe
 * @param index_p a pointer to the index
 * @return an error code
 */
static inline int upipe_m3u_playlist_get_index(struct upipe *upipe,
                                               uint64_t *index_p)
{
    return upipe_control(upipe, UPIPE_M3U_PLAYLIST_GET_INDEX,
                         UPIPE_M3U_PLAYLIST_SIGNATURE, index_p);
}

/** @This sets the current index in the playlist.
 *
 * @param upipe description structure of the pipe
 * @param index index to set
 * @return an error code
 */
static inline int upipe_m3u_playlist_set_index(struct upipe *upipe,
                                               uint64_t index)
{
    return upipe_control(upipe, UPIPE_M3U_PLAYLIST_SET_INDEX,
                         UPIPE_M3U_PLAYLIST_SIGNATURE, index);
}

/** @This returns the m3u playlist manager.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_m3u_playlist_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_MODULES_UPIPE_M3U_PLAYLIST_H_ */
