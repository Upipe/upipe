/*
 * Copyright (C) 2013-2014 OpenHeadend S.A.R.L.
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
 * @short Upipe module setting arbitrary attributes to urefs
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
#include <upipe-modules/upipe_setattr.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <assert.h>

/** @internal @This is the private context of a setattr pipe. */
struct upipe_setattr {
    /** refcount management structure */
    struct urefcount urefcount;

    /** pipe acting as output */
    struct upipe *output;
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

UPIPE_HELPER_UPIPE(upipe_setattr, upipe, UPIPE_SETATTR_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_setattr, urefcount, upipe_setattr_free)
UPIPE_HELPER_VOID(upipe_setattr)
UPIPE_HELPER_OUTPUT(upipe_setattr, output, flow_def, output_state, request_list)

/** @internal @This allocates a setattr pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_setattr_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_setattr_alloc_void(mgr, uprobe, signature,
                                                   args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_setattr *upipe_setattr = upipe_setattr_from_upipe(upipe);
    upipe_setattr_init_urefcount(upipe);
    upipe_setattr_init_output(upipe);
    upipe_setattr->dict = NULL;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_setattr_input(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p)
{
    struct upipe_setattr *upipe_setattr = upipe_setattr_from_upipe(upipe);
    if (unlikely(upipe_setattr->dict == NULL)) {
        upipe_setattr_output(upipe, uref, upump_p);
        return;
    }

    if (upipe_setattr->dict->udict != NULL) {
        if (uref->udict == NULL) {
            uref->udict = udict_alloc(uref->mgr->udict_mgr, 0);
            if (unlikely(uref->udict == NULL)) {
                upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
                uref_free(uref);
                return;
            }
        }
        const char *name = NULL;
        enum udict_type type = UDICT_TYPE_END;
        while (ubase_check(udict_iterate(upipe_setattr->dict->udict,
                                         &name, &type)) &&
               type != UDICT_TYPE_END) {
            size_t size;
            const uint8_t *v1 = NULL;
            udict_get(upipe_setattr->dict->udict, name, type, &size, &v1);
            uint8_t *v2 = NULL;
            udict_set(uref->udict, name, type, size, &v2);
            if (unlikely(v1 == NULL || v2 == NULL)) {
                uref_free(uref);
                upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
                return;
            }
            memcpy(v2, v1, size);
        }
    }
    upipe_setattr_output(upipe, uref, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_setattr_set_flow_def(struct upipe *upipe,
                                      struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    struct uref *flow_def_dup;
    if ((flow_def_dup = uref_dup(flow_def)) == NULL)
        return UBASE_ERR_ALLOC;
    upipe_setattr_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current dictionary being set into urefs.
 *
 * @param upipe description structure of the pipe
 * @param dict_p filled with the current dictionary
 * @return an error code
 */
static int _upipe_setattr_get_dict(struct upipe *upipe, struct uref **dict_p)
{
    struct upipe_setattr *upipe_setattr = upipe_setattr_from_upipe(upipe);
    *dict_p = upipe_setattr->dict;
    return UBASE_ERR_NONE;
}

/** @This sets the dictionary to set into urefs.
 *
 * @param upipe description structure of the pipe
 * @param dict dictionary to set
 * @return an error code
 */
static int _upipe_setattr_set_dict(struct upipe *upipe, struct uref *dict)
{
    struct upipe_setattr *upipe_setattr = upipe_setattr_from_upipe(upipe);
    if (upipe_setattr->dict != NULL)
        uref_free(upipe_setattr->dict);
    if (dict != NULL) {
        upipe_setattr->dict = uref_dup(dict);
        if (upipe_setattr->dict == NULL) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
    } else
        upipe_setattr->dict = NULL;
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a setattr pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_setattr_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_setattr_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_setattr_set_flow_def(upipe, flow_def);
        }

        case UPIPE_SETATTR_GET_DICT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_SETATTR_SIGNATURE)
            struct uref **dict_p = va_arg(args, struct uref **);
            return _upipe_setattr_get_dict(upipe, dict_p);
        }
        case UPIPE_SETATTR_SET_DICT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_SETATTR_SIGNATURE)
            struct uref *dict = va_arg(args, struct uref *);
            return _upipe_setattr_set_dict(upipe, dict);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_setattr_free(struct upipe *upipe)
{
    struct upipe_setattr *upipe_setattr = upipe_setattr_from_upipe(upipe);
    upipe_throw_dead(upipe);

    upipe_setattr_clean_output(upipe);

    if (upipe_setattr->dict != NULL)
        uref_free(upipe_setattr->dict);

    upipe_setattr_clean_urefcount(upipe);
    upipe_setattr_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_setattr_mgr = {
    .refcount = NULL,
    .signature = UPIPE_SETATTR_SIGNATURE,

    .upipe_alloc = upipe_setattr_alloc,
    .upipe_input = upipe_setattr_input,
    .upipe_control = upipe_setattr_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all setattr pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_setattr_mgr_alloc(void)
{
    return &upipe_setattr_mgr;
}
