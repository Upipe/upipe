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
 * @short probe catching add_flow events and forwarding flows of some outputs
 *
 * The probe catches the add_flow events for optional program flows (of flow
 * definition "program."), meaning that it is necessary to decode the program
 * description, and decides whether to allocate such a demux void subpipe.
 *
 * It also catches add_flow events for elementary streams and only exports
 * (ie. forwards upstream) those that are in the selected outputs.
 *
 * In case of a change of configuration, or if outputs are added or deleted,
 * the selections are reconsidered and appropriate del_flows/add_flows are
 * emitted.
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_program_flow.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_helper_uprobe.h>
#include <upipe/uprobe_select_programs.h>
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

/** @This defines a potential output. */
struct uprobe_selprog_output {
    /** structure for double-linked lists */
    struct uchain uchain;

    /** pointer to split pipe that emitted the flow id, just for comparisons */
    struct upipe *split_pipe;
    /** flow id declared by the split pipe */
    uint64_t flow_id;

    /** program number */
    uint64_t program_number;
    /** true if the output is currently selected */
    bool selected;

    /** flow definition */
    struct uref *flow_def;
    /** true if the flow is a program definition that ought not to be
     * forwarded upstream */
    bool program_def;
    /** pointer to optional demux void subpipe to decode program definition,
     * if program_def and selected are true */
    struct upipe *void_pipe;
};

/** @This returns the high-level uprobe_selprog_output structure.
 *
 * @param uchain pointer to the uchain structure wrapped into the
 * uprobe_selprog_output
 * @return pointer to the uprobe_selprog_output structure
 */
static inline struct uprobe_selprog_output *
    uprobe_selprog_output_from_uchain(struct uchain *uchain)
{
    return container_of(uchain, struct uprobe_selprog_output, uchain);
}

/** @This returns the uchain structure used for FIFO, LIFO and lists.
 *
 * @param uprobe_selprog_output uprobe_selprog_output structure
 * @return pointer to the uchain structure
 */
static inline struct uchain *
    uprobe_selprog_output_to_uchain(struct uprobe_selprog_output *s)
{
    return &s->uchain;
}

/** @This is a super-set of the uprobe structure with additional local
 * members. */
struct uprobe_selprog {
    /** user configuration */
    char *programs;
    /** true if the user specified auto, regardless of what happened after */
    bool auto_cfg;
    /** true if at least one output is selected */
    bool has_selection;
    /** list of outputs */
    struct ulist outputs;
    /** list of all programs */
    char *all_programs;

    /** structure exported to modules */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_selprog, uprobe)

/** @internal @This checks if a given program number is within a given list.
 *
 * @param programs comma-separated list of programs
 * @param program_number program number to check
 * @return true if the program number was found
 */
static bool uprobe_selprog_lookup(const char *programs, uint64_t program_number)
{
    if (programs == NULL)
        return false;

    uint64_t found;
    int consumed;
    while (sscanf(programs, "%"PRIu64",%n", &found, &consumed) >= 1) {
        if (found == program_number)
            return true;
        programs += consumed;
    }
    return false;
}

/** @internal @This compiles the list of all programs.
 *
 * @param uprobe pointer to probe
 */
static void uprobe_selprog_update_list(struct uprobe *uprobe)
{
    struct uprobe_selprog *uprobe_selprog = uprobe_selprog_from_uprobe(uprobe);
    free(uprobe_selprog->all_programs);
    char *all_programs = uprobe_selprog->all_programs = strdup("");
    size_t size = 1;

    struct uchain *uchain;
    ulist_foreach (&uprobe_selprog->outputs, uchain) {
        struct uprobe_selprog_output *output =
            uprobe_selprog_output_from_uchain(uchain);
        if (!uprobe_selprog_lookup(all_programs, output->program_number)) {
            char program[strlen(UINT64_MAX_STR) + 2];
            size += sprintf(program, "%"PRIu64",", output->program_number);
            all_programs = realloc(all_programs, size);
            if (unlikely(all_programs == NULL)) {
                uprobe_throw_fatal(uprobe, NULL, UPROBE_ERR_ALLOC);
                return;
            }
            strcat(all_programs, program);
            uprobe_selprog->all_programs = all_programs;
        }
    }
}

