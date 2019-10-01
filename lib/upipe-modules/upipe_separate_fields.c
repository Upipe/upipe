/*
 * Copyright (C) 2017-2018 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
 *          James Darnley
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
 * @short Upipe module to separate the fields of an interlaced picture
 */

#include <stdlib.h>
#include <limits.h>

#include <upipe/upipe.h>
#include <upipe/uclock.h>
#include <upipe/uref_clock.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_flow_def.h>

#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>

#include <upipe-modules/upipe_separate_fields.h>

struct upipe_separate_fields {
    /** refcount management structure */
    struct urefcount urefcount;

    /* output stuff */
    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** public upipe structure */
    struct upipe upipe;

    /** frame duration in ticks */
    uint64_t field_duration;
};

UPIPE_HELPER_UPIPE(upipe_separate_fields, upipe, UPIPE_SEPARATE_FIELDS_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_separate_fields, urefcount, upipe_separate_fields_free)
UPIPE_HELPER_VOID(upipe_separate_fields)
UPIPE_HELPER_OUTPUT(upipe_separate_fields, output, flow_def, output_state,
                    request_list)

static int upipe_separate_fields_set_flow_def(struct upipe *upipe,
                                       struct uref *flow_def)
{
    struct upipe_separate_fields *ctx = upipe_separate_fields_from_upipe(upipe);

    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, "pic."));

    /* TODO: check vertical chroma subsampling */
    uint64_t height;
    UBASE_RETURN(uref_pic_flow_get_vsize(flow_def, &height));
    if (height & 1) {
        upipe_err(upipe, "flow def is an odd height");
        upipe_throw_error(upipe, UBASE_ERR_UNKNOWN);
        return UBASE_ERR_UNKNOWN;
    }

    struct urational fps;
    UBASE_RETURN(uref_pic_flow_get_fps(flow_def, &fps));

    struct uref *flow_def_dup = uref_dup(flow_def);
    if (unlikely(flow_def_dup == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    fps.num *= 2;
    height /= 2;
    ctx->field_duration = UCLOCK_FREQ * fps.den / fps.num;
    UBASE_RETURN(uref_pic_flow_set_fps(flow_def_dup, fps));
    UBASE_RETURN(uref_pic_flow_set_vsize(flow_def_dup, height));
    if (ubase_check(uref_pic_get_progressive(flow_def_dup)))
        UBASE_RETURN(uref_pic_delete_progressive(flow_def_dup));

    upipe_separate_fields_store_flow_def(upipe, flow_def_dup);

    return UBASE_ERR_NONE;
}

static int upipe_separate_fields_control(struct upipe *upipe, int command,
                                  va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_separate_fields_set_flow_def(upipe, flow_def);
        }
        case UPIPE_REGISTER_REQUEST:
        case UPIPE_UNREGISTER_REQUEST:
        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_separate_fields_control_output(upipe, command, args);
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

static void upipe_separate_fields_free(struct upipe *upipe)
{
    upipe_separate_fields_clean_urefcount(upipe);
    upipe_separate_fields_clean_output(upipe);
    upipe_separate_fields_free_void(upipe);
}

static struct upipe *upipe_separate_fields_alloc(struct upipe_mgr *mgr,
                                           struct uprobe *uprobe,
                                           uint32_t signature,
                                           va_list args)
{
    struct upipe *upipe =
        upipe_separate_fields_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_separate_fields_init_urefcount(upipe);
    upipe_separate_fields_init_output(upipe);

    return upipe;
}

static void upipe_separate_fields_input(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p)
{
    struct upipe_separate_fields *ctx = upipe_separate_fields_from_upipe(upipe);

    size_t width, height;
    uint8_t macropixel;
    if (unlikely(!ubase_check(uref_pic_size(uref, &width, &height,
                        &macropixel)))) {
        upipe_warn(upipe, "dropping picture");
        upipe_throw_error(upipe, UBASE_ERR_INVALID);
        uref_free(uref);
        return;
    }

    bool has_progressive_attr = ubase_check(uref_pic_get_progressive(uref));
    bool has_tff_attr         = ubase_check(uref_pic_get_tff(uref));

    if (has_progressive_attr)
        upipe_warn(upipe, "picture marked as progrssive, separating fields anyway");

    uref_clock_set_duration(uref, ctx->field_duration);

    struct uref *odd = NULL, *even = NULL;
    int err = uref_split_fields(uref, &odd, &even);
    if (!ubase_check(err)) {
        upipe_err_va(upipe, "%s", ubase_err_str(err));
        uref_free(uref);
        return;
    }

    /* first field */
    struct uref *uref_field = has_tff_attr ? even : odd;

    uref_pic_delete_progressive(uref_field);
    if (has_tff_attr) {
        uref_pic_delete_bf(uref_field);
        uref_pic_set_tf(uref_field);
    } else {
        uref_pic_delete_tf(uref_field);
        uref_pic_set_bf(uref_field);
    }

    upipe_separate_fields_output(upipe, uref_field, NULL);

    /* second field */
    uref_field = has_tff_attr ? odd : even;

    uref_pic_delete_progressive(uref_field);
    if (!has_tff_attr) {
        uref_pic_delete_bf(uref_field);
        uref_pic_set_tf(uref_field);
    } else {
        uref_pic_delete_tf(uref_field);
        uref_pic_set_bf(uref_field);
    }

    uref_clock_add_date_sys(uref_field, ctx->field_duration);
    uref_clock_add_date_prog(uref_field, ctx->field_duration);
    uref_clock_add_date_orig(uref_field, ctx->field_duration);

    upipe_separate_fields_output(upipe, uref_field, NULL);

    uref_free(uref);
}

static struct upipe_mgr upipe_separate_fields_mgr = {
    .refcount = NULL,
    .signature = UPIPE_SEPARATE_FIELDS_SIGNATURE,

    .upipe_alloc = upipe_separate_fields_alloc,
    .upipe_input = upipe_separate_fields_input,
    .upipe_control = upipe_separate_fields_control,

    .upipe_mgr_control = NULL,
};

struct upipe_mgr *upipe_separate_fields_mgr_alloc(void)
{
    return &upipe_separate_fields_mgr;
}
