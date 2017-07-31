/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
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

#include <upipe-ts/upipe_ts_demux.h>
#include <upipe-hls/uref_hls.h>
#include <upipe-hls/upipe_hls_void.h>
#include <upipe-hls/upipe_hls_video.h>
#include <upipe-hls/upipe_hls_audio.h>
#include <upipe-hls/upipe_hls_variant.h>

#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_bin_output.h>
#include <upipe/upipe_helper_inner.h>
#include <upipe/upipe_helper_uprobe.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_urefcount_real.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe.h>

#include <upipe/uprobe_prefix.h>

#include <upipe/uref_m3u.h>
#include <upipe/uref_uri.h>
#include <upipe/uref_dump.h>

#include <libgen.h>

static inline int upipe_hls_variant_make_uri(struct uref *parent,
                                             const char *item,
                                             char **uri)
{
    struct uuri uuri;
    if (ubase_check(uuri_from_str(&uuri, item)))
        return uuri_to_str(&uuri, uri);

    /* a relative uri, add path */
    UBASE_RETURN(uref_uri_get(parent, &uuri));
    uuri.query = ustring_null();
    uuri.fragment = ustring_null();

    char path[uuri.path.len + 1];
    UBASE_RETURN(ustring_cpy(uuri.path, path, sizeof (path)));
    char *root = dirname(path);

    char new_path[strlen(root) + 1 + strlen(item) + 1];
    sprintf(new_path, "%s%s%s", root, *item == '/' ? "" : "/", item);
    uuri.path = ustring_from_str(new_path);
    return uuri_to_str(&uuri, uri);
}

/** @internal @This is the private context for a sub variant pipe. */
struct upipe_hls_variant_sub {
    /** public pipe structure */
    struct upipe upipe;
    /** urefcount management structure */
    struct urefcount urefcount;
    /** real urefcount management structure */
    struct urefcount urefcount_real;
    /** flow format */
    struct uref *flow_def;
    /** uchain for super pipe */
    struct uchain uchain;
    /** list of request */
    struct uchain requests;
    /** last inner pipe */
    struct upipe *last_inner;
    /** last inner probe */
    struct uprobe probe_last_inner;
    /** output pipe */
    struct upipe *output;
};

UPIPE_HELPER_UPIPE(upipe_hls_variant_sub, upipe,
                   UPIPE_HLS_VARIANT_SUB_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_hls_variant_sub, urefcount,
                       upipe_hls_variant_sub_no_ref);
UPIPE_HELPER_UREFCOUNT_REAL(upipe_hls_variant_sub, urefcount_real,
                            upipe_hls_variant_sub_free);
UPIPE_HELPER_FLOW(upipe_hls_variant_sub, NULL);
UPIPE_HELPER_INNER(upipe_hls_variant_sub, last_inner);
UPIPE_HELPER_UPROBE(upipe_hls_variant_sub, urefcount_real,
                    probe_last_inner, NULL);
UPIPE_HELPER_BIN_OUTPUT(upipe_hls_variant_sub, last_inner, output, requests);

struct upipe_hls_variant {
    /** urefcount management structure */
    struct urefcount urefcount;
    /** public upipe_structure */
    struct upipe upipe;
    /** sub pipe manager */
    struct upipe_mgr sub_mgr;
    /** last inner request list */
    struct uchain last_inner_requests;
    /** list of sub pipes */
    struct uchain subs;
    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** upump */
    struct upump *upump;
    /** playing variant */
    struct uref *flow_def;
    /** list of rendition */
    struct uchain renditions;
    /** last flow id */
    uint64_t flow_id;
};

static int upipe_hls_variant_check(struct upipe *upipe);

UPIPE_HELPER_UPIPE(upipe_hls_variant, upipe, UPIPE_HLS_VARIANT_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_hls_variant, urefcount, upipe_hls_variant_free);
UPIPE_HELPER_FLOW(upipe_hls_variant, NULL);
UPIPE_HELPER_SUBPIPE(upipe_hls_variant, upipe_hls_variant_sub,
                     pipe, sub_mgr, subs, uchain);
UPIPE_HELPER_UPUMP_MGR(upipe_hls_variant, upump_mgr);
UPIPE_HELPER_UPUMP(upipe_hls_variant, upump, upump_mgr);

/** @internal @This allocates a hls sub variant pipe.
 *
 * @param mgr management structure for this pipe type
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return a pointer to the allocated pipe
 */