/** @internal @This returns whether the given program number is selected by the
 * user.
 *
 * @param uprobe pointer to probe
 * @param program_number program number
 * @return true if the program number is selected
 */
static bool uprobe_selprog_check(struct uprobe *uprobe, uint64_t program_number)
{
    struct uprobe_selprog *uprobe_selprog = uprobe_selprog_from_uprobe(uprobe);
    if (!strcmp(uprobe_selprog->programs, "all") ||
        !strcmp(uprobe_selprog->programs, "auto"))
        return true;

    /* try to find the flow definition for the program void output */
    struct uref *flow_def = NULL;
    struct uchain *uchain;
    ulist_foreach (&uprobe_selprog->outputs, uchain) {
        struct uprobe_selprog_output *output =
            uprobe_selprog_output_from_uchain(uchain);
        if (output->program_number == program_number && output->program_def) {
            flow_def = output->flow_def;
            break;
        }
    }

    /* comma-separated list of outputs */
    const char *programs = uprobe_selprog->programs;
    uint64_t found;
    char attr[strlen(programs) + 1];
    char value[strlen(programs) + 1];
    int consumed;
    int tmp;
    while ((tmp = sscanf(programs, "%"PRIu64",%n", &found, &consumed)) >= 1 ||
           sscanf(programs, "%[a-zA-Z]=%[^,],%n", attr, value,
                  &consumed) >= 2) {
        programs += consumed;
        if (tmp > 0) {
            if (found == program_number)
                return true;
        }
        else if (!strcmp(attr, "name")) {
            const char *name;
            if (flow_def != NULL &&
                uref_program_flow_get_name(flow_def, &name) &&
                !strcmp(name, value))
                return true;
        }
    }
    return false;
}

/** @internal @This sets the programs to output.
 *
 * @param uprobe pointer to probe
 * @param programs comma-separated list of programs to select, terminated by a
 * comma, or "auto" to automatically select the first program carrying
 * elementary streams, or "all"
 */
static void uprobe_selprog_set_internal(struct uprobe *uprobe,
                                        const char *programs)
{
    struct uprobe_selprog *uprobe_selprog = uprobe_selprog_from_uprobe(uprobe);
    free(uprobe_selprog->programs);
    uprobe_selprog->programs = strdup(programs);
    if (unlikely(uprobe_selprog->programs == NULL)) {
        uprobe_throw_fatal(uprobe, NULL, UPROBE_ERR_ALLOC);
        return;
    }

    struct uchain *uchain;
uprobe_selprog_set_internal_retry:
    ulist_foreach (&uprobe_selprog->outputs, uchain) {
        struct uprobe_selprog_output *output =
            uprobe_selprog_output_from_uchain(uchain);
        bool was_selected = output->selected;
        output->selected = uprobe_selprog_check(uprobe, output->program_number);

        if (!output->program_def) {
            if (was_selected && !output->selected)
                uprobe_throw(uprobe->next, output->split_pipe,
                             UPROBE_SPLIT_DEL_FLOW, output->flow_id);
            else if (!was_selected && output->selected)
                uprobe_throw(uprobe->next, output->split_pipe,
                             UPROBE_SPLIT_ADD_FLOW, output->flow_id,
                             output->flow_def);

        } else {
            if (was_selected && !output->selected) {
                struct upipe *void_pipe = output->void_pipe;
                output->void_pipe = NULL;
                upipe_release(void_pipe);
                goto uprobe_selprog_set_internal_retry;
            } else if (!was_selected && output->selected) {
                output->void_pipe = upipe_flow_alloc_sub(output->split_pipe,
                    uprobe_pfx_adhoc_alloc_va(uprobe, UPROBE_LOG_DEBUG,
                                              "program %"PRIu64,
                                              output->program_number),
                    output->flow_def);
                if (unlikely(output->void_pipe == NULL))
                    uprobe_throw_fatal(uprobe, NULL, UPROBE_ERR_ALLOC);
            }
        }
    }
}

