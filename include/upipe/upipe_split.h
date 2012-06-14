/*****************************************************************************
 * upipe_split.h: common declarations of split pipes
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

#ifndef _UPIPE_UPIPE_SPLIT_H_
/** @hidden */
#define _UPIPE_UPIPE_SPLIT_H_

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/ulist.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <assert.h>

/*
 * Definitions for one output
 */

/** structure defining an output of the split module */
struct upipe_split_output {
    /** structure for double-linked lists */
    struct uchain uchain;
    /** suffix added to every flow on this output (NULL = default output) */
    char *flow_suffix;
    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** pipe acting as output */
    struct upipe *output;
};

/** @This allocates and initializes a new upipe_split_output.
 *
 * @param fs flow suffix, or NULL for default output
 * @return allocated upipe_split_output or NULL in case of allocation failure
 */
static inline struct upipe_split_output *upipe_split_output_alloc(const char *fs)
{
    struct upipe_split_output *output =
        malloc(sizeof(struct upipe_split_output));
    if (unlikely(output == NULL)) return NULL;
    uchain_init(&output->uchain);
    if (likely(fs != NULL))
        output->flow_suffix = strdup(fs);
    else
        output->flow_suffix = NULL;
    output->ubuf_mgr = NULL;
    output->output = NULL;
    return output;
}

/** @This frees a struct upipe_split_output.
 *
 * @param output structure to free
 */
static inline void upipe_split_output_free(struct upipe_split_output *output)
{
    free(output->flow_suffix);
    if (likely(output->ubuf_mgr != NULL))
        ubuf_mgr_release(output->ubuf_mgr);
    if (likely(output->output != NULL))
        upipe_release(output->output);
    free(output);
}

/** @This returns the high-level struct upipe_split_output structure.
 *
 * @param u pointer to the uchain structure wrapped into the
 * upipe_split_output
 * @return pointer to the upipe_split_output structure
 */
static inline struct upipe_split_output *upipe_split_output_from_uchain(struct uchain *u)
{
    return container_of(u, struct upipe_split_output, uchain);
}

/** @This returns the uchain structure used for FIFO, LIFO and lists.
 *
 * @param output upipe_split_output structure
 * @return pointer to the uchain structure
 */
static inline struct uchain *upipe_split_output_to_uchain(struct upipe_split_output *o)
{
    return &o->uchain;
}

/*
 * Definitions for a list of output
 */

/** @This initializes a upipe_split_outputs structure.
 *
 * @param outputs pointer to the ulist structure
 */
static inline void upipe_split_outputs_init(struct ulist *outputs)
{
    ulist_init(outputs);
}

/** @This walks through a upipe_split_outputs structure.
 *
 * @param upipe_split_outputs pointer to a upipe_split_outputs structure
 * @param output iterator
 */
#define upipe_split_outputs_foreach(upipe_split_outputs, output)            \
    struct uchain *upipe_split_outputs_uchain;                              \
    for (upipe_split_outputs_uchain = (upipe_split_outputs)->first,         \
             output =                                                       \
                 upipe_split_output_from_uchain(upipe_split_outputs_uchain);\
         upipe_split_outputs_uchain != NULL;                                \
         upipe_split_outputs_uchain = upipe_split_outputs_uchain->next,     \
             output =                                                       \
                 upipe_split_output_from_uchain(upipe_split_outputs_uchain))

/** @internal @This is a helper function to match two suffixes (which can
 * be NULL).
 *
 * @param fs1 first flow suffix
 * @param fs2 second flow suffix
 * @return true if the two suffixes are identical
 */
static inline bool upipe_split_outputs_match(const char *fs1, const char *fs2)
{
    return (fs1 == NULL && fs2 == NULL) ||
           (fs1 != NULL && fs2 != NULL && !strcmp(fs1, fs2));
}

/** @This returns the output for a given flow suffix.
 *
 * @param outputs pointer to the upipe_split_outputs structure
 * @param flow_suffix flow suffix, or NULL for default output
 * @return pointer to the output, or NULL if not found
 */
static inline struct upipe_split_output *upipe_split_outputs_get(
        struct ulist *outputs, const char *flow_suffix)
{
    struct upipe_split_output *output;
    upipe_split_outputs_foreach (outputs, output) {
        if (unlikely(upipe_split_outputs_match(flow_suffix, output->flow_suffix)))
            return output;
    }
    return NULL;
}

