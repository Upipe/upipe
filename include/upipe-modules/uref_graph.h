/*
 * Copyright (C) 2019 OpenHeadend S.A.R.L.
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Graph attributes
 */

#ifndef _UPIPE_MODULES_UREF_GRAPH_H_
#define _UPIPE_MODULES_UREF_GRAPH_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uref_attr.h"

UREF_ATTR_INT(graph, value, "graph.value", graph value);

#ifdef __cplusplus
}
#endif
#endif
