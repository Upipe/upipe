/*****************************************************************************
 * upipe_flows.h: upipe structure to track input flows
 *****************************************************************************
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
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
 *****************************************************************************/

#ifndef _UPIPE_UPIPE_FLOWS_H_
/** @hidden */
#define _UPIPE_UPIPE_FLOWS_H_

#include <stdbool.h>

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/ulog.h>

/** @This initializes a upipe_flows structure.
 *
 * @param upipe_flows pointer to the struct ulist structure
 */
static inline void upipe_flows_init(struct ulist *upipe_flows)
{
    ulist_init(upipe_flows);
}

/** @This walks through a upipe_flows structure.
 *
 * @param upipe_flows pointer to a upipe_flows structure
 * @param uref iterator
 */
#define upipe_flows_foreach(upipe_flows, uref)                              \
    struct uchain *upipe_flows_uchain;                                      \
    for (upipe_flows_uchain = (upipe_flows)->first,                         \
             uref = uref_from_uchain(upipe_flows_uchain);                   \
         upipe_flows_uchain != NULL;                                        \
         upipe_flows_uchain = upipe_flows_uchain->next,                     \
             uref = uref_from_uchain(upipe_flows_uchain))

/** @This returns the uref defining a given flow.
 *
 * @param upipe_flows pointer to the upipe_flows structure
 * @param flow name of the flow
 * @return pointer to the uref defining the flow, or NULL if not found
 */
static inline struct uref *upipe_flows_get(struct ulist *upipe_flows,
                                           const char *flow)
{
    struct uref *uref;
    upipe_flows_foreach (upipe_flows, uref) {
        const char *uref_flow;
        if (unlikely(!uref_flow_get_name(uref, &uref_flow)))
            continue;
        if (unlikely(!strcmp(flow, uref_flow)))
            return uref;
    }
    return NULL;
}

/** @This returns the flow definition of a given flow.
 *
 * @param upipe_flows pointer to the upipe_flows structure
 * @param flow name of the flow
 * @param def_p reference to a string, will be written with the flow definition
 * @return false if the flow was not found
 */
static inline bool upipe_flows_get_definition(struct ulist *upipe_flows,
                                              const char *flow,
                                              const char **def_p)
{
    struct uref *uref = upipe_flows_get(upipe_flows, flow);
    if (unlikely(uref == NULL)) return false;
    return uref_flow_get_definition(uref, def_p);
}

/** @This deletes the flow definition of a given flow.
 *
 * @param upipe_flows pointer to the struct ulist structure
 * @param flow name of the flow
 * @return true if the flow was found and deleted
 */
static inline bool upipe_flows_delete(struct ulist *upipe_flows,
                                      const char *flow)
{
    struct uchain *uchain;
    ulist_delete_foreach (upipe_flows, uchain) {
        struct uref *uref = uref_from_uchain(uchain);
        const char *uref_flow;
        if (likely(uref_flow_get_name(uref, &uref_flow))) {
            if (unlikely(!strcmp(flow, uref_flow))) {
                ulist_delete(upipe_flows, uchain);
                uref_release(uref);
                return true;
            }
        }
    }
    return false;
}

/** @This sets the flow definition of a given flow.
 *
 * @param upipe_flows pointer to the upipe_flows structure
 * @param uref uref structure defining the flow
 * @return false in case of error
 */
static inline bool upipe_flows_set(struct ulist *upipe_flows, struct uref *uref)
{
    const char *flow;
    if (unlikely(!uref_flow_get_name(uref, &flow))) return false;
    upipe_flows_delete(upipe_flows, flow);
    ulist_add(upipe_flows, uref_to_uchain(uref));
    return true;
}

/** @This checks an incoming uref for validity and control messages.
 *
 * @param upipe_flows pointer to the upipe_flows structure
 * @param ulog structure used to output logs
 * @param uref_mgr management structure allowing to create urefs
 * @param uref uref structure to check
 * @return false if the uref is invalid and should be dropped
 */
static inline bool upipe_flows_input(struct ulist *upipe_flows,
                                     struct ulog *ulog,
                                     struct uref_mgr *uref_mgr,
                                     struct uref *uref)
{
    const char *flow, *def;
    if (unlikely(uref_mgr == NULL)) {
        ulog_warning(ulog, "received a buffer without a uref mgr");
        return false;
    }

    if (unlikely(!uref_flow_get_name(uref, &flow))) {
        ulog_warning(ulog, "received a buffer outside of a flow");
        return false;
    }

    if (unlikely(uref_flow_get_definition(uref, &def))) {
        struct uref *new_uref = uref_dup(uref_mgr, uref);
        if (likely(new_uref != NULL)) {
            upipe_flows_set(upipe_flows, new_uref);
            ulog_debug(ulog, "flow definition for %s: %s", flow, def);
        } else
            ulog_aerror(ulog);
    }
    else if (unlikely(!upipe_flows_get_definition(upipe_flows, flow, &def))) {
        ulog_warning(ulog, "received a buffer without a flow definition");
        return false;
    }
    else if (unlikely(uref_flow_get_delete(uref)))
        upipe_flows_delete(upipe_flows, flow);

    return true;
}

/** @This walks through a upipe_flows structure to replay all flow definitions.
 *
 * @param upipe_flows pointer to a upipe_flows structure
 * @param ulog structure used to output logs
 * @param uref_mgr management structure allowing to create urefs
 * @param uref name of the new uref flow definition to use in action
 * @param action line of code to execute for every new uref
 */
#define upipe_flows_foreach_replay(upipe_flows, ulog, uref_mgr, uref,       \
                                   action)                                  \
    struct uref *upipe_flows_replay_uref;                                   \
    upipe_flows_foreach (upipe_flows, upipe_flows_replay_uref) {            \
        struct uref *uref = uref_dup(uref_mgr, upipe_flows_replay_uref);    \
        if (likely(uref != NULL))                                           \
            action;                                                         \
        else                                                                \
            ulog_aerror(ulog);                                              \
    }

/** @This walks through a upipe_flows structure to play flow deletions.
 *
 * @param upipe_flows pointer to a upipe_flows structure
 * @param ulog structure used to output logs
 * @param uref_mgr management structure allowing to create urefs
 * @param uref name of the new uref flow deletion to use in action
 * @param action line of code to execute for every new uref
 */
#define upipe_flows_foreach_delete(upipe_flows, ulog, uref_mgr, uref,       \
                                   action)                                  \
    struct uref *upipe_flows_delete_uref;                                   \
    upipe_flows_foreach (upipe_flows, upipe_flows_delete_uref) {            \
        const char *flow;                                                   \
        bool ret = uref_flow_get_name(upipe_flows_delete_uref, &flow);      \
        assert(ret);                                                        \
        struct uref *uref = uref_flow_alloc_delete(uref_mgr, flow);         \
        if (likely(uref != NULL))                                           \
            action;                                                         \
        else                                                                \
            ulog_aerror(ulog);                                              \
    }

/** @This cleans up a struct ulist structure.
 *
 * @param upipe_flows pointer to the upipe_flows structure
 */
static inline void upipe_flows_clean(struct ulist *upipe_flows)
{
    struct uchain *uchain;
    ulist_delete_foreach (upipe_flows, uchain) {
        struct uref *uref = uref_from_uchain(uchain);
        ulist_delete(upipe_flows, uchain);
        uref_release(uref);
    }
}

#endif