/** @internal @This sets the programs to output, with printf-style syntax.
 *
 * @param uprobe pointer to probe
 * @param format of the syntax, followed by optional arguments
 */
static void uprobe_selprog_set_internal_va(struct uprobe *uprobe,
                                           const char *format, ...)
{
    UBASE_VARARG(uprobe_selprog_set_internal(uprobe, string))
}

/** @internal @This checks that there is at least one selected output, or
 * otherwise selects a new program.
 *
 * @param uprobe pointer to probe
 */
static void uprobe_selprog_check_auto(struct uprobe *uprobe)
{
    struct uprobe_selprog *uprobe_selprog = uprobe_selprog_from_uprobe(uprobe);
    struct uchain *uchain;
    ulist_foreach (&uprobe_selprog->outputs, uchain) {
        struct uprobe_selprog_output *output =
            uprobe_selprog_output_from_uchain(uchain);
        if (output->program_def && output->selected)
            return;
    }

    /* no output selected - find an active program */
    uint64_t program_number = UINT64_MAX;
    ulist_foreach (&uprobe_selprog->outputs, uchain) {
        struct uprobe_selprog_output *output =
            uprobe_selprog_output_from_uchain(uchain);
        if (output->program_def) {
            program_number = output->program_number;
            break;
        }
    }
    if (program_number == UINT64_MAX) {
        uprobe_selprog->has_selection = false;
        uprobe_selprog_set_internal(uprobe, "auto");
    } else
        uprobe_selprog_set_internal_va(uprobe, "%"PRIu64",", program_number);
}

/** @internal @This returns the selprog_output corresponding to the given
 * flow id of it exists, or allocates it.
 *
 * @param uprobe pointer to probe
 * @param split_pipe pipe sending the event
 * @param flow_id flow id
 * @return pointer to selprog_output
 */
static struct uprobe_selprog_output *
    uprobe_selprog_output_by_id(struct uprobe *uprobe,
                                struct upipe *split_pipe, uint64_t flow_id)
{
    struct uprobe_selprog *uprobe_selprog = uprobe_selprog_from_uprobe(uprobe);
    struct uchain *uchain;
    ulist_foreach (&uprobe_selprog->outputs, uchain) {
        struct uprobe_selprog_output *output =
            uprobe_selprog_output_from_uchain(uchain);
        if (output->split_pipe == split_pipe && output->flow_id == flow_id)
            return output;
    }

    struct uprobe_selprog_output *output =
        malloc(sizeof(struct uprobe_selprog_output));
    if (unlikely(output == NULL))
        return NULL;

    uchain_init(&output->uchain);
    output->split_pipe = split_pipe;
    output->flow_id = flow_id;

    output->program_number = UINT64_MAX;
    output->selected = false;
    output->flow_def = NULL;
    output->program_def = false;
    output->void_pipe = NULL;

    ulist_add(&uprobe_selprog->outputs,
              uprobe_selprog_output_to_uchain(output));
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
static bool uprobe_selprog_add_flow(struct uprobe *uprobe, struct upipe *upipe,
                                    enum uprobe_event event, va_list args)
{
    struct uprobe_selprog *uprobe_selprog = uprobe_selprog_from_uprobe(uprobe);
    uint64_t flow_id = va_arg(args, uint64_t);
    struct uref *uref = va_arg(args, struct uref *);
    const char *def;
    const char *programs;
    uint64_t program_number;
    if (unlikely(uref == NULL || !uref_flow_get_def(uref, &def) ||
                 !uref_flow_get_program(uref, &programs) ||
                 sscanf(programs, "%"PRIu64",", &program_number) != 1))
        return false;

