/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe GL sink animation
 */

#ifndef _UPIPE_GL_UPROBE_GL_SINK_H_
/** @hidden */
#define _UPIPE_GL_UPROBE_GL_SINK_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"
#include "upipe-gl/upipe_gl_sink_common.h"

/** @This allocates a gl_sink uprobe to take care of the
 * GL animation.
 *
 * @param next probe to test if this one doesn't catch the event
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_gl_sink_alloc(struct uprobe *next);

#ifdef __cplusplus
}
#endif
#endif
