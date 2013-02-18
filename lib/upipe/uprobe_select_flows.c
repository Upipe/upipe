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
 * @short probe catching add_flow events and forwarding some flows
 *
 * The probe catches the add_flow events and only exports
 * (ie. forwards upstream) the flows that are selected.
 *
 * In case of a change of configuration, or if flows are added or deleted,
 * the selections are reconsidered and appropriate del_flows/add_flows are
 * emitted.
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_helper_uprobe.h>
#include <upipe/uprobe_select_flows.h>
#include <upipe/upipe.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include <assert.h>

#define STRINGIFY(x) #x
#define UINT64_MAX_STR STRINGIFY(UINT64_MAX)

/** @This defines a potential output. */
struct uprobe_selflow_output {
    /** structure for double-linked lists */
    struct uchain uchain;

    /** pointer to split pipe that emitted the flow id, just for comparisons */
    struct upipe *split_pipe;
    /** flow id declared by the split pipe */
    uint64_t flow_id;

    /** true if the output is currently selected */
    bool selected;

    /** flow definition */
    struct uref *flow_def;
};

/** @This returns the high-level uprobe_selflow_output structure.
 *
 * @param uchain pointer to the uchain structure wrapped into the
 * uprobe_selflow_output
 * @return pointer to the uprobe_selflow_output structure
 */
static inline struct uprobe_selflow_output *
    uprobe_selflow_output_from_uchain(struct uchain *uchain)
{
    return container_of(uchain, struct uprobe_selflow_output, uchain);
}

/** @This returns the uchain structure used for FIFO, LIFO and lists.
 *
 * @param uprobe_selflow_output uprobe_selflow_output structure
 * @return pointer to the uchain structure
 */
static inline struct uchain *
    uprobe_selflow_output_to_uchain(struct uprobe_selflow_output *s)
{
    return &s->uchain;
}

/** @This is a super-set of the uprobe structure with additional local
 * members. */
struct uprobe_selflow {
    /** type of flows to filter */
    enum uprobe_selflow_type type;
    /** user configuration */
    char *flows;
    /** true if the user specified auto, regardless of what happened after */
    bool auto_cfg;
    /** true if at least one output is selected */
    bool has_selection;
    /** list of outputs */
    struct ulist outputs;
    /** list of all flows */
    char *all_flows;

    /** structure exported to modules */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_selflow, uprobe)

/** @internal @This checks if a given flow is within a given list.
 *
 * @param flows comma-separated list of flows
 * @param flow_id flow to check
 * @return true if the flow was found
 */
static bool uprobe_selflow_lookup(const char *flows, uint64_t flow_id)
{
    if (flows == NULL)
        return false;

    uint64_t found;
    int consumed;
    while (sscanf(flows, "%"PRIu64",%n", &found, &consumed) >= 1) {
        if (found == flow_id)
            return true;
        flows += consumed;
    }
    return false;
}

/** @internal @This compiles the list of all flows.
 *
 * @param uprobe pointer to probe
 */
static void uprobe_selflow_update_list(struct uprobe *uprobe)
{
    struct uprobe_selflow *uprobe_selflow = uprobe_selflow_from_uprobe(uprobe);
    free(uprobe_selflow->all_flows);
    char *all_flows = uprobe_selflow->all_flows = strdup("");
    size_t size = 1;

    struct uchain *uchain;
    ulist_foreach (&uprobe_selflow->outputs, uchain) {
        struct uprobe_selflow_output *output =
            uprobe_selflow_output_from_uchain(uchain);
        if (!uprobe_selflow_lookup(all_flows, output->flow_id)) {
            char flow[strlen(UINT64_MAX_STR) + 2];
            size += sprintf(flow, "%"PRIu64",", output->flow_id);
            all_flows = realloc(all_flows, size);
            if (unlikely(all_flows == NULL)) {
                uprobe_throw_aerror(uprobe, NULL);
                return;
            }
            strcat(all_flows, flow);
            uprobe_selflow->all_flows = all_flows;
        }
    }
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

/** @internal @This returns whether the given flow is selected by the
 * user.
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

    /* comma-separated list of outputs */
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
        }
        else if (!strcmp(attr, "lang")) {
            const char *lang;
            if (uref_flow_get_lang(flow_def, &lang) && !strcmp(lang, value))
                return true;
        }
    }
    return false;
}

/** @internal @This sets the flows to output.
 *
 * @param uprobe pointer to probe
 * @param flows comma-separated list of flows to select, terminated by a
 * comma, or "auto" to automatically select the first flow carrying
 * elementary streams, or "all"
 */
