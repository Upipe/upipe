/*
 * Copyright (C) 2013-2016 OpenHeadend S.A.R.L.
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
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include <assert.h>

/** @This is a super-set of the uprobe structure with additional local
 * members. */
struct uprobe_selflow {
    /** refcount management structure */
    struct urefcount urefcount;

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
    struct uchain subs;

    /** structure exported to modules */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_selflow, uprobe)
UBASE_FROM_TO(uprobe_selflow, urefcount, urefcount, urefcount)

/** @This defines a potential subpipe. */
struct uprobe_selflow_sub {
    /** refcount management structure */
    struct urefcount urefcount;

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
    /** probe for optional subpipe, if the flow is selected */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_selflow_sub, uprobe)
UBASE_FROM_TO(uprobe_selflow_sub, uchain, uchain, uchain)
UBASE_FROM_TO(uprobe_selflow_sub, urefcount, urefcount, urefcount)

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

    while (*flows) {
        int consumed;
        uint64_t found;
        if (sscanf(flows, "%"PRIu64",%n", &found, &consumed) >= 1 &&
            consumed > 0) {
            flows += consumed;
            if (found == flow_id)
                return true;
            continue;
        }

        char attr[strlen(flows) + 1];
        char value[strlen(flows) + 1];
        if (sscanf(flows, "%[a-zA-Z]=%[^,],%n", attr, value, &consumed) >= 2 &&
            consumed > 0) {
            flows += consumed;
            if (!strcmp(attr, "lang")) {
                uint8_t languages;
                if (ubase_check(uref_flow_get_languages(flow_def, &languages))
                        && languages) {
                    for (uint8_t j = 0; j < languages; j++) {
                        const char *lang;
                        if (ubase_check(uref_flow_get_language(flow_def, &lang,
                                                               j)) &&
                            !strcmp(lang, value))
                            return true;
                    }
                }
            } else if (!strcmp(attr, "name")) {
                const char *name;
                if (ubase_check(uref_program_flow_get_name(flow_def, &name)) &&
                    !strcmp(name, value))
                     return true;
            }
            continue;
        }

        uprobe_warn_va(uprobe, NULL, "malformed flow (%s)", flows);
        break;
    }
    return false;
}

/** @internal @This sets the flows to select.
 *
 * @param uprobe pointer to probe
 * @param flows comma-separated list of flows to select, or "auto" to
 * automatically select the first flow, or "all"
 * @return an error code
 */
static int uprobe_selflow_set_internal(struct uprobe *uprobe, const char *flows)
{
    struct uprobe_selflow *uprobe_selflow = uprobe_selflow_from_uprobe(uprobe);
    free(uprobe_selflow->flows);
    size_t flows_len = strlen(flows);
    if (!flows_len) {
        uprobe_warn(uprobe, NULL, "invalid flows");
        uprobe_selflow->flows = strdup("auto");
        if (unlikely(uprobe_selflow->flows == NULL))
            return UBASE_ERR_ALLOC;
    } else if (!strcmp(flows, "all") || !strcmp(flows, "auto")) {
        uprobe_selflow->flows = strdup(flows);
        if (unlikely(uprobe_selflow->flows == NULL))
            return UBASE_ERR_ALLOC;
    } else {
        uprobe_selflow->flows = malloc(flows_len + sizeof(","));
        if (unlikely(uprobe_selflow->flows == NULL))
            return UBASE_ERR_ALLOC;
        memcpy(uprobe_selflow->flows, flows, flows_len);
        if (uprobe_selflow->flows[flows_len - 1] != ',') {
            uprobe_selflow->flows[flows_len] = ',';
            uprobe_selflow->flows[flows_len + 1] = '\0';
        } else
            uprobe_selflow->flows[flows_len] = '\0';
    }

    int error = UBASE_ERR_NONE;
    struct uchain *uchain;
    ulist_foreach (&uprobe_selflow->subs, uchain) {
        struct uprobe_selflow_sub *sub = uprobe_selflow_sub_from_uchain(uchain);
        if (uprobe_selflow_check(uprobe, sub->flow_id, sub->flow_def)) {
            if (sub->subpipe == NULL) {
                sub->subpipe = upipe_flow_alloc_sub(sub->split_pipe,
                    uprobe_pfx_alloc_va(uprobe_use(&sub->uprobe),
                                        UPROBE_LOG_VERBOSE,
                                        "flow %"PRIu64, sub->flow_id),
                    sub->flow_def);
                if (unlikely(sub->subpipe == NULL))
                    error = UBASE_ERR_ALLOC;
            }
        } else if (sub->subpipe != NULL) {
            struct upipe *subpipe = sub->subpipe;
            sub->subpipe = NULL;
            upipe_release(subpipe);
        }
    }
    return error;
}

/** @internal @This sets the flows to select, with printf-style syntax.
 *
 * @param uprobe pointer to probe
 * @param format of the syntax, followed by optional arguments
 */
static int uprobe_selflow_set_internal_va(struct uprobe *uprobe,
                                          const char *format, ...)
{
    UBASE_VARARG(uprobe_selflow_set_internal(uprobe, string))
}

