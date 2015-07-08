/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
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
 * @short Upipe module setting arbitrary attributes to flow definitions
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-modules/upipe_setflowdef.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <assert.h>

/** @internal @This is the private context of a setflowdef pipe. */
struct upipe_setflowdef {
    /** refcount management structure */
    struct urefcount urefcount;

    /** pipe acting as output */
    struct upipe *output;
    /** input flow definition packet */
    struct uref *flow_def_input;
    /** output flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** dictionary to set */
    struct uref *dict;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_setflowdef, upipe, UPIPE_SETFLOWDEF_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_setflowdef, urefcount, upipe_setflowdef_free)
UPIPE_HELPER_VOID(upipe_setflowdef)
UPIPE_HELPER_OUTPUT(upipe_setflowdef, output, flow_def, output_state, request_list)

/** @internal @This allocates a setflowdef pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_setflowdef_alloc(struct upipe_mgr *mgr,
                                            struct uprobe *uprobe,
                                            uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_setflowdef_alloc_void(mgr, uprobe, signature,
                                                      args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_setflowdef *upipe_setflowdef =
        upipe_setflowdef_from_upipe(upipe);
    upipe_setflowdef_init_urefcount(upipe);
    upipe_setflowdef_init_output(upipe);
    upipe_setflowdef->dict = NULL;
    upipe_setflowdef->flow_def_input = NULL;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_setflowdef_input(struct upipe *upipe, struct uref *uref,
                                   struct upump **upump_p)
{
    upipe_setflowdef_output(upipe, uref, upump_p);
}

/** @internal @This builds the output flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_setflowdef_build_flow_def(struct upipe *upipe)
{
    struct upipe_setflowdef *upipe_setflowdef =
        upipe_setflowdef_from_upipe(upipe);
    struct uref *flow_def_dup;
    if(upipe_setflowdef->flow_def_input == NULL)
        return UBASE_ERR_UNHANDLED;
    if ((flow_def_dup = uref_dup(upipe_setflowdef->flow_def_input)) == NULL)
        return UBASE_ERR_ALLOC;

    if (unlikely(upipe_setflowdef->dict == NULL)) {
        upipe_setflowdef_store_flow_def(upipe, flow_def_dup);
        return UBASE_ERR_NONE;
    }

    if (upipe_setflowdef->dict->udict != NULL) {
        if (flow_def_dup->udict == NULL) {
            flow_def_dup->udict = udict_alloc(flow_def_dup->mgr->udict_mgr, 0);
            if (unlikely(flow_def_dup->udict == NULL)) {
                uref_free(flow_def_dup);
                return UBASE_ERR_ALLOC;
            }
        }
        const char *name = NULL;
        enum udict_type type = UDICT_TYPE_END;
        while (ubase_check(udict_iterate(upipe_setflowdef->dict->udict,
                                         &name, &type)) &&
               type != UDICT_TYPE_END) {
            size_t size;
            const uint8_t *v1 = NULL;
            udict_get(upipe_setflowdef->dict->udict, name, type, &size, &v1);
            uint8_t *v2 = NULL;
            udict_set(flow_def_dup->udict, name, type, size, &v2);
            if (unlikely(v1 == NULL || v2 == NULL)) {
                uref_free(flow_def_dup);
                return UBASE_ERR_ALLOC;
            }
            memcpy(v2, v1, size);
        }
    }

    upipe_setflowdef_store_flow_def(upipe, flow_def_dup);
    /* force outputting the flow def */
    upipe_setflowdef_output(upipe, NULL, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_setflowdef_set_flow_def(struct upipe *upipe,
                                         struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    struct uref *flow_def_dup;
    if ((flow_def_dup = uref_dup(flow_def)) == NULL)
        return UBASE_ERR_ALLOC;

    struct upipe_setflowdef *upipe_setflowdef =
        upipe_setflowdef_from_upipe(upipe);
    uref_free(upipe_setflowdef->flow_def_input);
    upipe_setflowdef->flow_def_input = flow_def_dup;
    upipe_setflowdef_build_flow_def(upipe);
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current dictionary being set into urefs.
 *
 * @param upipe description structure of the pipe
 * @param dict_p filled with the current dictionary
 * @return an error code
 */
static int _upipe_setflowdef_get_dict(struct upipe *upipe, struct uref **dict_p)
{
    struct upipe_setflowdef *upipe_setflowdef = upipe_setflowdef_from_upipe(upipe);
    *dict_p = upipe_setflowdef->dict;
    return UBASE_ERR_NONE;
}

/** @This sets the dictionary to set into urefs.
 *
 * @param upipe description structure of the pipe
 * @param dict dictionary to set
 * @return an error code
 */
static int _upipe_setflowdef_set_dict(struct upipe *upipe, struct uref *dict)
{
    struct upipe_setflowdef *upipe_setflowdef = upipe_setflowdef_from_upipe(upipe);
    if (upipe_setflowdef->dict != NULL)
        uref_free(upipe_setflowdef->dict);
    if (dict != NULL) {
        upipe_setflowdef->dict = uref_dup(dict);
        if (upipe_setflowdef->dict == NULL) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
    } else
        upipe_setflowdef->dict = NULL;
    upipe_setflowdef_build_flow_def(upipe);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a setflowdef pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_setflowdef_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_setflowdef_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_setflowdef_free_output_proxy(upipe, request);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_setflowdef_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_setflowdef_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_setflowdef_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_setflowdef_set_output(upipe, output);
        }

        case UPIPE_SETFLOWDEF_GET_DICT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_SETFLOWDEF_SIGNATURE)
            struct uref **dict_p = va_arg(args, struct uref **);
            return _upipe_setflowdef_get_dict(upipe, dict_p);
        }
        case UPIPE_SETFLOWDEF_SET_DICT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_SETFLOWDEF_SIGNATURE)
            struct uref *dict = va_arg(args, struct uref *);
            return _upipe_setflowdef_set_dict(upipe, dict);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_setflowdef_free(struct upipe *upipe)
{
    struct upipe_setflowdef *upipe_setflowdef = upipe_setflowdef_from_upipe(upipe);
    upipe_throw_dead(upipe);

    upipe_setflowdef_clean_output(upipe);
    uref_free(upipe_setflowdef->flow_def_input);

    if (upipe_setflowdef->dict != NULL)
        uref_free(upipe_setflowdef->dict);

    upipe_setflowdef_clean_urefcount(upipe);
    upipe_setflowdef_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_setflowdef_mgr = {
    .refcount = NULL,
    .signature = UPIPE_SETFLOWDEF_SIGNATURE,

    .upipe_alloc = upipe_setflowdef_alloc,
    .upipe_input = upipe_setflowdef_input,
    .upipe_control = upipe_setflowdef_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all setflowdef pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_setflowdef_mgr_alloc(void)
{
    return &upipe_setflowdef_mgr;
}
