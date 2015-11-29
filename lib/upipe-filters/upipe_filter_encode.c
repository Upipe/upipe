/*
 * Copyright (C) 2014-2015 OpenHeadend S.A.R.L.
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
 * @short Bin pipe encoding a flow
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uref.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_bin_input.h>
#include <upipe/upipe_helper_bin_output.h>
#include <upipe-modules/upipe_idem.h>
#include <upipe-filters/upipe_filter_encode.h>
#include <upipe-x264/upipe_x264.h>
#include <upipe-av/upipe_avcodec_encode.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/** @internal @This is the private context of a fenc manager. */
struct upipe_fenc_mgr {
    /** refcount management structure */
    struct urefcount urefcount;

    /** pointer to avcenc manager */
    struct upipe_mgr *avcenc_mgr;
    /** pointer to x264 manager */
    struct upipe_mgr *x264_mgr;

    /** public upipe_mgr structure */
    struct upipe_mgr mgr;
};

UBASE_FROM_TO(upipe_fenc_mgr, upipe_mgr, upipe_mgr, mgr)
UBASE_FROM_TO(upipe_fenc_mgr, urefcount, urefcount, urefcount)

static int upipe_fenc_provide(struct upipe *upipe, struct uref *unused);

/** @internal @This is the private context of a fenc pipe. */
struct upipe_fenc {
    /** real refcount management structure */
    struct urefcount urefcount_real;
    /** refcount management structure exported to the public structure */
    struct urefcount urefcount;

    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** uref manager request */
    struct urequest uref_mgr_request;

    /** input flow def */
    struct uref *flow_def_input;
    /** uref serving as a dictionary for options */
    struct uref *options;
    /** x264 preset */
    char *preset;
    /** x264 tune */
    char *tune;
    /** x264 profile */
    char *profile;
    /** x264 speed control */
    uint64_t sc_latency;
    /** x262 */
    bool x262;

    /** probe for the last inner pipe */
    struct uprobe last_inner_probe;

    /** list of input bin requests */
    struct uchain input_request_list;
    /** list of output bin requests */
    struct uchain output_request_list;
    /** first inner pipe of the bin (avcdec) */
    struct upipe *first_inner;
    /** last inner pipe of the bin (avcdec) */
    struct upipe *last_inner;
    /** output */
    struct upipe *output;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_fenc, upipe, UPIPE_FENC_SIGNATURE)
UPIPE_HELPER_FLOW(upipe_fenc, "block.")
UPIPE_HELPER_UREFCOUNT(upipe_fenc, urefcount, upipe_fenc_no_ref)
UPIPE_HELPER_UREF_MGR(upipe_fenc, uref_mgr, uref_mgr_request,
                      upipe_fenc_provide, upipe_throw_provide_request, NULL)
UPIPE_HELPER_BIN_INPUT(upipe_fenc, first_inner, input_request_list)
UPIPE_HELPER_BIN_OUTPUT(upipe_fenc, last_inner_probe, last_inner, output,
                        output_request_list)

UBASE_FROM_TO(upipe_fenc, urefcount, urefcount_real, urefcount_real)

/** @hidden */
static void upipe_fenc_free(struct urefcount *urefcount_real);

