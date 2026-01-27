/*
 * Copyright (C) 2019 OpenHeadend S.A.R.L.
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Graph flow attributes
 */

#ifndef _UPIPE_MODULES_UREF_GRAPH_FLOW_H_
#define _UPIPE_MODULES_UREF_GRAPH_FLOW_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uref_attr.h"
#include "upipe/uref_flow.h"

/** @internal flow definition prefix for graph flow. */
#define UREF_GRAPH_FLOW_DEF "graph."

UREF_ATTR_STRING(graph_flow, name, "graph.name", graph name);
UREF_ATTR_STRING(graph_flow, color, "graph.color", graph color);
UREF_ATTR_VOID(graph_flow, stacked, "graph.stacked", graph is stacked);
UREF_ATTR_VOID(graph_flow, filled, "graph.filled", filled the graph);
UREF_ATTR_VOID(graph_flow, interpolated, "graph.interpolated",
               interpolate with the previous value);

static inline struct uref *uref_graph_flow_alloc_def(struct uref_mgr *mgr,
                                                     const char *name,
                                                     const char *color)
{
    struct uref *uref = uref_alloc_control(mgr);
    if (unlikely(!uref))
        return NULL;
    if (unlikely(!ubase_check(uref_flow_set_def(uref, UREF_GRAPH_FLOW_DEF)) ||
                 (name &&
                  !ubase_check(uref_graph_flow_set_name(uref, name))) ||
                 (color &&
                  !ubase_check(uref_graph_flow_set_color(uref, color))))) {
        uref_free(uref);
        return NULL;
    }
    return uref;
}

#ifdef __cplusplus
}
#endif
#endif
