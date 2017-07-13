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

/** @This is the grid pipe signature. */
#define UPIPE_GRID_SIGNATURE   UBASE_FOURCC('g','r','i','d')

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

int upipe_grid_out_get_input(struct upipe *upipe, struct upipe **input_p);
int upipe_grid_out_set_input(struct upipe *upipe, struct upipe *input);

/** @This allocates a new grid output.
 *
 * @param upipe description structure of the pipe
 * @param uprobe structure used to raise events
 * @return an allocated sub pipe.
 */
struct upipe *upipe_grid_alloc_output(struct upipe *upipe,
                                      struct uprobe *uprobe);

#endif