static struct upipe *upipe_hls_variant_sub_alloc(struct upipe_mgr *mgr,
                                                 struct uprobe *uprobe,
                                                 uint32_t signature,
                                                 va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe =
        upipe_hls_variant_sub_alloc_flow(mgr, uprobe, signature,
                                         args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    uint64_t id;
    if (unlikely(!ubase_check(uref_flow_get_id(flow_def, &id)))) {
        uref_free(flow_def);
        upipe_hls_variant_sub_free_flow(upipe);
        uprobe_err(uprobe, NULL, "no flow id");
        return NULL;
    }

    upipe_hls_variant_sub_init_urefcount(upipe);
    upipe_hls_variant_sub_init_urefcount_real(upipe);
    upipe_hls_variant_sub_init_sub(upipe);
    upipe_hls_variant_sub_init_probe_last_inner(upipe);
    upipe_hls_variant_sub_init_bin_output(upipe);

    struct upipe_hls_variant_sub *upipe_hls_variant_sub =
        upipe_hls_variant_sub_from_upipe(upipe);
    upipe_hls_variant_sub->flow_def = flow_def;

    upipe_throw_ready(upipe);

    struct upipe_mgr *upipe_mgr = NULL;
    struct upipe *last_inner = NULL;
    const char *name;
    if (ubase_check(uref_flow_match_def(flow_def, "void."))) {
        upipe_mgr = upipe_hls_void_mgr_alloc();
        name = "void";
    }
    else if (ubase_check(uref_flow_match_def(flow_def, "sound."))) {
        upipe_mgr = upipe_hls_audio_mgr_alloc();
        name = "audio";
    }
    else if (ubase_check(uref_flow_match_def(flow_def, "pic."))) {
        upipe_mgr = upipe_hls_video_mgr_alloc();
        name = "video";
    }
    else {
        const char *def = "(none)";
        uref_flow_get_def(flow_def, &def);
        upipe_warn_va(upipe, "unhandle flow def %s", def);
    }

    if (upipe_mgr == NULL) {
        upipe_release(upipe);
        return NULL;
    }

    last_inner = upipe_void_alloc(
        upipe_mgr,
        uprobe_pfx_alloc(
            uprobe_use(&upipe_hls_variant_sub->probe_last_inner),
            UPROBE_LOG_VERBOSE, name));
    upipe_mgr_release(upipe_mgr);
    if (last_inner == NULL) {
        upipe_err(upipe, "fail to allocate sub pipe");
        upipe_release(upipe);
        return NULL;
    }

    struct upipe_hls_variant *upipe_hls_variant =
        upipe_hls_variant_from_sub_mgr(mgr);

    const char *item_uri = NULL;
    char *uri = NULL;
    uref_hls_get_uri(flow_def, &item_uri);

    if (unlikely(!ubase_check(upipe_hls_variant_make_uri(
                    upipe_hls_variant->flow_def, item_uri, &uri))) || !uri) {
        upipe_warn_va(upipe, "fail to make uri with %s", item_uri);
        upipe_release(last_inner);
        upipe_release(upipe);
        return NULL;
    }

    if (unlikely(!ubase_check(upipe_set_uri(last_inner, uri)))) {
        upipe_warn_va(upipe, "fail to set uri %s", uri);
        free(uri);
        upipe_release(last_inner);
        upipe_release(upipe);
        return NULL;
    }
    free(uri);

    upipe_hls_variant_sub_store_bin_output(upipe, last_inner);

    return upipe;
}

/** @internal @This frees a sub variant pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_hls_variant_sub_free(struct upipe *upipe)
{
    struct upipe_hls_variant_sub *upipe_hls_variant_sub =
        upipe_hls_variant_sub_from_upipe(upipe);

    upipe_throw_dead(upipe);

    uref_free(upipe_hls_variant_sub->flow_def);
    upipe_hls_variant_sub_clean_probe_last_inner(upipe);
    upipe_hls_variant_sub_clean_bin_output(upipe);
    upipe_hls_variant_sub_clean_sub(upipe);
    upipe_hls_variant_sub_clean_urefcount(upipe);
    upipe_hls_variant_sub_clean_urefcount_real(upipe);
    upipe_hls_variant_sub_free_flow(upipe);
}

/** @internal @This is called when there is no external reference to the pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_hls_variant_sub_no_ref(struct upipe *upipe)
{
    upipe_hls_variant_sub_clean_last_inner(upipe);
    upipe_hls_variant_sub_release_urefcount_real(upipe);
}

/** @internal @This dispatches the control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args optional arguments
 * @return an error code
 */
static int upipe_hls_variant_sub_control(struct upipe *upipe,
                                         int command, va_list args)
{
    UBASE_HANDLED_RETURN(
        upipe_hls_variant_sub_control_super(upipe, command, args));
    switch (command) {
    case UPIPE_BIN_GET_FIRST_INNER: {
        struct upipe_hls_variant_sub *upipe_hls_variant_sub =
            upipe_hls_variant_sub_from_upipe(upipe);
        struct upipe **p = va_arg(args, struct upipe **);
        *p = upipe_hls_variant_sub->last_inner;
        return (*p != NULL) ? UBASE_ERR_NONE : UBASE_ERR_UNHANDLED;
    }
    }
    return upipe_hls_variant_sub_control_bin_output(upipe, command, args);
}

/** @internal @This initializes the sub pipe manager.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_hls_variant_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_hls_variant *upipe_hls_variant =
        upipe_hls_variant_from_upipe(upipe);

    memset(&upipe_hls_variant->sub_mgr, 0, sizeof (upipe_hls_variant->sub_mgr));
    upipe_hls_variant->sub_mgr.signature = UPIPE_HLS_VARIANT_SUB_SIGNATURE;
    upipe_hls_variant->sub_mgr.refcount = upipe->refcount;
    upipe_hls_variant->sub_mgr.upipe_alloc = upipe_hls_variant_sub_alloc;
    upipe_hls_variant->sub_mgr.upipe_control = upipe_hls_variant_sub_control;
}

/** @internal @This allocates a hls variant pipe.
 *
 * @param mgr management structure for this pipe type
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_hls_variant_alloc(struct upipe_mgr *mgr,
                                             struct uprobe *uprobe,
                                             uint32_t signature,
                                             va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe =
        upipe_hls_variant_alloc_flow(mgr, uprobe, signature, args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_hls_variant_init_urefcount(upipe);
    upipe_hls_variant_init_sub_mgr(upipe);
    upipe_hls_variant_init_sub_pipes(upipe);
    upipe_hls_variant_init_upump_mgr(upipe);
    upipe_hls_variant_init_upump(upipe);

    struct upipe_hls_variant *upipe_hls_variant =
        upipe_hls_variant_from_upipe(upipe);
    upipe_hls_variant->flow_def = flow_def;
    upipe_hls_variant->flow_id = 0;
    ulist_init(&upipe_hls_variant->renditions);

    upipe_throw_ready(upipe);

    upipe_hls_variant_check(upipe);

    return upipe;
}

/** @internal @This frees a hls variant pipe.
 *
 * @param urefcount pointer to real urefcount structure
 */
static void upipe_hls_variant_free(struct upipe *upipe)
{
    struct upipe_hls_variant *upipe_hls_variant =
        upipe_hls_variant_from_upipe(upipe);

    upipe_throw_dead(upipe);

    struct uchain *uchain;
    while ((uchain = ulist_pop(&upipe_hls_variant->renditions)) != NULL)
        uref_free(uref_from_uchain(uchain));
    uref_free(upipe_hls_variant->flow_def);
    upipe_hls_variant_clean_upump(upipe);
    upipe_hls_variant_clean_upump_mgr(upipe);
    upipe_hls_variant_clean_sub_pipes(upipe);
    upipe_hls_variant_clean_urefcount(upipe);
    upipe_hls_variant_free_flow(upipe);
}

/** @internal @This starts the hls variant.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_hls_variant_split(struct upipe *upipe)
{
    struct upipe_hls_variant *upipe_hls_variant =
        upipe_hls_variant_from_upipe(upipe);
    struct uref *flow_def = upipe_hls_variant->flow_def;

    if (unlikely(flow_def == NULL))
        return UBASE_ERR_NONE;

    const char *uri;
    UBASE_RETURN(uref_m3u_get_uri(flow_def, &uri));

    struct uref *main_rend = uref_sibling_alloc_control(flow_def);
    UBASE_ALLOC_RETURN(main_rend);
    uref_flow_set_def(main_rend, "void.");
    uref_flow_set_id(main_rend, upipe_hls_variant->flow_id++);
    uref_hls_set_uri(main_rend, uri);
    ulist_add(&upipe_hls_variant->renditions, uref_to_uchain(main_rend));

    uint8_t renditions = 0;
    uref_hls_get_renditions(flow_def, &renditions);
    for (uint8_t i = 0; i < renditions; i++) {
        const char *type, *name, *uri;
        if (!ubase_check(uref_hls_rendition_get_type(flow_def, &type, i)) ||
            !ubase_check(uref_hls_rendition_get_name(flow_def, &name, i)) ||
            !ubase_check(uref_hls_rendition_get_uri(flow_def, &uri, i)))
            continue;

        struct uref *rend = uref_sibling_alloc_control(flow_def);
        UBASE_ALLOC_RETURN(rend);
        if (!strcmp(type, "AUDIO"))
            uref_flow_set_def(rend, "sound.");
        else if (!strcmp(type, "VIDEO"))
            uref_flow_set_def(rend, "pic.");
        else {
            upipe_warn_va(upipe, "unknown type %s", type);
            uref_free(rend);
            continue;
        }

        uref_flow_set_id(rend, upipe_hls_variant->flow_id++);
        uref_hls_set_type(rend, type);
        uref_hls_set_name(rend, name);
        uref_hls_set_uri(rend, uri);

        if (ubase_check(uref_hls_rendition_get_default(flow_def, i)))
            uref_hls_set_default(rend);
        if (ubase_check(uref_hls_rendition_get_autoselect(flow_def, i)))
            uref_hls_set_autoselect(rend);

        ulist_add(&upipe_hls_variant->renditions, uref_to_uchain(rend));
    }

    return upipe_split_throw_update(upipe);
}

/** @internal @This iterates over renditions.
 *
 * @param upipe description structure of the pipe
 * @param uref_p pointer filled with the next rendition or NULL
 * @return an error code
 */
static int upipe_hls_variant_split_iterate(struct upipe *upipe,
                                           struct uref **uref_p)
{
    struct upipe_hls_variant *upipe_hls_variant =
        upipe_hls_variant_from_upipe(upipe);

    if (unlikely(uref_p == NULL))
        return UBASE_ERR_INVALID;

    struct uchain *uchain;
    if (unlikely(*uref_p == NULL))
        uchain = &upipe_hls_variant->renditions;
    else
        uchain = uref_to_uchain(*uref_p);

    if (ulist_is_last(&upipe_hls_variant->renditions, uchain)) {
        *uref_p = NULL;
        return UBASE_ERR_NONE;
    }
    *uref_p = uref_from_uchain(uchain->next);
    return UBASE_ERR_NONE;
}

/** @internal @This dispatches the controls.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args optional arguments
 * @return an error code
 */
static int _upipe_hls_variant_control(struct upipe *upipe,
                                      int command,
                                      va_list args)
{
    UBASE_HANDLED_RETURN(upipe_hls_variant_control_pipes(upipe, command, args));

    switch (command) {
    case UPIPE_ATTACH_UPUMP_MGR:
        upipe_hls_variant_set_upump(upipe, NULL);
        return upipe_hls_variant_attach_upump_mgr(upipe);

    case UPIPE_SPLIT_ITERATE: {
        struct uref **uref_p = va_arg(args, struct uref **);
        return upipe_hls_variant_split_iterate(upipe, uref_p);
    }
    }

    return UBASE_ERR_UNHANDLED;
}

/** @internal @This is called when the upump is triggered.
 *
 * @param upump description structure of the pump
 */
static void upipe_hls_variant_cb(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    upipe_hls_variant_split(upipe);
    upump_stop(upump);
}

/** @internal @This allocates the upump if needed.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_hls_variant_check(struct upipe *upipe)
{
    struct upipe_hls_variant *upipe_hls_variant =
        upipe_hls_variant_from_upipe(upipe);

    int ret = upipe_hls_variant_check_upump_mgr(upipe);
    if (unlikely(!ubase_check(ret)))
        return ret;

    if (upipe_hls_variant->upump == NULL) {
        struct upump *upump =
            upump_alloc_idler(upipe_hls_variant->upump_mgr,
                              upipe_hls_variant_cb, upipe,
                              upipe->refcount);
        if (unlikely(upump == NULL))
            return UBASE_ERR_UPUMP;

        upipe_hls_variant_set_upump(upipe, upump);
        upump_start(upump);
    }

    return UBASE_ERR_NONE;
}

/** @internal @This dispatches the controls and allocates the upump if needed.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args optional arguments
 * @return an error code
 */
static int upipe_hls_variant_control(struct upipe *upipe,
                                      int command,
                                      va_list args)
{
    int ret = _upipe_hls_variant_control(upipe, command, args);
    if (unlikely(!ubase_check(ret)))
        return ret;
    return upipe_hls_variant_check(upipe);
}

/** @internal @This is the static structure for hls variant manager. */
static struct upipe_mgr upipe_hls_variant_mgr = {
    .refcount = NULL,
    .signature = UPIPE_HLS_VARIANT_SIGNATURE,
    .upipe_command_str = upipe_hls_variant_command_str,
    .upipe_alloc = upipe_hls_variant_alloc,
    .upipe_control = upipe_hls_variant_control,
};

/** @This returns the management structure for hls variant pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_hls_variant_mgr_alloc(void)
{
    return &upipe_hls_variant_mgr;
}