    struct uprobe_selprog_output *output =
        uprobe_selprog_output_by_id(uprobe, upipe, flow_id);
    if (unlikely(output == NULL)) {
        uprobe_throw_fatal(uprobe, upipe, UPROBE_ERR_ALLOC);
        return false;
    }

    if (output->flow_def != NULL)
        uref_free(output->flow_def);
    output->flow_def = uref_dup(uref);
    if (unlikely(output->flow_def == NULL)) {
        uprobe_throw_fatal(uprobe, upipe, UPROBE_ERR_ALLOC);
        return false;
    }

    bool was_selected = output->selected;
    output->program_number = program_number;
    uprobe_selprog_update_list(uprobe);
    output->selected = uprobe_selprog_check(uprobe, program_number);
    output->program_def = !ubase_ncmp(def, "program.");

    if (!output->program_def) {
        if (output->void_pipe != NULL) {
            upipe_release(output->void_pipe);
            output->void_pipe = NULL;
        }

        if (was_selected && !output->selected)
            uprobe_throw(uprobe->next, output->split_pipe,
                         UPROBE_SPLIT_DEL_FLOW, output->flow_id);

        if (!strcmp(uprobe_selprog->programs, "auto")) {
            uprobe_selprog->has_selection = true;
            uprobe_selprog_set_internal_va(uprobe,
                                           "%"PRIu64",", program_number);
        }
        return !output->selected;
    }

    if (output->selected) {
        /* try to set the new flow definition to the subpipe; if it
         * fails we have to close and reopen everything */
        if (output->void_pipe != NULL &&
            !upipe_set_flow_def(output->void_pipe, output->flow_def)) {
            upipe_release(output->void_pipe);
            output->void_pipe = NULL;
        }

        if (output->void_pipe == NULL) {
            output->void_pipe = upipe_flow_alloc_sub(upipe,
                uprobe_pfx_adhoc_alloc_va(uprobe, UPROBE_LOG_DEBUG,
                                          "program %"PRIu64,
                                          program_number),
                output->flow_def);
            if (unlikely(output->void_pipe == NULL))
                uprobe_throw_fatal(uprobe, upipe, UPROBE_ERR_ALLOC);
        }
    } else if (was_selected) {
        upipe_release(output->void_pipe);
        output->void_pipe = NULL;
    }
    return true;
}

/** @internal @This catches del_flow events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return true if the event was caught and handled
 */
static bool uprobe_selprog_del_flow(struct uprobe *uprobe, struct upipe *upipe,
                                    enum uprobe_event event, va_list args)
{
    struct uprobe_selprog *uprobe_selprog = uprobe_selprog_from_uprobe(uprobe);
    uint64_t flow_id = va_arg(args, uint64_t);
    struct uprobe_selprog_output *output =
        uprobe_selprog_output_by_id(uprobe, upipe, flow_id);
    if (unlikely(output == NULL)) {
        uprobe_throw_fatal(uprobe, upipe, UPROBE_ERR_ALLOC);
        return false;
    }

    struct uchain *uchain;
    ulist_delete_foreach(&uprobe_selprog->outputs, uchain) {
        if (uprobe_selprog_output_from_uchain(uchain) == output) {
            ulist_delete(&uprobe_selprog->outputs, uchain);
            break;
        }
    }
    uprobe_selprog_update_list(uprobe);

    bool was_selected = output->selected;
    bool was_program = output->program_def;
    if (likely(output->flow_def != NULL))
        uref_free(output->flow_def);
    if (output->void_pipe != NULL)
        upipe_release(output->void_pipe);
    free(output);

