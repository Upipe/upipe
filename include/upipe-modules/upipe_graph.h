/*
 * Copyright (C) 2019 OpenHeadend S.A.R.L.
 *
 * Authors: Arnaud de Turckheim
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation https (the
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
 * @short Upipe module to graph
 */

#ifndef _UPIPE_MODULES_UPIPE_GRAPH_H_
#define _UPIPE_MODULES_UPIPE_GRAPH_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>

struct upipe_mgr;

#define UPIPE_GRAPH_SIGNATURE   UBASE_FOURCC('g','r','p','h')
#define UPIPE_GRAPH_SUB_SIGNATURE   UBASE_FOURCC('g','r','p','H')

/** @This enumerates the graph control commands. */
enum upipe_graph_control {
    UPIPE_GRAPH_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** set the minimum of the graph (int64_t) */
    UPIPE_GRAPH_SET_MINIMUM,
    /** set the maximum of the graph (int64_t) */
    UPIPE_GRAPH_SET_MAXIMUM,
    /** set the history size (uint64_t) */
    UPIPE_GRAPH_SET_HISTORY,
    /** set the background color (const char *) */
    UPIPE_GRAPH_SET_COLOR,
};

/** @This sets the graph minimum value.
 *
 * @param upipe description structure of the pipe
 * @param min minimum value shown on the graph
 * @return an error code
 */
static inline int upipe_graph_set_minimum(struct upipe *upipe, int64_t min)
{
    return upipe_control(upipe, UPIPE_GRAPH_SET_MINIMUM,
                         UPIPE_GRAPH_SIGNATURE, min);
}

/** @This sets the graph maximum value.
 *
 * @param upipe description structure of the pipe
 * @param max maximum value shown on the graph
 * @return an error code
 */
static inline int upipe_graph_set_maximum(struct upipe *upipe, int64_t max)
{
    return upipe_control(upipe, UPIPE_GRAPH_SET_MAXIMUM,
                         UPIPE_GRAPH_SIGNATURE, max);
}

/** @This sets the history size of the graph pipe.
 *
 * @param upipe description structure of the pipe
 * @param size history size in number of elements
 * @return an error code
 */
static inline int upipe_graph_set_history(struct upipe *upipe, uint64_t size)
{
    return upipe_control(upipe, UPIPE_GRAPH_SET_HISTORY,
                         UPIPE_GRAPH_SIGNATURE, size);
}

/** @This sets the background color of the graph.
 *
 * @param upipe description structure of the pipe
 * @param color string representing rgb color (rgb(R, G, B))
 * @return an error code
 */
static inline int  upipe_graph_set_color(struct upipe *upipe, const char *color)
{
    return upipe_control(upipe, UPIPE_GRAPH_SET_COLOR,
                         UPIPE_GRAPH_SIGNATURE, color);
}

/** @This enumerates the graph input control commands. */
enum upipe_graph_sub_control {
    UPIPE_GRAPH_SUB_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** set the input graph color (const char *) */
    UPIPE_GRAPH_SUB_SET_COLOR,
    /** update the current value (int64_t) */
    UPIPE_GRAPH_SUB_SET_VALUE,
};

/** @This sets the input graph color.
 * The format for RGB is "rgb(R, G, B)" with R,G and B respectively the green,
 * red and blue value.
 * The format for RGBA is "rgba(R, G, B, A)" with R, G, B and A respectively
 * the green, red, blue and alpha value.
 *
 * @param upipe description structure of the pipe
 * @param color color string
 * @return an error code
 */
static inline int upipe_graph_sub_set_color(struct upipe *upipe,
                                            const char *color)
{
    return upipe_control(upipe, UPIPE_GRAPH_SUB_SET_COLOR,
                         UPIPE_GRAPH_SUB_SIGNATURE, color);
}

/** @This updates the graph input value.
 *
 * @param upipe description structure of the pipe
 * @param value the new value
 * @return an error code
 */
static inline int upipe_graph_sub_set_value(struct upipe *upipe,
                                            int64_t value)
{
    return upipe_control(upipe, UPIPE_GRAPH_SUB_SET_VALUE,
                         UPIPE_GRAPH_SUB_SIGNATURE, value);
}

/** @This returns the graph pipe manager.
 *
 * @return a pointer to the graph pipe manager.
 */
struct upipe_mgr *upipe_graph_mgr_alloc(void);

#ifdef __cpluscplus
}
#endif
#endif