/** @internal @This allocates the inner encoder pipe.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_fenc_alloc_inner(struct upipe *upipe)
{
    struct upipe_fenc *upipe_fenc = upipe_fenc_from_upipe(upipe);
    struct upipe_fenc_mgr *fenc_mgr = upipe_fenc_mgr_from_upipe_mgr(upipe->mgr);
    const char *def, *codec;
    if (ubase_check(uref_flow_get_def(upipe_fenc->flow_def_input, &def)) &&
        !ubase_check(uref_avcenc_get_codec_name(upipe_fenc->flow_def_input,
                                                &codec)) &&
        fenc_mgr->x264_mgr != NULL) {
        struct upipe *enc = upipe_void_alloc(fenc_mgr->x264_mgr,
                uprobe_pfx_alloc(
                    uprobe_use(&upipe_fenc->last_inner_probe),
                    UPROBE_LOG_VERBOSE, "x264"));
        if (unlikely(enc == NULL))
            return UBASE_ERR_INVALID;
        if (!ubase_ncmp(def, "block.mpeg2video."))
            upipe_fenc->x262 = true;

        upipe_fenc_store_first_inner(upipe, upipe_use(enc));
        upipe_fenc_store_last_inner(upipe, enc);
        return UBASE_ERR_NONE;
    }

    struct upipe *enc = upipe_flow_alloc(fenc_mgr->avcenc_mgr,
            uprobe_pfx_alloc(
                uprobe_use(&upipe_fenc->last_inner_probe),
                UPROBE_LOG_VERBOSE, "avcenc"), upipe_fenc->flow_def_input);
    if (unlikely(enc == NULL))
        return UBASE_ERR_INVALID;

    upipe_fenc_store_first_inner(upipe, upipe_use(enc));
    upipe_fenc_store_last_inner(upipe, enc);
    return UBASE_ERR_NONE;
}

/** @internal @This allocates a fenc pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_fenc_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature, va_list args)
{
    struct uref *flow_def_input;
    struct upipe *upipe = upipe_fenc_alloc_flow(mgr, uprobe, signature, args,
                                                &flow_def_input);
    if (unlikely(upipe == NULL))
        return NULL;
    struct upipe_fenc *upipe_fenc = upipe_fenc_from_upipe(upipe);
    upipe_fenc_init_urefcount(upipe);
    urefcount_init(upipe_fenc_to_urefcount_real(upipe_fenc),
                   upipe_fenc_free);
    upipe_fenc_init_uref_mgr(upipe);
    upipe_fenc_init_bin_input(upipe);
    upipe_fenc_init_bin_output(upipe, upipe_fenc_to_urefcount_real(upipe_fenc));
    upipe_fenc->flow_def_input = flow_def_input;
    upipe_fenc->options = NULL;
    upipe_fenc->preset = NULL;
    upipe_fenc->tune = NULL;
    upipe_fenc->profile = NULL;
    upipe_fenc->sc_latency = UINT64_MAX;
    upipe_fenc->x262 = false;
    upipe_throw_ready(upipe);
    upipe_fenc_demand_uref_mgr(upipe);

    if (unlikely(!ubase_check(upipe_fenc_alloc_inner(upipe)))) {
        upipe_release(upipe);
        return NULL;
    }

    return upipe;
}

/** @internal @This allocates the options uref.
 *
 * @param upipe description structure of the pipe
 * @param unused unused argument
 * @return an error code
 */
