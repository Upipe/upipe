/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
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
 * @short probe catching need_update events and creating subpipes
 *
 * The probe catches the need_update events, checks whether it is necessary
 * to output the flow, and allocates a subpipe.
 *
 * In case of a change of configuration, or if flows are added or deleted,
 * the selections are reconsidered.
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_program_flow.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_helper_uprobe.h>
#include <upipe/uprobe_select_flows.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/upipe.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include <assert.h>

#define STRINGIFY(x) #x
#define UINT64_MAX_STR STRINGIFY(UINT64_MAX)

/** @This is a super-set of the uprobe structure with additional local
 * members. */
struct uprobe_selflow {
    /** type of flows to filter */
    enum uprobe_selflow_type type;
    /** probe to give to subpipes */
    struct uprobe *subprobe;
    /** user configuration */
    char *flows;
    /** true if the user specified auto, regardless of what happened after */
    bool auto_cfg;
    /** true if at least one program is selected */
    bool has_selection;

    /** list of potential subpipes */
    struct ulist subs;

    /** structure exported to modules */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_selflow, uprobe)

/** @This defines a potential subpipe. */
struct uprobe_selflow_sub {
    /** structure for double-linked lists */
    struct uchain uchain;
    /** pointer to super-probe */
    struct uprobe_selflow *uprobe_selflow;

    /** pointer to split pipe that emitted the flow id */
    struct upipe *split_pipe;
    /** flow id declared by the split pipe */
    uint64_t flow_id;

    /** flow definition */
    struct uref *flow_def;
    /** pointer to optional subpipe, if the flow is selected */
    struct upipe *subpipe;
};

/** @This returns the high-level uprobe_selflow_sub structure.
 *
 * @param uchain pointer to the uchain structure wrapped into the
 * uprobe_selflow_sub
 * @return pointer to the uprobe_selflow_sub structure
 */
static inline struct uprobe_selflow_sub *
    uprobe_selflow_sub_from_uchain(struct uchain *uchain)
{
    return container_of(uchain, struct uprobe_selflow_sub, uchain);
}

/** @This returns the uchain structure used for FIFO, LIFO and lists.
 *
 * @param uprobe_selflow_sub uprobe_selflow_sub structure
 * @return pointer to the uchain structure
 */
static inline struct uchain *
    uprobe_selflow_sub_to_uchain(struct uprobe_selflow_sub *s)
{
    return &s->uchain;
}

/** @internal @This checks if a flow definition matches our flow type.
 *
 * @param uprobe pointer to probe
 * @param def flow definition string
 * @return true if the flow matches
 */
static bool uprobe_selflow_check_def(struct uprobe *uprobe, const char *def)
{
    struct uprobe_selflow *uprobe_selflow = uprobe_selflow_from_uprobe(uprobe);
    switch (uprobe_selflow->type) {
        case UPROBE_SELFLOW_VOID:
            return !ubase_ncmp(def, "void.");
        case UPROBE_SELFLOW_PIC: {
            const char *pos;
            return (!ubase_ncmp(def, "pic.") && ubase_ncmp(def, "pic.sub.")) ||
                   ((pos = strstr(def, ".pic.")) != NULL &&
                    ubase_ncmp(pos, ".pic.sub."));
        }
        case UPROBE_SELFLOW_SOUND:
            return !ubase_ncmp(def, "sound.") || strstr(def, ".sound.") != NULL;
        case UPROBE_SELFLOW_SUBPIC:
            return !ubase_ncmp(def, "pic.sub.") ||
                   strstr(def, ".pic.sub.") != NULL;
        default:
            return false;
    }
}

/** @internal @This returns whether the given flow is selected by the user.
 *
 * @param uprobe pointer to probe
 * @param flow_id flow ID
 * @param flow_def flow definition packet
 * @return true if the flow is selected
 */