/** @internal @This checks that there is at least one selected flow, or
 * otherwise selects a new flow.
 *
 * @param uprobe pointer to probe
 */
static int uprobe_selflow_check_auto(struct uprobe *uprobe)
{
    struct uprobe_selflow *uprobe_selflow = uprobe_selflow_from_uprobe(uprobe);
    struct uchain *uchain;
    ulist_foreach (&uprobe_selflow->subs, uchain) {
        struct uprobe_selflow_sub *sub = uprobe_selflow_sub_from_uchain(uchain);
        if (sub->subpipe)
            return uprobe_selflow_set_internal_va(uprobe, "%"PRIu64",",
                                                  sub->flow_id);
    }

    /* no sub selected - find a flow */
    uchain = ulist_peek(&uprobe_selflow->subs);
    if (uchain == NULL) {
        uprobe_selflow->has_selection = false;
        return uprobe_selflow_set_internal(uprobe, "auto");
    }

    struct uprobe_selflow_sub *sub = uprobe_selflow_sub_from_uchain(uchain);
    return uprobe_selflow_set_internal_va(uprobe, "%"PRIu64",", sub->flow_id);
}

/** @internal @This catches events thrown by subpipes.
 *
 * @param uprobe pointer to probe
 * @param subpipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return an error code
 */
static int uprobe_selflow_sub_throw(struct uprobe *uprobe,
                                    struct upipe *subpipe,
                                    int event, va_list args)
{
    if (event != UPROBE_SOURCE_END)
        return uprobe_throw_next(uprobe, subpipe, event, args);

    uprobe_throw_next(uprobe, subpipe, event, args);

    struct uprobe_selflow_sub *sub =
        uprobe_selflow_sub_from_uprobe(uprobe);
    assert(subpipe == sub->subpipe);
    ulist_delete(uprobe_selflow_sub_to_uchain(sub));
    uref_free(sub->flow_def);
    sub->subpipe = NULL;
    upipe_release(subpipe);
    uprobe_release(uprobe_selflow_sub_to_uprobe(sub));
    return UBASE_ERR_NONE;
}

/** @internal @This frees a uprobe_selflow_sub structure.
 *
 * @param urefcount pointer to urefcount structure
 */
static void uprobe_selflow_sub_free(struct urefcount *urefcount)
{
    struct uprobe_selflow_sub *sub =
        uprobe_selflow_sub_from_urefcount(urefcount);
    uprobe_clean(uprobe_selflow_sub_to_uprobe(sub));
    free(sub);
}

/** @internal @This allocates a subprobe for a flow.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param uprobe_selflow pointer to super-probe
 * @param split_pipe pointer to split pipe
 * @param flow id flow ID
 * @return pointer to uprobe, or NULL in case of error
 */
static struct uprobe *uprobe_selflow_sub_alloc(struct uprobe *next,
        struct uprobe_selflow *uprobe_selflow, struct upipe *split_pipe,
        uint64_t flow_id)
{
    struct uprobe_selflow_sub *sub = malloc(sizeof(struct uprobe_selflow_sub));
    if (unlikely(sub == NULL)) {
        uprobe_release(next);
        return NULL;
    }

    struct uprobe *uprobe = uprobe_selflow_sub_to_uprobe(sub);
    uprobe_init(uprobe, uprobe_selflow_sub_throw, next);

    uchain_init(&sub->uchain);
    sub->uprobe_selflow = uprobe_selflow;
    sub->split_pipe = split_pipe;
    sub->flow_id = flow_id;
    sub->flow_def = NULL;
    sub->subpipe = NULL;
    urefcount_init(uprobe_selflow_sub_to_urefcount(sub),
                   uprobe_selflow_sub_free);
    uprobe->refcount = uprobe_selflow_sub_to_urefcount(sub);
    return uprobe;
}

/** @internal @This catches events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return an error code
 */