/** @This deletes the output for a given flow suffix.
 *
 * @param outputs pointer to the upipe_split_outputs structure
 * @param flow_suffix flow suffix, or NULL for default output
 * @return true if the output was found and deleted
 */
static inline bool upipe_split_outputs_delete(struct ulist *outputs,
                                              const char *flow_suffix)
{
    struct uchain *uchain;
    ulist_delete_foreach (outputs, uchain) {
        struct upipe_split_output *output =
            upipe_split_output_from_uchain(uchain);
        if (unlikely(upipe_split_outputs_match(flow_suffix, output->flow_suffix))) {
            ulist_delete(outputs, uchain);
            upipe_split_output_free(output);
            return true;
        }
    }
    return false;
}

/** @This allocates and adds a new output.
 *
 * @param outputs pointer to the upipe_split_outputs structure
 * @param flow_suffix new flow suffix
 * @return pointer to the output, or NULL in case of error
 */
static inline struct upipe_split_output *upipe_split_outputs_add(
        struct ulist *outputs, const char *flow_suffix)
{
    struct upipe_split_output *output = upipe_split_output_alloc(flow_suffix);
    if (unlikely(output == NULL)) return NULL;
    ulist_add(outputs, upipe_split_output_to_uchain(output));
    return output;
}

/** @This cleans up a upipe_split_outputs structure.
 *
 * @param outputs pointer to the ulist structure
 */
static inline void upipe_split_outputs_clean(struct ulist *outputs)
{
    struct uchain *uchain;
    ulist_delete_foreach (outputs, uchain) {
        struct upipe_split_output *output =
            upipe_split_output_from_uchain(uchain);
        ulist_delete(outputs, uchain);
        upipe_split_output_free(output);
    }
}

/*
 * Other usual definitions
 */

/** super-set of the upipe structure with additional members */
struct upipe_split {
    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** list of outputs */
    struct ulist outputs;

    /** structure exported to application */
    struct upipe upipe;
};

/** @This returns the high-level upipe structure.
 *
 * @param upipe_split pointer to the upipe_split structure
 * @return pointer to the upipe structure
 */
static inline struct upipe *upipe_split_to_upipe(struct upipe_split *upipe_split)
{
    return &upipe_split->upipe;
}

/** @This returns the private upipe_split structure.
 *
 * @param upipe description structure of the pipe
 * @return pointer to the upipe_split structure
 */
static inline struct upipe_split *upipe_split_from_upipe(struct upipe *upipe)
{
    return container_of(upipe, struct upipe_split, upipe);
}

UPIPE_STRUCT_TEMPLATE(split, uref_mgr, struct uref_mgr *)

/** @This returns a pointer to the ulist structure.
 *
 * @param upipe description structure of the pipe
 * @return pointer to the ulist structure
 */
static inline struct ulist *upipe_split_outputs(struct upipe *upipe)
{
    struct upipe_split *upipe_split = upipe_split_from_upipe(upipe);
    return &upipe_split->outputs;
}

/** @This outputs a uref to the output for the given flow suffix.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure to send
 * @param flow_suffix flow suffix to add
 * @return false in case the uref couldn't be sent
 */
static inline bool upipe_split_output(struct upipe *upipe, struct uref *uref,
                                      const char *flow_suffix)
{
    struct upipe_split *upipe_split = upipe_split_from_upipe(upipe);
    struct upipe_split_output *output =
        upipe_split_outputs_get(&upipe_split->outputs, flow_suffix);
    if (unlikely(output == NULL)) {
        /* get default output */
        output = upipe_split_outputs_get(&upipe_split->outputs, NULL);
        if (unlikely(output == NULL)) {
            uref_release(uref);
            return false;
        }
    }

    /* change flow */
    const char *flow;
    if (unlikely(!uref_flow_get_name(uref, &flow))) {
        if (unlikely(!uref_flow_set_name(&uref, flow_suffix))) {
            uref_release(uref);
            return false;
        }
    } else {
        char new_flow[strlen(flow) + strlen(flow_suffix) + 2];
        sprintf(new_flow, "%s.%s", flow, flow_suffix);
        if (unlikely(!uref_flow_set_name(&uref, new_flow))) {
            uref_release(uref);
            return false;
        }
    }

    upipe_input(output->output, uref);
    return true;
}