static int upipe_fenc_provide(struct upipe *upipe, struct uref *unused)
{
    struct upipe_fenc *upipe_fenc = upipe_fenc_from_upipe(upipe);
    if (upipe_fenc->uref_mgr != NULL && upipe_fenc->options == NULL)
        upipe_fenc->options = uref_alloc_control(upipe_fenc->uref_mgr);
    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_fenc_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    struct upipe_fenc *upipe_fenc = upipe_fenc_from_upipe(upipe);
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    if (upipe_fenc->last_inner != NULL) {
        if (ubase_check(upipe_set_flow_def(upipe_fenc->last_inner, flow_def)))
            return UBASE_ERR_NONE;
    }
    upipe_fenc_store_first_inner(upipe, NULL);
    upipe_fenc_store_last_inner(upipe, NULL);

    if (unlikely(!ubase_check(upipe_fenc_alloc_inner(upipe))))
        return UBASE_ERR_UNHANDLED;

    if (upipe_fenc->x262) {
        UBASE_RETURN(upipe_x264_set_default_mpeg2(upipe_fenc->last_inner))
    }
    if (upipe_fenc->preset != NULL || upipe_fenc->tune != NULL)
        upipe_x264_set_default_preset(upipe_fenc->last_inner,
                                      upipe_fenc->preset, upipe_fenc->tune);
    if (upipe_fenc->profile != NULL)
        upipe_x264_set_profile(upipe_fenc->last_inner, upipe_fenc->profile);
    if (upipe_fenc->sc_latency != UINT64_MAX)
        upipe_x264_set_sc_latency(upipe_fenc->last_inner,
                                  upipe_fenc->sc_latency);

    if (upipe_fenc->options != NULL && upipe_fenc->options->udict != NULL) {
        const char *key = NULL;
        enum udict_type type = UDICT_TYPE_END;
        while (ubase_check(udict_iterate(upipe_fenc->options->udict, &key,
                                         &type)) && type != UDICT_TYPE_END) {
            const char *value;
            if (key == NULL ||
                !ubase_check(udict_get_string(upipe_fenc->options->udict,
                                              &value, type, key)))
                continue;
            if (!ubase_check(upipe_set_option(upipe_fenc->last_inner,
                                              key, value)))
                upipe_warn_va(upipe, "option %s=%s invalid", key, value);
        }
    }

    if (ubase_check(upipe_set_flow_def(upipe_fenc->last_inner, flow_def)))
        return UBASE_ERR_NONE;
    upipe_fenc_store_first_inner(upipe, NULL);
    upipe_fenc_store_last_inner(upipe, NULL);
    return UBASE_ERR_INVALID;
}

/** @internal @This gets the value of an option.
 *
 * @param upipe description structure of the pipe
 * @param key name of the option
 * @param value of the option, or NULL to delete it
 * @return an error code
 */
static int upipe_fenc_get_option(struct upipe *upipe,
                                 const char *key, const char **value_p)
{
    struct upipe_fenc *upipe_fenc = upipe_fenc_from_upipe(upipe);
    assert(key != NULL);

    if (upipe_fenc->options == NULL)
        return UBASE_ERR_INVALID;

    return udict_get_string(upipe_fenc->options->udict, value_p,
                            UDICT_TYPE_STRING, key);
}

/** @internal @This sets the value of an option.
 *
 * @param upipe description structure of the pipe
 * @param key name of the option
 * @param value of the option, or NULL to delete it
 * @return an error code
 */
static int upipe_fenc_set_option(struct upipe *upipe,
                                 const char *key, const char *value)
{
    struct upipe_fenc *upipe_fenc = upipe_fenc_from_upipe(upipe);
    assert(key != NULL);

    if (upipe_fenc->options == NULL)
        return UBASE_ERR_ALLOC;

    if (upipe_fenc->last_inner != NULL) {
        UBASE_RETURN(upipe_set_option(upipe_fenc->last_inner, key, value))
    }
    if (value != NULL)
        return udict_set_string(upipe_fenc->options->udict, value,
                                UDICT_TYPE_STRING, key);
    else
        udict_delete(upipe_fenc->options->udict, UDICT_TYPE_STRING, key);
    return UBASE_ERR_NONE;
}

/** @internal @This resets parameters to mpeg2 default.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_fenc_set_default_mpeg2(struct upipe *upipe)
{
    struct upipe_fenc *upipe_fenc = upipe_fenc_from_upipe(upipe);

    upipe_fenc->x262 = true;

    if (upipe_fenc->last_inner != NULL) {
        UBASE_RETURN(upipe_x264_set_default_mpeg2(upipe_fenc->last_inner))
    }
    return UBASE_ERR_NONE;
}

/** @internal @This sets the x264 default preset.
 *
 * @param upipe description structure of the pipe
 * @param preset preset
 * @param tune tune
 * @return an error code
 */