static bool uprobe_selflow_check(struct uprobe *uprobe, uint64_t flow_id,
                                 struct uref *flow_def)
{
    struct uprobe_selflow *uprobe_selflow = uprobe_selflow_from_uprobe(uprobe);
    if (!strcmp(uprobe_selflow->flows, "all") ||
        !strcmp(uprobe_selflow->flows, "auto"))
        return true;

    /* comma-separated list of subs */
    const char *flows = uprobe_selflow->flows;
    uint64_t found;
    char attr[strlen(flows) + 1];
    char value[strlen(flows) + 1];
    int consumed;
    int tmp;
    while ((tmp = sscanf(flows, "%"PRIu64",%n", &found, &consumed)) >= 1 ||
           sscanf(flows, "%[a-zA-Z]=%[^,],%n", attr, value,
                  &consumed) >= 2) {
        flows += consumed;
        if (tmp > 0) {
            if (found == flow_id)
                return true;
        } else if (!strcmp(attr, "lang")) {
            const char *lang;
            if (uref_flow_get_lang(flow_def, &lang) && !strcmp(lang, value))
                return true;
        } else if (!strcmp(attr, "name")) {
            const char *name;
            if (uref_program_flow_get_name(flow_def, &name) &&
                !strcmp(name, value))
                 return true;
        }
    }
    return false;
}

/** @internal @This sets the flows to select.
 *
 * @param uprobe pointer to probe
 * @param flows comma-separated list of flows to select, terminated by a
 * comma, or "auto" to automatically select the first flow, or "all"
 */
static void uprobe_selflow_set_internal(struct uprobe *uprobe,
                                        const char *flows)
{
    struct uprobe_selflow *uprobe_selflow = uprobe_selflow_from_uprobe(uprobe);
    free(uprobe_selflow->flows);
    uprobe_selflow->flows = strdup(flows);
    if (unlikely(uprobe_selflow->flows == NULL)) {
        uprobe_throw_fatal(uprobe, NULL, UPROBE_ERR_ALLOC);
        return;
    }

    struct uchain *uchain;
    ulist_foreach (&uprobe_selflow->subs, uchain) {
        struct uprobe_selflow_sub *sub = uprobe_selflow_sub_from_uchain(uchain);
        if (uprobe_selflow_check(uprobe, sub->flow_id, sub->flow_def)) {
            if (sub->subpipe == NULL) {
                sub->subpipe = upipe_flow_alloc_sub(sub->split_pipe,
                    uprobe_pfx_adhoc_alloc_va(uprobe_selflow->subprobe,
                                              UPROBE_LOG_DEBUG,
                                              "flow %"PRIu64, sub->flow_id),
                    sub->flow_def);
                if (unlikely(sub->subpipe == NULL))
                    uprobe_throw_fatal(uprobe, sub->split_pipe,
                                       UPROBE_ERR_ALLOC);
            }
        } else if (sub->subpipe != NULL) {
            upipe_release(sub->subpipe);
            sub->subpipe = NULL;
        }
    }
}

/** @internal @This sets the flows to select, with printf-style syntax.
 *
 * @param uprobe pointer to probe
 * @param format of the syntax, followed by optional arguments
 */
static void uprobe_selflow_set_internal_va(struct uprobe *uprobe,
                                           const char *format, ...)
{
    UBASE_VARARG(uprobe_selflow_set_internal(uprobe, string))
}

/** @internal @This checks that there is at least one selected flow, or
 * otherwise selects a new flow.
 *
 * @param uprobe pointer to probe
 */
static void uprobe_selflow_check_auto(struct uprobe *uprobe)
{
    struct uprobe_selflow *uprobe_selflow = uprobe_selflow_from_uprobe(uprobe);
    struct uchain *uchain;
    ulist_foreach (&uprobe_selflow->subs, uchain) {
        struct uprobe_selflow_sub *sub = uprobe_selflow_sub_from_uchain(uchain);
        if (sub->subpipe) {
            uprobe_selflow_set_internal_va(uprobe, "%"PRIu64",", sub->flow_id);
            return;
        }
    }

    /* no sub selected - find a flow */
    uchain = ulist_peek(&uprobe_selflow->subs);
    if (uchain == NULL) {
        uprobe_selflow->has_selection = false;
        uprobe_selflow_set_internal(uprobe, "auto");
        return;
    }

    struct uprobe_selflow_sub *sub = uprobe_selflow_sub_from_uchain(uchain);
    uprobe_selflow_set_internal_va(uprobe, "%"PRIu64",", sub->flow_id);
}

