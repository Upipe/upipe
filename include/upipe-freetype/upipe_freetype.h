/*
 * Copyright (C) 2016 Open Broadcast Systems Ltd
 * Copyright (C) 2018-2019 OpenHeadend S.A.R.L.
 *
 * Authors: Rafaël Carré
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
 * @short freetype2 based text renderer
 */

#ifndef _UPIPE_FREETYPE_UPIPE_FREETYPE_H_
# define _UPIPE_FREETYPE_UPIPE_FREETYPE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/ubase.h"
#include "upipe/upipe.h"

#define UPIPE_FREETYPE_SIGNATURE UBASE_FOURCC('f','r','t','2')

/** @This enumerates the freetype probe events. */
enum uprobe_freetype_event {
    /** sentinel */
    UPROBE_FREETYPE_SENTINEL = UPROBE_LOCAL,

    /** new input text received (const char *) */
    UPROBE_FREETYPE_NEW_TEXT,
};

/** @This enumerates the freetype pipe commands. */
enum upipe_freetype_command {
    /** sentinel */
    UPIPE_FREETYPE_SENTINEL = UPIPE_CONTROL_LOCAL,
    /** get the string bounding box (const char *, struct upipe_freetype_bbox *)
     */
    UPIPE_FREETYPE_GET_BBOX,
    /** set the freetype pixel size (unsigned) */
    UPIPE_FREETYPE_SET_PIXEL_SIZE,
    /** set the baseline position in the buffer (int64_t, int64_t) */
    UPIPE_FREETYPE_SET_BASELINE,
    /** get the current text (const char **) */
    UPIPE_FREETYPE_GET_TEXT,
    /** get the font global metrics (struct upipe_freetype_metrics *) */
    UPIPE_FREETYPE_GET_METRICS,
    /** get a string advance value (const char *, uint64_t *, uint64_t *) */
    UPIPE_FREETYPE_GET_ADVANCE,
};

/** @This describes a string bounding box. */
struct upipe_freetype_bbox {
    /** horizontal position */
    int x;
    /** vertical position */
    int y;
    /** box height */
    uint64_t height;
    /** box width */
    uint64_t width;
};

/** @This gets the bounding box for a string at the current pixel size.
 *
 * @param upipe description structure of the pipe
 * @param str a string
 * @param bbox_p filled with the x and y min and max of the rendered string
 * @return an error code
 */
static inline int upipe_freetype_get_bbox(struct upipe *upipe,
                                          const char *str,
                                          struct upipe_freetype_bbox *bbox_p)
{
    return upipe_control(upipe, UPIPE_FREETYPE_GET_BBOX,
                         UPIPE_FREETYPE_SIGNATURE, str, bbox_p);
}

/** @This describes the global font metrics. */
struct upipe_freetype_metrics {
    int units_per_EM;
    struct {
        long min;
        long max;
    } x;
    struct {
        long min;
        long max;
    } y;
};

/** @This gets the global font metrics.
 *
 * @param upipe description structure of the pipe
 * @param metrics filled with the string metrics
 * @return an error code
 */
static inline int
upipe_freetype_get_metrics(struct upipe *upipe,
                           struct upipe_freetype_metrics *metrics)
{
    return upipe_control(upipe, UPIPE_FREETYPE_GET_METRICS,
                         UPIPE_FREETYPE_SIGNATURE, metrics);
}

/** @This gets the advance value for a string.
 *
 * @param upipe description structure of the pipe
 * @param str string to get the advance value
 * @param advance_p filled with the compute advance value
 * @param units_per_EM_p filled with the units per EM of the font
 * @return an error code
 */
static inline int
upipe_freetype_get_advance(struct upipe *upipe, const char *str,
                           uint64_t *advance_p, uint64_t *units_per_EM_p)
{
    return upipe_control(upipe, UPIPE_FREETYPE_GET_ADVANCE,
                         UPIPE_FREETYPE_SIGNATURE,
                         str, advance_p, units_per_EM_p);
}

/** @This sets the freetype pixel size.
 *
 * @param upipe description structure of the pipe
 * @param pixel_size pixel size to set
 * @return an error code
 */
static inline int upipe_freetype_set_pixel_size(struct upipe *upipe,
                                                unsigned pixel_size)
{
    return upipe_control(upipe, UPIPE_FREETYPE_SET_PIXEL_SIZE,
                         UPIPE_FREETYPE_SIGNATURE, pixel_size);
}

/** @This sets the baseline start position.
 *
 * @param upipe description structure of the pipe
 * @param xoff offset from the left of the buffer
 * @param yoff offset from the top of the buffer
 * @return an error code
 */
static inline int upipe_freetype_set_baseline(struct upipe *upipe,
                                              int64_t xoff, int64_t yoff)
{
    return upipe_control(upipe, UPIPE_FREETYPE_SET_BASELINE,
                         UPIPE_FREETYPE_SIGNATURE, xoff, yoff);
}

/** @This gets the current text.
 *
 * @param upipe description structure of the pipe
 * @param text_p filled with the current text
 * @return an error code
 */
static inline int upipe_freetype_get_text(struct upipe *upipe,
                                          const char **text_p)
{
    return upipe_control(upipe, UPIPE_FREETYPE_GET_TEXT,
                         UPIPE_FREETYPE_SIGNATURE, text_p);
}

/** @This returns the freetype pipes manager.
 *
 * @return a pointer to the freetype pipes manager
 */
struct upipe_mgr *upipe_freetype_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_FREETYPE_UPIPE_FREETYPE_H_ */