static int upipe_fenc_set_default_preset(struct upipe *upipe,
                                         const char *preset, const char *tune)
{
    struct upipe_fenc *upipe_fenc = upipe_fenc_from_upipe(upipe);

    ubase_clean_str(&upipe_fenc->preset);
    ubase_clean_str(&upipe_fenc->tune);
    if (preset != NULL)
        upipe_fenc->preset = strdup(preset);
    if (tune != NULL)
        upipe_fenc->tune = strdup(tune);

    if (upipe_fenc->last_inner != NULL) {
        UBASE_RETURN(upipe_x264_set_default_preset(upipe_fenc->last_inner,
                                                   preset, tune))
    }
    return UBASE_ERR_NONE;
}

/** @internal @This sets the x264 profile.
 *
 * @param upipe description structure of the pipe
 * @param profile profile
 * @return an error code
 */
static int upipe_fenc_set_profile(struct upipe *upipe, const char *profile)
{
    struct upipe_fenc *upipe_fenc = upipe_fenc_from_upipe(upipe);

    ubase_clean_str(&upipe_fenc->profile);
    if (profile != NULL)
        upipe_fenc->profile = strdup(profile);

    if (upipe_fenc->last_inner != NULL) {
        UBASE_RETURN(upipe_x264_set_profile(upipe_fenc->last_inner, profile))
    }
    return UBASE_ERR_NONE;
}

/** @internal @This sets the x264 speed control latency.
 *
 * @param upipe description structure of the pipe
 * @param sc_latency latency
 * @return an error code
 */
static int upipe_fenc_set_sc_latency(struct upipe *upipe, uint64_t sc_latency)
{
    struct upipe_fenc *upipe_fenc = upipe_fenc_from_upipe(upipe);

    upipe_fenc->sc_latency = sc_latency;

    if (upipe_fenc->last_inner != NULL) {
        UBASE_RETURN(upipe_x264_set_sc_latency(upipe_fenc->last_inner, 
                                               sc_latency))
    }
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a fenc pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_fenc_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_GET_OPTION: {
            const char *key = va_arg(args, const char *);
            const char **value_p = va_arg(args, const char **);
            return upipe_fenc_get_option(upipe, key, value_p);
        }
        case UPIPE_SET_OPTION: {
            const char *key = va_arg(args, const char *);
            const char *value = va_arg(args, const char *);
            return upipe_fenc_set_option(upipe, key, value);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_fenc_set_flow_def(upipe, flow_def);
        }

        case UPIPE_X264_SET_DEFAULT_MPEG2: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_X264_SIGNATURE)
            return upipe_fenc_set_default_mpeg2(upipe);
        }
        case UPIPE_X264_SET_DEFAULT_PRESET: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_X264_SIGNATURE)
            const char *preset = va_arg(args, const char *);
            const char *tune = va_arg(args, const char *);
            return upipe_fenc_set_default_preset(upipe, preset, tune);
        }
        case UPIPE_X264_SET_PROFILE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_X264_SIGNATURE)
            const char *profile = va_arg(args, const char *);
            return upipe_fenc_set_profile(upipe, profile);
        }
        case UPIPE_X264_SET_SC_LATENCY: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_X264_SIGNATURE)
            uint64_t sc_latency = va_arg(args, uint64_t);
            return upipe_fenc_set_sc_latency(upipe, sc_latency);
        }

        default:
            break;
    }

    int err = upipe_fenc_control_bin_input(upipe, command, args);
    if (err == UBASE_ERR_UNHANDLED)
        return upipe_fenc_control_bin_output(upipe, command, args);
    return err;
}

/** @This frees a upipe.
 *
 * @param urefcount_real pointer to urefcount_real structure
 */