/** @internal @This catches events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return true if the event was caught and handled
 */
static bool uprobe_selflow_throw(struct uprobe *uprobe, struct upipe *upipe,
                                 enum uprobe_event event, va_list args)
{
    if (event != UPROBE_SPLIT_UPDATE)
        return false;

    struct uprobe_selflow *uprobe_selflow = uprobe_selflow_from_uprobe(uprobe);
    bool need_update = false;

    /* Iterate over existing flows. */
    struct uref *flow_def = NULL;
    while (upipe_split_iterate(upipe, &flow_def)) {
        uint64_t flow_id;
        const char *def;
        bool ret = uref_flow_get_id(flow_def, &flow_id) &&
                   uref_flow_get_def(flow_def, &def);
        assert(ret);

        if (!uprobe_selflow_check_def(uprobe, def))
            continue;

        /* Try to find a sub with that flow id. */
        struct uprobe_selflow_sub *sub = NULL;
        struct uchain *uchain;
        ulist_foreach (&uprobe_selflow->subs, uchain) {
            struct uprobe_selflow_sub *sub_chain =
                uprobe_selflow_sub_from_uchain(uchain);
            if (sub_chain->flow_id == flow_id &&
                sub_chain->split_pipe == upipe) {
                sub = sub_chain;
                break;
            }
        }

        if (sub == NULL) {
            /* Create a sub. */
            sub = malloc(sizeof(struct uprobe_selflow_sub));
            if (unlikely(sub == NULL)) {
                uprobe_throw_fatal(uprobe, upipe, UPROBE_ERR_ALLOC);
                return false;
            }

            uchain_init(&sub->uchain);
            sub->uprobe_selflow = uprobe_selflow;
            sub->split_pipe = upipe;
            sub->flow_id = flow_id;
            sub->flow_def = NULL;
            sub->subpipe = NULL;

            ulist_add(&uprobe_selflow->subs,
                      uprobe_selflow_sub_to_uchain(sub));
        } else if (sub->flow_def != NULL)
            continue;

        need_update = true;
        sub->flow_def = uref_dup(flow_def);
        if (unlikely(sub->flow_def == NULL)) {
            uprobe_throw_fatal(uprobe, upipe, UPROBE_ERR_ALLOC);
            return false;
        }

        if (!strcmp(uprobe_selflow->flows, "auto")) {
            uprobe_selflow->has_selection = true;
            uprobe_selflow_set_internal_va(uprobe,
                                           "%"PRIu64",", flow_id);
        } else if (uprobe_selflow_check(uprobe, flow_id, sub->flow_def)) {
            if (sub->subpipe == NULL) {
                sub->subpipe = upipe_flow_alloc_sub(upipe,
                    uprobe_pfx_adhoc_alloc_va(uprobe_selflow->subprobe,
                                              UPROBE_LOG_DEBUG,
                                              "flow %"PRIu64, flow_id),
                    sub->flow_def);
                if (unlikely(sub->subpipe == NULL))
                    uprobe_throw_fatal(uprobe, upipe, UPROBE_ERR_ALLOC);
            }
        } else if (sub->subpipe != NULL) {
            upipe_release(sub->subpipe);
            sub->subpipe = NULL;
        }
    }

