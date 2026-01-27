/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe GL sink cube animation
 */

#ifndef _UPIPE_GL_UPROBE_GL_SINK_CUBE_H_
/** @hidden */
#define _UPIPE_GL_UPROBE_GL_SINK_CUBE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"
#include "upipe-gl/upipe_gl_sink_common.h"

/** @This allocates a gl_sink_cube uprobe to take care of the
 * GL cube animation.
 *
 * @param next probe to test if this one doesn't catch the event
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_gl_sink_cube_alloc(struct uprobe *next);

#ifdef __cplusplus
}
#endif
#endif