static void upipe_fenc_free(struct urefcount *urefcount_real)
{
    struct upipe_fenc *upipe_fenc =
        upipe_fenc_from_urefcount_real(urefcount_real);
    struct upipe *upipe = upipe_fenc_to_upipe(upipe_fenc);
    upipe_throw_dead(upipe);
    uref_free(upipe_fenc->flow_def_input);
    uref_free(upipe_fenc->options);
    free(upipe_fenc->preset);
    free(upipe_fenc->tune);
    free(upipe_fenc->profile);
    uprobe_clean(&upipe_fenc->last_inner_probe);
    upipe_fenc_clean_uref_mgr(upipe);
    urefcount_clean(urefcount_real);
    upipe_fenc_clean_urefcount(upipe);
    upipe_fenc_free_flow(upipe);
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_fenc_no_ref(struct upipe *upipe)
{
    struct upipe_fenc *upipe_fenc = upipe_fenc_from_upipe(upipe);
    upipe_fenc_clean_bin_input(upipe);
    upipe_fenc_clean_bin_output(upipe);
    urefcount_release(upipe_fenc_to_urefcount_real(upipe_fenc));
}

/** @This frees a upipe manager.
 *
 * @param urefcount pointer to urefcount structure
 */
static void upipe_fenc_mgr_free(struct urefcount *urefcount)
{
    struct upipe_fenc_mgr *fenc_mgr = upipe_fenc_mgr_from_urefcount(urefcount);
    upipe_mgr_release(fenc_mgr->avcenc_mgr);
    upipe_mgr_release(fenc_mgr->x264_mgr);

    urefcount_clean(urefcount);
    free(fenc_mgr);
}

/** @This processes control commands on a fenc manager.
 *
 * @param mgr pointer to manager
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_fenc_mgr_control(struct upipe_mgr *mgr,
                                  int command, va_list args)
{
    struct upipe_fenc_mgr *fenc_mgr = upipe_fenc_mgr_from_upipe_mgr(mgr);

    switch (command) {
#define GET_SET_MGR(name, NAME)                                             \
        case UPIPE_FENC_MGR_GET_##NAME##_MGR: {                             \
            UBASE_SIGNATURE_CHECK(args, UPIPE_FENC_SIGNATURE)               \
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);       \
            *p = fenc_mgr->name##_mgr;                                      \
            return UBASE_ERR_NONE;                                          \
        }                                                                   \
        case UPIPE_FENC_MGR_SET_##NAME##_MGR: {                             \
            UBASE_SIGNATURE_CHECK(args, UPIPE_FENC_SIGNATURE)               \
            if (!urefcount_single(&fenc_mgr->urefcount))                    \
                return UBASE_ERR_BUSY;                                      \
            struct upipe_mgr *m = va_arg(args, struct upipe_mgr *);         \
            upipe_mgr_release(fenc_mgr->name##_mgr);                        \
            fenc_mgr->name##_mgr = upipe_mgr_use(m);                        \
            return UBASE_ERR_NONE;                                          \
        }

        GET_SET_MGR(avcenc, AVCENC)
        GET_SET_MGR(x264, X264)
#undef GET_SET_MGR

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This returns the management structure for all fenc pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_fenc_mgr_alloc(void)
{
    struct upipe_fenc_mgr *fenc_mgr = malloc(sizeof(struct upipe_fenc_mgr));
    if (unlikely(fenc_mgr == NULL))
        return NULL;

    fenc_mgr->avcenc_mgr = NULL;
    fenc_mgr->x264_mgr = NULL;

    urefcount_init(upipe_fenc_mgr_to_urefcount(fenc_mgr),
                   upipe_fenc_mgr_free);
    fenc_mgr->mgr.refcount = upipe_fenc_mgr_to_urefcount(fenc_mgr);
    fenc_mgr->mgr.signature = UPIPE_FENC_SIGNATURE;
    fenc_mgr->mgr.upipe_alloc = upipe_fenc_alloc;
    fenc_mgr->mgr.upipe_input = upipe_fenc_bin_input;
    fenc_mgr->mgr.upipe_control = upipe_fenc_control;
    fenc_mgr->mgr.upipe_mgr_control = upipe_fenc_mgr_control;
    return upipe_fenc_mgr_to_upipe_mgr(fenc_mgr);
}