    if (uprobe_selprog->auto_cfg)
        uprobe_selprog_check_auto(uprobe);
    return was_program || !was_selected;
}

/** @internal @This catches events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return true if the event was caught and handled
 */
static bool uprobe_selprog_throw(struct uprobe *uprobe, struct upipe *upipe,
                                 enum uprobe_event event, va_list args)
{
    switch (event) {
        case UPROBE_SPLIT_ADD_FLOW:
            return uprobe_selprog_add_flow(uprobe, upipe, event, args);
        case UPROBE_SPLIT_DEL_FLOW:
            return uprobe_selprog_del_flow(uprobe, upipe, event, args);
        default:
            return false;
    }
}

/** @This allocates a new uprobe_selprog structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param programs comma-separated list of programs or attribute/value pairs
 * (name=ABC) to select, terminated by a comma, or "auto" to automatically
 * select the first program carrying elementary streams, or "all"
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_selprog_alloc(struct uprobe *next, const char *programs)
{
    assert(programs != NULL);
    struct uprobe_selprog *uprobe_selprog =
        malloc(sizeof(struct uprobe_selprog));
    if (unlikely(uprobe_selprog == NULL))
        return NULL;
    struct uprobe *uprobe = uprobe_selprog_to_uprobe(uprobe_selprog);
    uprobe_init(uprobe, uprobe_selprog_throw, next);
    uprobe_selprog->has_selection = false;
    uprobe_selprog->all_programs = strdup("");
    uprobe_selprog->programs = NULL;
    ulist_init(&uprobe_selprog->outputs);
    uprobe_selprog_set(uprobe, programs);
    return uprobe;
}

/** @This frees a uprobe_selprog structure.
 *
 * @param uprobe structure to free
 * @return next probe
 */
struct uprobe *uprobe_selprog_free(struct uprobe *uprobe)
{
    struct uprobe *next = uprobe->next;
    struct uprobe_selprog *uprobe_selprog =
        uprobe_selprog_from_uprobe(uprobe);
    assert(ulist_empty(&uprobe_selprog->outputs));
    free(uprobe_selprog->programs);
    free(uprobe_selprog->all_programs);
    free(uprobe_selprog);
    return next;
}

/** @This returns the programs selected by this probe.
 *
 * @param uprobe pointer to probe
 * @param programs_p filled in with a comma-separated list of programs or
 * attribute/value pairs (name=ABC) to select, terminated by a comma, or "all",
 * or "auto" if no program has been found yet
 */
void uprobe_selprog_get(struct uprobe *uprobe, const char **programs_p)
{
    struct uprobe_selprog *uprobe_selprog =
        uprobe_selprog_from_uprobe(uprobe);
    *programs_p = uprobe_selprog->programs;
}

/** @This returns a list of all the programs available.
 *
 * @param uprobe pointer to probe
 * @param programs_p filled in with a comma-separated list of all programs,
 * terminated by a comma
 */
void uprobe_selprog_list(struct uprobe *uprobe, const char **programs_p)
{
    struct uprobe_selprog *uprobe_selprog =
        uprobe_selprog_from_uprobe(uprobe);
    *programs_p = uprobe_selprog->all_programs;
}

/** @This changes the programs selected by this probe.
 *
 * @param uprobe pointer to probe
 * @param programs comma-separated list of programs or attribute/value pairs
 * (name=ABC) to select, terminated by a comma, or "auto" to automatically
 * select the first program carrying elementary streams, or "all"
 * elementary streams, or "all"
 */
void uprobe_selprog_set(struct uprobe *uprobe, const char *programs)
{
    struct uprobe_selprog *uprobe_selprog =
        uprobe_selprog_from_uprobe(uprobe);
    uprobe_selprog->auto_cfg = !strcmp(programs, "auto");
    if (!uprobe_selprog->auto_cfg || !uprobe_selprog->has_selection)
        uprobe_selprog_set_internal(uprobe, programs);
}

/** @This changes the programs selected by this probe, with printf-style
 * syntax.
 *
 * @param uprobe pointer to probe
 * @param format format of the syntax, followed by optional arguments
 */
void uprobe_selprog_set_va(struct uprobe *uprobe, const char *format, ...)
{
    UBASE_VARARG(uprobe_selprog_set(uprobe, string))
}