/** @This outputs a uref to the output for the given flow suffix, with
 * printf-style name generation.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure to send
 * @param flow_suffix flow suffix to add
 * @return false in case the uref couldn't be sent
 */
static inline bool upipe_split_output_va(struct upipe *upipe, struct uref *uref,
                                         const char *format, ...)
                   __attribute__ ((format(printf, 3, 4)));
/** @hidden */
static inline bool upipe_split_output_va(struct upipe *upipe, struct uref *uref,
                                         const char *format, ...)
{
    UBASE_VARARG(upipe_split_output(upipe, uref, string));
}

/** @This checks if the split pipe is ready to process data.
 * This only checks uref_mgr and default output.
 *
 * @param upipe description structure of the pipe
 */
static inline bool upipe_split_ready(struct upipe *upipe)
{
    struct upipe_split *upipe_split = upipe_split_from_upipe(upipe);
    return upipe_split->uref_mgr != NULL &&
           upipe_split_outputs_get(&upipe_split->outputs, NULL) != NULL;
}

/** @This initializes the common members of split pipes.
 *
 * @param upipe description structure of the pipe
 */
static inline void upipe_split_init(struct upipe *upipe)
{
    struct upipe_split *upipe_split = upipe_split_from_upipe(upipe);
    UPIPE_OBJ_INIT_TEMPLATE(upipe_split, uref_mgr)
    ulist_init(&upipe_split->outputs);
}

/** @This processes common control commands on a split pipe.
 *
 * @param upipe description structure of the pipe
 * @return true if the command has been correctly processed
 */
static inline bool upipe_split_control(struct upipe *upipe, 
                                       enum upipe_control control,
                                       va_list args)
{
    struct upipe_split *upipe_split = upipe_split_from_upipe(upipe);
    switch (control) {
        UPIPE_OBJ_CONTROL_TEMPLATE(upipe_split, UPIPE, uref_mgr, UREF_MGR, uref_mgr)

/** @hidden */
#define UPIPE_OBJ_CONTROL_TEMPLATE_SPLIT(name, NAME, type)                  \
        case UPIPE_SPLIT_GET_##NAME: {                                      \
            struct type **p = va_arg(args, struct type **);                 \
            const char *flow_suffix = va_arg(args, const char *);           \
            struct upipe_split_output *output =                             \
                upipe_split_outputs_get(&upipe_split->outputs, flow_suffix);\
            if (unlikely(output == NULL)) return false;                     \
            assert(p != NULL);                                              \
            *p = output->name;                                              \
            return true;                                                    \
        }                                                                   \
        case UPIPE_SPLIT_SET_##NAME: {                                      \
            struct type *s = va_arg(args, struct type *);                   \
            const char *flow_suffix = va_arg(args, const char *);           \
            struct upipe_split_output *output =                             \
                upipe_split_outputs_get(&upipe_split->outputs, flow_suffix);\
            if (unlikely(output == NULL)) return false;                     \
            if (unlikely(output->name != NULL))                             \
                type##_release(output->name);                               \
            output->name = s;                                               \
            if (likely(output->name != NULL))                               \
                type##_use(output->name);                                   \
            return true;                                                    \
        }
        UPIPE_OBJ_CONTROL_TEMPLATE_SPLIT(ubuf_mgr, UBUF_MGR, ubuf_mgr)
        UPIPE_OBJ_CONTROL_TEMPLATE_SPLIT(output, OUTPUT, upipe)
#undef UPIPE_OBJ_CONTROL_TEMPLATE_SPLIT

        default:
            return false;
    }
}

/** @This cleans up the common members of split pipes.
 *
 * @param upipe description structure of the pipe
 */
static inline void upipe_split_clean(struct upipe *upipe)
{
    struct upipe_split *upipe_split = upipe_split_from_upipe(upipe);
    UPIPE_OBJ_CLEAN_TEMPLATE(upipe_split, uref_mgr, uref_mgr)
    upipe_split_outputs_clean(&upipe_split->outputs);
}

#endif