    /* Find deleted flows. */
    struct uchain *uchain;
    ulist_delete_foreach (&uprobe_selflow->subs, uchain) {
        struct uprobe_selflow_sub *sub = uprobe_selflow_sub_from_uchain(uchain);
        bool found = false;
        flow_def = NULL;
        while (upipe_split_iterate(upipe, &flow_def)) {
            uint64_t flow_id;
            bool ret = uref_flow_get_id(flow_def, &flow_id);
            assert(ret);
            if (flow_id == sub->flow_id) {
                found = true;
                break;
            }
        }

        if (!found) {
            ulist_delete(&uprobe_selflow->subs, uchain);
            need_update = true;

            if (likely(sub->flow_def != NULL))
                uref_free(sub->flow_def);
            if (sub->subpipe != NULL)
                upipe_release(sub->subpipe);
            free(sub);
        }
    }

    if (need_update && uprobe_selflow->auto_cfg)
        uprobe_selflow_check_auto(uprobe);

    /* always return false so the event may be handled by someone else too */
    return false;
}

/** @This allocates a new uprobe_selflow structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param subprobe probe to set on flow subpipes
 * @param type type of flows to filter
 * @param flows comma-separated list of flows or attribute/value pairs
 * (lang=eng or name=ABC) to select, terminated by a comma, or "auto" to
 * automatically select the first flow, or "all"
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_selflow_alloc(struct uprobe *next,
                                    struct uprobe *subprobe,
                                    enum uprobe_selflow_type type,
                                    const char *flows)
{
    assert(flows != NULL);
    struct uprobe_selflow *uprobe_selflow =
        malloc(sizeof(struct uprobe_selflow));
    if (unlikely(uprobe_selflow == NULL))
        return NULL;
    struct uprobe *uprobe = uprobe_selflow_to_uprobe(uprobe_selflow);
    uprobe_init(uprobe, uprobe_selflow_throw, next);
    uprobe_selflow->subprobe = subprobe;
    uprobe_selflow->type = type;
    uprobe_selflow->has_selection = false;
    uprobe_selflow->flows = NULL;
    ulist_init(&uprobe_selflow->subs);
    uprobe_selflow_set(uprobe, flows);
    return uprobe;
}

/** @This frees a uprobe_selflow structure.
 *
 * @param uprobe structure to free
 * @return next probe
 */
struct uprobe *uprobe_selflow_free(struct uprobe *uprobe)
{
    struct uprobe *next = uprobe->next;
    struct uprobe_selflow *uprobe_selflow = uprobe_selflow_from_uprobe(uprobe);
    assert(ulist_empty(&uprobe_selflow->subs));
    free(uprobe_selflow->flows);
    free(uprobe_selflow);
    return next;
}

/** @This returns the flows selected by this probe.
 *
 * @param uprobe pointer to probe
 * @param flows_p filled in with a comma-separated list of flows to select,
 * terminated by a comma, or "all", or "auto" if no flow has been found yet
 */
void uprobe_selflow_get(struct uprobe *uprobe, const char **flows_p)
{
    struct uprobe_selflow *uprobe_selflow = uprobe_selflow_from_uprobe(uprobe);
    *flows_p = uprobe_selflow->flows;
}

/** @This changes the flows selected by this probe.
 *
 * @param uprobe pointer to probe
 * @param flows comma-separated list of flows or attribute/value pairs
 * (lang=eng or name=ABC) to select, terminated by a comma, or "auto" to
 * automatically select the first flow, or "all"
 */
void uprobe_selflow_set(struct uprobe *uprobe, const char *flows)
{
    struct uprobe_selflow *uprobe_selflow =
        uprobe_selflow_from_uprobe(uprobe);
    uprobe_selflow->auto_cfg = !strcmp(flows, "auto");
    if (!uprobe_selflow->auto_cfg || !uprobe_selflow->has_selection)
        uprobe_selflow_set_internal(uprobe, flows);
    else
        uprobe_selflow_check_auto(uprobe);
}

/** @This changes the flows selected by this probe, with printf-style
 * syntax.
 *
 * @param uprobe pointer to probe
 * @param format format of the syntax, followed by optional arguments
 */
void uprobe_selflow_set_va(struct uprobe *uprobe, const char *format, ...)
{
    UBASE_VARARG(uprobe_selflow_set(uprobe, string))
}
