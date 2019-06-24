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
 * @short Graph flow attributes
 */

#ifndef _UPIPE_MODULES_UREF_GRAPH_FLOW_H_
#define _UPIPE_MODULES_UREF_GRAPH_FLOW_H_
#ifdef __cplusplus
extern "C" {
#endif

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