static void uprobe_selflow_set_internal(struct uprobe *uprobe,
                                        const char *flows)
{
    struct uprobe_selflow *uprobe_selflow = uprobe_selflow_from_uprobe(uprobe);
    free(uprobe_selflow->flows);
    uprobe_selflow->flows = strdup(flows);
    if (unlikely(uprobe_selflow->flows == NULL)) {
        uprobe_throw_aerror(uprobe, NULL);
        return;
    }

    struct uchain *uchain;
    ulist_foreach (&uprobe_selflow->outputs, uchain) {
        struct uprobe_selflow_output *output =
            uprobe_selflow_output_from_uchain(uchain);
        bool was_selected = output->selected;
        output->selected = uprobe_selflow_check(uprobe, output->flow_id,
                                                output->flow_def);

        if (was_selected && !output->selected)
            uprobe_throw(uprobe->next, output->split_pipe,
                         UPROBE_SPLIT_DEL_FLOW, output->flow_id);
        else if (!was_selected && output->selected)
            uprobe_throw(uprobe->next, output->split_pipe,
                         UPROBE_SPLIT_ADD_FLOW, output->flow_id,
                         output->flow_def);
    }
}

/** @internal @This sets the flows to output, with printf-style syntax.
 *
 * @param uprobe pointer to probe
 * @param format of the syntax, followed by optional arguments
 */
static void uprobe_selflow_set_internal_va(struct uprobe *uprobe,
                                           const char *format, ...)
{
    UBASE_VARARG(uprobe_selflow_set_internal(uprobe, string))
}

/** @internal @This checks that there is at least one selected output, or
 * otherwise selects a new flow.
 *
 * @param uprobe pointer to probe
 */
static void uprobe_selflow_check_auto(struct uprobe *uprobe)
{
    struct uprobe_selflow *uprobe_selflow = uprobe_selflow_from_uprobe(uprobe);
    struct uchain *uchain;
    ulist_foreach (&uprobe_selflow->outputs, uchain) {
        struct uprobe_selflow_output *output =
            uprobe_selflow_output_from_uchain(uchain);
        if (output->selected)
            return;
    }

    /* no output selected - find an active flow */
    uchain = ulist_peek(&uprobe_selflow->outputs);
    if (uchain != NULL) {
        struct uprobe_selflow_output *output =
            uprobe_selflow_output_from_uchain(uchain);
        uprobe_selflow_set_internal_va(uprobe, "%"PRIu64",", output->flow_id);
    } else {
        uprobe_selflow->has_selection = false;
        uprobe_selflow_set_internal(uprobe, "auto");
    }
}

/** @internal @This returns the selflow_output corresponding to the given
 * flow id of it exists, or allocates it.
 *
 * @param uprobe pointer to probe
 * @param split_pipe pipe sending the event
 * @param flow_id flow id
 * @param create allocate an output if it is true
 * @return pointer to selflow_output
 */
static struct uprobe_selflow_output *
    uprobe_selflow_output_by_id(struct uprobe *uprobe,
                                struct upipe *split_pipe, uint64_t flow_id,
                                bool create)
{
    struct uprobe_selflow *uprobe_selflow = uprobe_selflow_from_uprobe(uprobe);
    struct uchain *uchain;
    ulist_foreach (&uprobe_selflow->outputs, uchain) {
        struct uprobe_selflow_output *output =
            uprobe_selflow_output_from_uchain(uchain);
        if (output->split_pipe == split_pipe && output->flow_id == flow_id)
            return output;
    }

    if (!create)
        return NULL;

    struct uprobe_selflow_output *output =
        malloc(sizeof(struct uprobe_selflow_output));
    if (unlikely(output == NULL))
        return NULL;

    uchain_init(&output->uchain);
    output->split_pipe = split_pipe;
    output->flow_id = flow_id;

    output->selected = false;
    output->flow_def = NULL;

    ulist_add(&uprobe_selflow->outputs,
              uprobe_selflow_output_to_uchain(output));
    return output;
}

/** @internal @This catches add_flow events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return true if the event was caught and handled
 */
static bool uprobe_selflow_add_flow(struct uprobe *uprobe, struct upipe *upipe,
                                    enum uprobe_event event, va_list args)
{
    struct uprobe_selflow *uprobe_selflow = uprobe_selflow_from_uprobe(uprobe);
    uint64_t flow_id = va_arg(args, uint64_t);
    struct uref *uref = va_arg(args, struct uref *);
    const char *def;
    if (unlikely(uref == NULL || !uref_flow_get_def(uref, &def) ||
                 !uprobe_selflow_check_def(uprobe, def)))
        return false;

    struct uprobe_selflow_output *output =
        uprobe_selflow_output_by_id(uprobe, upipe, flow_id, true);
    if (unlikely(output == NULL)) {
        uprobe_throw_aerror(uprobe, upipe);
        return false;
    }

    if (output->flow_def != NULL)
        uref_free(output->flow_def);
    output->flow_def = uref_dup(uref);
    if (unlikely(output->flow_def == NULL)) {
        uprobe_throw_aerror(uprobe, upipe);
        return false;
    }

    bool was_selected = output->selected;
    uprobe_selflow_update_list(uprobe);
    output->selected = uprobe_selflow_check(uprobe, flow_id, output->flow_def);

    if (was_selected && !output->selected)
        uprobe_throw(uprobe->next, output->split_pipe,
                     UPROBE_SPLIT_DEL_FLOW, output->flow_id);

