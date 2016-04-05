/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
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

#ifndef _UPIPE_MODULES_UPIPE_HLS_PLAYLIST_H_
/** @hidden */
# define _UPIPE_MODULES_UPIPE_HLS_PLAYLIST_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_HLS_PLAYLIST_SIGNATURE UBASE_FOURCC('m','3','u','p')

/** @This extends @ref upipe_command with specific m3u playlist command. */
enum upipe_hls_playlist_command {
    UPIPE_HLS_PLAYLIST_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** get the current index (uint64_t *) */
    UPIPE_HLS_PLAYLIST_GET_INDEX,
    /** set the current index (uint64_t) */
    UPIPE_HLS_PLAYLIST_SET_INDEX,
    /** play */
    UPIPE_HLS_PLAYLIST_PLAY,
    /** go to the next index */
    UPIPE_HLS_PLAYLIST_NEXT,
    /** seek to this offset (uint64_t) */
    UPIPE_HLS_PLAYLIST_SEEK,
};

/** @This converts m3u playlist specific command to a string.
 *
 * @param cmd @ref upipe_hls_playlist_command to convert
 * @return the corresponding string or @ref NULL if not a valid
 * @ref upipe_hls_playlist_command
 */
static inline const char *upipe_hls_playlist_command_str(int cmd)
{
    switch ((enum upipe_hls_playlist_command)cmd) {
    UBASE_CASE_TO_STR(UPIPE_HLS_PLAYLIST_GET_INDEX);
    UBASE_CASE_TO_STR(UPIPE_HLS_PLAYLIST_SET_INDEX);
    UBASE_CASE_TO_STR(UPIPE_HLS_PLAYLIST_PLAY);
    UBASE_CASE_TO_STR(UPIPE_HLS_PLAYLIST_NEXT);
    UBASE_CASE_TO_STR(UPIPE_HLS_PLAYLIST_SEEK);
    case UPIPE_HLS_PLAYLIST_SENTINEL: break;
    }
    return NULL;
}

/** @This gets the current index in the playlist.
 *
 * @param upipe description structure of the pipe
 * @param index_p a pointer to the index
 * @return an error code
 */
static inline int upipe_hls_playlist_get_index(struct upipe *upipe,
                                               uint64_t *index_p)
{
    return upipe_control(upipe, UPIPE_HLS_PLAYLIST_GET_INDEX,
                         UPIPE_HLS_PLAYLIST_SIGNATURE, index_p);
}

/** @This sets the current index in the playlist.
 *
 * @param upipe description structure of the pipe
 * @param index index to set
 * @return an error code
 */
static inline int upipe_hls_playlist_set_index(struct upipe *upipe,
                                               uint64_t index)
{
    return upipe_control(upipe, UPIPE_HLS_PLAYLIST_SET_INDEX,
                         UPIPE_HLS_PLAYLIST_SIGNATURE, index);
}

/** @This plays the next item in the playlist.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static inline int upipe_hls_playlist_play(struct upipe *upipe)
{
    return upipe_control(upipe, UPIPE_HLS_PLAYLIST_PLAY,
                         UPIPE_HLS_PLAYLIST_SIGNATURE);
}

/** @This goes to the next element in the playlist.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static inline int upipe_hls_playlist_next(struct upipe *upipe)
{
    return upipe_control(upipe, UPIPE_HLS_PLAYLIST_NEXT,
                         UPIPE_HLS_PLAYLIST_SIGNATURE);
}

/** @This seeks into the playlist the corresponding media sequence for a
 * given offset.
 *
 * @param upipe description structure of the pipe
 * @param at offset to seek
 * @param offset_p filled with remaining offset to seek in the current item,
 * may be NULL.
 * @return an error code
 */
static inline int upipe_hls_playlist_seek(struct upipe *upipe, uint64_t at,
                                          uint64_t *offset_p)
{
    return upipe_control(upipe, UPIPE_HLS_PLAYLIST_SEEK,
                         UPIPE_HLS_PLAYLIST_SIGNATURE, at, offset_p);
}

/** @This extends @ref uprobe_event with specific m3u playlist events. */
enum uprobe_hls_playlist_event {
    UPROBE_HLS_PLAYLIST_SENTINEL = UPROBE_LOCAL,

    /** playlist was reloaded */
    UPROBE_HLS_PLAYLIST_RELOADED,
    /** the item has finished */
    UPROBE_HLS_PLAYLIST_ITEM_END,
};

/** @This converts hls playlist specific event to a string.
 *
 * @param event @ref uprobe_event to convert
 * @return a string or NULL if not a valid @ref uprobe_hls_playlist_event
 */
static inline const char *uprobe_hls_playlist_event_str(int event)
{
    switch ((enum uprobe_hls_playlist_event)event) {
    UBASE_CASE_TO_STR(UPROBE_HLS_PLAYLIST_RELOADED);
    UBASE_CASE_TO_STR(UPROBE_HLS_PLAYLIST_ITEM_END);
    case UPROBE_HLS_PLAYLIST_SENTINEL: break;
    }
    return NULL;
}

/** @This returns the m3u playlist manager.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_hls_playlist_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_MODULES_UPIPE_HLS_PLAYLIST_H_ */