static int uprobe_selflow_throw(struct uprobe *uprobe, struct upipe *upipe,
                                int event, va_list args)
{
    if (event != UPROBE_SPLIT_UPDATE)
        return uprobe_throw_next(uprobe, upipe, event, args);

    struct uprobe_selflow *uprobe_selflow = uprobe_selflow_from_uprobe(uprobe);
    bool need_update = false;

    int error = UBASE_ERR_NONE;
    /* Iterate over existing flows. */
    struct uref *flow_def = NULL;
    while (ubase_check(upipe_split_iterate(upipe, &flow_def)) &&
           flow_def != NULL) {
        uint64_t flow_id;
        const char *def;
        UBASE_RETURN(uref_flow_get_id(flow_def, &flow_id))
        UBASE_RETURN(uref_flow_get_def(flow_def, &def))

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
            struct uprobe *uprobe_selflow_sub =
                uprobe_selflow_sub_alloc(uprobe_use(uprobe_selflow->subprobe),
                                         uprobe_selflow, upipe, flow_id);
            if (unlikely(uprobe_selflow_sub == NULL))
                return UBASE_ERR_ALLOC;

            sub = uprobe_selflow_sub_from_uprobe(uprobe_selflow_sub);
            ulist_add(&uprobe_selflow->subs,
                      uprobe_selflow_sub_to_uchain(sub));
        } else if (sub->flow_def != NULL)
            continue;

        need_update = true;
        sub->flow_def = uref_dup(flow_def);
        if (unlikely(sub->flow_def == NULL))
            return UBASE_ERR_ALLOC;

        if (!strcmp(uprobe_selflow->flows, "auto")) {
            uprobe_selflow->has_selection = true;
            uprobe_selflow_set_internal_va(uprobe,
                                           "%"PRIu64",", flow_id);
        } else if (uprobe_selflow_check(uprobe, flow_id, sub->flow_def)) {
            if (sub->subpipe == NULL) {
                sub->subpipe = upipe_flow_alloc_sub(upipe,
                    uprobe_pfx_alloc_va(uprobe_use(&sub->uprobe),
                                        UPROBE_LOG_VERBOSE,
                                        "flow %"PRIu64, flow_id),
                    sub->flow_def);
                if (unlikely(sub->subpipe == NULL))
                    error = UBASE_ERR_ALLOC;
            }
        } else if (sub->subpipe != NULL) {
            struct upipe *subpipe = sub->subpipe;
            sub->subpipe = NULL;
            upipe_release(subpipe);
        }
    }

    /* Find deleted flows. */
    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach (&uprobe_selflow->subs, uchain, uchain_tmp) {
        struct uprobe_selflow_sub *sub = uprobe_selflow_sub_from_uchain(uchain);
        bool found = false;
        flow_def = NULL;
        while (ubase_check(upipe_split_iterate(upipe, &flow_def)) &&
               flow_def != NULL) {
            uint64_t flow_id;
            UBASE_RETURN(uref_flow_get_id(flow_def, &flow_id))
            if (flow_id == sub->flow_id) {
                found = true;
                break;
            }
        }

        if (!found) {
            ulist_delete(uchain);
            need_update = true;

            uref_free(sub->flow_def);
            struct upipe *subpipe = sub->subpipe;
            sub->subpipe = NULL;
            upipe_release(subpipe);
            uprobe_release(uprobe_selflow_sub_to_uprobe(sub));
        }
    }

    if (need_update && uprobe_selflow->auto_cfg)
        uprobe_selflow_check_auto(uprobe);
    if (ubase_check(error))
        return uprobe_throw_next(uprobe, upipe, event, args);
    return error;
}

/** @internal @This frees a uprobe_selflow structure.
 *
 * @param urefcount pointer to urefcount structure
 */
static void uprobe_selflow_free(struct urefcount *urefcount)
{
    struct uprobe_selflow *uprobe_selflow =
        uprobe_selflow_from_urefcount(urefcount);
    assert(ulist_empty(&uprobe_selflow->subs));
    free(uprobe_selflow->flows);
    uprobe_release(uprobe_selflow->subprobe);
    uprobe_clean(uprobe_selflow_to_uprobe(uprobe_selflow));
    free(uprobe_selflow);
}

/** @This allocates a new uprobe_selflow structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param subprobe probe to set on flow subpipes
 * @param type type of flows to filter
 * @param flows comma-separated list of flows or attribute/value pairs
 * (lang=eng or name=ABC) to select, or "auto" to automatically select the
 * first flow, or "all"
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
    urefcount_init(uprobe_selflow_to_urefcount(uprobe_selflow),
                   uprobe_selflow_free);
    uprobe->refcount = uprobe_selflow_to_urefcount(uprobe_selflow);
    return uprobe;
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
 * (lang=eng or name=ABC) to select, or "auto" to automatically select the
 * first flow, or "all"
 * @return an error code
 */
int uprobe_selflow_set(struct uprobe *uprobe, const char *flows)
{
    assert(flows != NULL);
    struct uprobe_selflow *uprobe_selflow = uprobe_selflow_from_uprobe(uprobe);
    uprobe_selflow->auto_cfg = !strcmp(flows, "auto");
    if (!uprobe_selflow->auto_cfg || !uprobe_selflow->has_selection)
        return uprobe_selflow_set_internal(uprobe, flows);
    else
        return uprobe_selflow_check_auto(uprobe);
}

/** @This allocates a new uprobe_selflow structure, with printf-style syntax.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param subprobe probe to set on flow subpipes
 * @param type type of flows to filter
 * @param format printf-style format for the flows, followed by optional
 * arguments
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_selflow_alloc_va(struct uprobe *next,
                                       struct uprobe *subprobe,
                                       enum uprobe_selflow_type type,
                                       const char *format, ...)
{
    UBASE_VARARG(uprobe_selflow_alloc(next, subprobe, type, string))
}

/** @This changes the flows selected by this probe, with printf-style
 * syntax.
 *
 * @param uprobe pointer to probe
 * @param format format of the syntax, followed by optional arguments
 */
int uprobe_selflow_set_va(struct uprobe *uprobe, const char *format, ...)
{
    UBASE_VARARG(uprobe_selflow_set(uprobe, string))
}