    if (!strcmp(uprobe_selflow->flows, "auto")) {
        uprobe_selflow->has_selection = true;
        uprobe_selflow_set_internal_va(uprobe,
                                       "%"PRIu64",", flow_id);
    }
    return !output->selected;
}

/** @internal @This catches del_flow events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return true if the event was caught and handled
 */
static bool uprobe_selflow_del_flow(struct uprobe *uprobe, struct upipe *upipe,
                                    enum uprobe_event event, va_list args)
{
    struct uprobe_selflow *uprobe_selflow = uprobe_selflow_from_uprobe(uprobe);
    uint64_t flow_id = va_arg(args, uint64_t);
    struct uprobe_selflow_output *output =
        uprobe_selflow_output_by_id(uprobe, upipe, flow_id, false);
    if (output == NULL)
        return false;

    struct uchain *uchain;
    ulist_delete_foreach(&uprobe_selflow->outputs, uchain) {
        if (uprobe_selflow_output_from_uchain(uchain) == output) {
            ulist_delete(&uprobe_selflow->outputs, uchain);
        }
    }
    uprobe_selflow_update_list(uprobe);

    bool was_selected = output->selected;
    if (likely(output->flow_def != NULL))
        uref_free(output->flow_def);
    free(output);

    if (uprobe_selflow->auto_cfg)
        uprobe_selflow_check_auto(uprobe);
    return !was_selected;
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
    switch (event) {
        case UPROBE_SPLIT_ADD_FLOW:
            return uprobe_selflow_add_flow(uprobe, upipe, event, args);
        case UPROBE_SPLIT_DEL_FLOW:
            return uprobe_selflow_del_flow(uprobe, upipe, event, args);
        default:
            return false;
    }
}

/** @This allocates a new uprobe_selflow structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param type type of flows to filter
 * @param flows comma-separated list of flows or attribute/value pairs
 * (lang=eng) to select, terminated by a comma, or "auto" to automatically
 * select the first flow, or "all"
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_selflow_alloc(struct uprobe *next,
                                    enum uprobe_selflow_type type,
                                    const char *flows)
{
    assert(flows != NULL);
    struct uprobe_selflow *uprobe_selflow =
        malloc(sizeof(struct uprobe_selflow));
    if (unlikely(uprobe_selflow == NULL))
        return NULL;
    struct uprobe *uprobe = uprobe_selflow_to_uprobe(uprobe_selflow);
    uprobe_selflow->type = type;
    uprobe_selflow->has_selection = false;
    uprobe_selflow->all_flows = strdup("");
    uprobe_selflow->flows = NULL;
    ulist_init(&uprobe_selflow->outputs);
    uprobe_selflow_set(uprobe, flows);
    uprobe_init(uprobe, uprobe_selflow_throw, next);
    return uprobe;
}

/** @This frees a uprobe_selflow structure.
 *
 * @param uprobe structure to free
 */
void uprobe_selflow_free(struct uprobe *uprobe)
{
    struct uprobe_selflow *uprobe_selflow =
        uprobe_selflow_from_uprobe(uprobe);
    assert(ulist_empty(&uprobe_selflow->outputs));
    free(uprobe_selflow->flows);
    free(uprobe_selflow->all_flows);
    free(uprobe_selflow);
}

/** @This returns the flows selected by this probe.
 *
 * @param uprobe pointer to probe
 * @param flows_p filled in with a comma-separated list of flows or
 * attribute/value pairs (lang=eng) to select, terminated by a comma, or "all",
 * or "auto" if no flow has been found yet
 */
void uprobe_selflow_get(struct uprobe *uprobe, const char **flows_p)
{
    struct uprobe_selflow *uprobe_selflow =
        uprobe_selflow_from_uprobe(uprobe);
    *flows_p = uprobe_selflow->flows;
}

/** @This returns a list of all the flows available.
 *
 * @param uprobe pointer to probe
 * @param flows_p filled in with a comma-separated list of all flows,
 * terminated by a comma
 */
void uprobe_selflow_list(struct uprobe *uprobe, const char **flows_p)
{
    struct uprobe_selflow *uprobe_selflow =
        uprobe_selflow_from_uprobe(uprobe);
    *flows_p = uprobe_selflow->all_flows;
}

/** @This changes the flows selected by this probe.
 *
 * @param uprobe pointer to probe
 * @param flows comma-separated list of flows or attribute/value pairs
 * (lang=eng) to select, terminated by a comma, or "auto" to automatically
 * select the first flow, or "all"
 */
void uprobe_selflow_set(struct uprobe *uprobe, const char *flows)
{
    struct uprobe_selflow *uprobe_selflow =
        uprobe_selflow_from_uprobe(uprobe);
    uprobe_selflow->auto_cfg = !strcmp(flows, "auto");
    if (!uprobe_selflow->auto_cfg || !uprobe_selflow->has_selection)
        uprobe_selflow_set_internal(uprobe, flows);
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
