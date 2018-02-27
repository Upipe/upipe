/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
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

#ifndef _UPIPE_MODULES_UPIPE_GRID_H_
#define _UPIPE_MODULES_UPIPE_GRID_H_

#include <upipe/upipe.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @This is the grid pipe signature. */
#define UPIPE_GRID_SIGNATURE        UBASE_FOURCC('g','r','i','d')
/** @This is the grid input pipe signature. */
#define UPIPE_GRID_IN_SIGNATURE     UBASE_FOURCC('g','r','d','i')
/** @This is the grid output pipe signature. */
#define UPIPE_GRID_OUT_SIGNATURE    UBASE_FOURCC('g','r','d','o')

/** @This returns grid pipe manager.
 *
 * @return a pointer to the pipe manager
 */
struct upipe_mgr *upipe_grid_mgr_alloc(void);

/** @This allocates a new grid input.
 *
 * @param upipe description structure of the pipe
 * @param uprobe structure used to raise events
 * @return an allocated sub pipe.
 */
struct upipe *upipe_grid_alloc_input(struct upipe *upipe,
                                     struct uprobe *uprobe);

/** @This enumerates the grid output control commands. */
enum upipe_grid_out_command {
    /** sentinel */
    UPIPE_GRID_OUT_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** get the grid output input pipe (struct upipe **) */
    UPIPE_GRID_OUT_GET_INPUT,
    /** set the grid output input pipe (struct upipe *) */
    UPIPE_GRID_OUT_SET_INPUT,
    /** iterate the grid input of a grid output (struct upipe **) */
    UPIPE_GRID_OUT_ITERATE_INPUT,
};

/** @This allocates a new grid output.
 *
 * @param upipe description structure of the pipe
 * @param uprobe structure used to raise events
 * @return an allocated sub pipe.
 */
struct upipe *upipe_grid_alloc_output(struct upipe *upipe,
                                      struct uprobe *uprobe);

/** @This sets the input of a grid output pipe.
 *
 * @param upipe description structure of the pipe
 * @param input description of the input pipe to set
 * @return an error code
 */
static inline int upipe_grid_out_set_input(struct upipe *upipe,
                                           struct upipe *input)
{
    return upipe_control(upipe, UPIPE_GRID_OUT_SET_INPUT,
                         UPIPE_GRID_OUT_SIGNATURE, input);
}

/** @This gets the current input of a grid output pipe.
 *
 * @param upipe description structure of the pipe
 * @param input_p filled with the input pipe
 * @return an error code
 */
static inline int upipe_grid_out_get_input(struct upipe *upipe,
                                           struct upipe **input_p)
{
    return upipe_control(upipe, UPIPE_GRID_OUT_GET_INPUT,
                         UPIPE_GRID_OUT_SIGNATURE, input_p);
}

/** @This iterates the inputs of a grid output pipe.
 *
 * @param upipe description structure of the output pipe
 * @param input_p filled with the next input pipe
 * @return an error code
 */
static inline int upipe_grid_out_iterate_input(struct upipe *upipe,
                                               struct upipe **input_p)
{
    return upipe_control(upipe, UPIPE_GRID_OUT_ITERATE_INPUT,
                         UPIPE_GRID_OUT_SIGNATURE, input_p);
}

#ifdef __cplusplus
}
#endif
#endif
