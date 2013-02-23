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
 * @short Upipe module setting arbitrary attributes to urefs
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-modules/upipe_setattr.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <assert.h>

/** @internal @This is the private context of a setattr pipe. */
struct upipe_setattr {
    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /** dictionary to set */
    struct uref *dict;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_setattr, upipe)
UPIPE_HELPER_OUTPUT(upipe_setattr, output, flow_def, flow_def_sent)

/** @internal @This allocates a setattr pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_setattr_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe)
{
    struct upipe_setattr *upipe_setattr = malloc(sizeof(struct upipe_setattr));
    if (unlikely(upipe_setattr == NULL))
        return NULL;
    struct upipe *upipe = upipe_setattr_to_upipe(upipe_setattr);
    upipe_init(upipe, mgr, uprobe);
    upipe_setattr_init_output(upipe);
    upipe_setattr->dict = NULL;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_setattr_input(struct upipe *upipe, struct uref *uref,
                                struct upump *upump)
{
    struct upipe_setattr *upipe_setattr = upipe_setattr_from_upipe(upipe);
    const char *def;
    if (unlikely(uref_flow_get_def(uref, &def))) {
        upipe_setattr_store_flow_def(upipe, uref);
        return;
    }

    if (unlikely(upipe_setattr->dict == NULL)) {
        upipe_setattr_output(upipe, uref, upump);
        return;
    }

    const char *name = NULL;
    enum udict_type type = UDICT_TYPE_END;
    while (uref_attr_iterate(upipe_setattr->dict, &name, &type) &&
           type != UDICT_TYPE_END) {
        size_t size;
        const uint8_t *v1 = udict_get(upipe_setattr->dict->udict, name, type,
                                      &size);
        uint8_t *v2 = udict_set(uref->udict, name, type, size);
        if (unlikely(v1 == NULL || v2 == NULL)) {
            uref_free(uref);
            upipe_throw_aerror(upipe);
            return;
        }
        memcpy(v2, v1, size);
    }
    upipe_setattr_output(upipe, uref, upump);
}

/** @internal @This returns the current dictionary being set into urefs.
 *
 * @param upipe description structure of the pipe
 * @param dict_p filled with the current dictionary
 * @return false in case of error
 */
static bool _upipe_setattr_get_dict(struct upipe *upipe, struct uref **dict_p)
{
    struct upipe_setattr *upipe_setattr = upipe_setattr_from_upipe(upipe);
    *dict_p = upipe_setattr->dict;
    return true;
}

/** @This sets the dictionary to set into urefs.
 *
 * @param upipe description structure of the pipe
 * @param dict dictionary to set
 * @return false in case of error
 */
static bool _upipe_setattr_set_dict(struct upipe *upipe, struct uref *dict)
{
    struct upipe_setattr *upipe_setattr = upipe_setattr_from_upipe(upipe);
    if (upipe_setattr->dict != NULL)
        uref_free(upipe_setattr->dict);
    if (dict != NULL) {
        upipe_setattr->dict = uref_dup(dict);
        if (upipe_setattr->dict == NULL) {
            upipe_throw_aerror(upipe);
            return false;
        }
    } else
        upipe_setattr->dict = NULL;
    return true;
}

/** @internal @This processes control commands on a setattr pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_setattr_control(struct upipe *upipe,
                                enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_setattr_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_setattr_set_output(upipe, output);
        }

        case UPIPE_SETATTR_GET_DICT: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_SETATTR_SIGNATURE);
            struct uref **dict_p = va_arg(args, struct uref **);
            return _upipe_setattr_get_dict(upipe, dict_p);
        }
        case UPIPE_SETATTR_SET_DICT: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_SETATTR_SIGNATURE);
            struct uref *dict = va_arg(args, struct uref *);
            return _upipe_setattr_set_dict(upipe, dict);
        }
        default:
            return false;
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

    upipe_clean(upipe);
    free(upipe_setattr);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_setattr_mgr = {
    .signature = UPIPE_SETATTR_SIGNATURE,

    .upipe_alloc = upipe_setattr_alloc,
    .upipe_input = upipe_setattr_input,
    .upipe_control = upipe_setattr_control,
    .upipe_free = upipe_setattr_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for all setattr pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_setattr_mgr_alloc(void)
{
    return &upipe_setattr_mgr;
}
