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

#include <upipe-hls/upipe_hls_master.h>
#include <upipe-hls/upipe_hls_variant.h>

#include <upipe-hls/uref_hls.h>

#include <upipe/upipe_helper_subpipe.h>
#include <upipe/upipe_helper_bin_output.h>
#include <upipe/upipe_helper_inner.h>
#include <upipe/upipe_helper_uprobe.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_urefcount_real.h>
#include <upipe/upipe_helper_upipe.h>

#include <upipe/uref_m3u_master.h>
#include <upipe/uref_m3u.h>
#include <upipe/uref_uri.h>
#include <upipe/uref_block.h>

#include <upipe/uprobe_prefix.h>

#define EXPECTED_FLOW_DEF       "block.m3u.master."

/** @internal @This is the private context of a sub master pipe. */
struct upipe_hls_master_sub {
    /** public upipe structure */
    struct upipe upipe;
    /** urefcount management structure */
    struct urefcount urefcount;
    /** real urefcount management structure */
    struct urefcount urefcount_real;
    /** link to the super pipe */
    struct uchain uchain;
    /** list of requests */
    struct uchain requests;
    /** last inner probe */
    struct uprobe last_inner_probe;
    /** last inner pipe */
    struct upipe *last_inner;
    /** output pipe */
    struct upipe *output;
    /** flow definition */
    struct uref *flow_def;
};

UPIPE_HELPER_UPIPE(upipe_hls_master_sub, upipe, UPIPE_HLS_MASTER_SUB_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_hls_master_sub, urefcount,
                       upipe_hls_master_sub_no_ref);
UPIPE_HELPER_UREFCOUNT_REAL(upipe_hls_master_sub, urefcount_real,
                            upipe_hls_master_sub_free);
UPIPE_HELPER_FLOW(upipe_hls_master_sub, NULL);
UPIPE_HELPER_UPROBE(upipe_hls_master_sub, urefcount_real,
                    last_inner_probe, NULL);
UPIPE_HELPER_INNER(upipe_hls_master_sub, last_inner);
UPIPE_HELPER_BIN_OUTPUT(upipe_hls_master_sub, last_inner, output, requests);

/** @internal @This is the private context of a master pipe. */
struct upipe_hls_master {
    /** urefcount management structure */
    struct urefcount urefcount;
    /** public upipe structure */
    struct upipe upipe;
    /** input flow format */
    struct uref *flow_def;
    /** variant list */
    struct uchain items;
    /** list of rendition */
    struct uchain renditions;
    /** sub pipe manager */
    struct upipe_mgr sub_mgr;
    /** list of sub pipes */
    struct uchain subs;
    /** variant id */
    uint64_t id;
};

UPIPE_HELPER_UPIPE(upipe_hls_master, upipe, UPIPE_HLS_MASTER_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_hls_master, urefcount, upipe_hls_master_free)
UPIPE_HELPER_VOID(upipe_hls_master)
UPIPE_HELPER_SUBPIPE(upipe_hls_master, upipe_hls_master_sub,
                     pipe, sub_mgr, subs, uchain);

/** @internal @This allocates a sub master pipe.
 *
 * @param mgr management structure for this pipe type
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_hls_master_sub_alloc(struct upipe_mgr *mgr,
                                                struct uprobe *uprobe,
                                                uint32_t signature,
                                                va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_hls_master_sub_alloc_flow(
        mgr, uprobe, signature, args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    uint64_t id;
    if (unlikely(!ubase_check(uref_flow_get_id(flow_def, &id)))) {
        uref_free(flow_def);
        upipe_hls_master_sub_free_flow(upipe);
        return NULL;
    }

    upipe_hls_master_sub_init_urefcount(upipe);
    upipe_hls_master_sub_init_urefcount_real(upipe);
    upipe_hls_master_sub_init_last_inner_probe(upipe);
    upipe_hls_master_sub_init_bin_output(upipe);
    upipe_hls_master_sub_init_sub(upipe);

    struct upipe_hls_master_sub *upipe_hls_master_sub =
        upipe_hls_master_sub_from_upipe(upipe);
    upipe_hls_master_sub->flow_def = flow_def;

    upipe_throw_ready(upipe);

    struct upipe_mgr *upipe_hls_variant_mgr = upipe_hls_variant_mgr_alloc();
    struct upipe *inner = NULL;
    if (likely(upipe_hls_variant_mgr != NULL)) {
        inner = upipe_flow_alloc(
            upipe_hls_variant_mgr,
            uprobe_pfx_alloc_va(
                uprobe_use(&upipe_hls_master_sub->last_inner_probe),
                UPROBE_LOG_VERBOSE, "flow %"PRIu64, id),
            flow_def);
        upipe_mgr_release(upipe_hls_variant_mgr);
    }
    upipe_hls_master_sub_store_bin_output(upipe, inner);

    return upipe;
}

/** @internal @This frees a sub master pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_hls_master_sub_free(struct upipe *upipe)
{
    struct upipe_hls_master_sub *upipe_hls_master_sub =
        upipe_hls_master_sub_from_upipe(upipe);

    upipe_throw_dead(upipe);

    uref_free(upipe_hls_master_sub->flow_def);
    upipe_hls_master_sub_clean_sub(upipe);
    upipe_hls_master_sub_clean_bin_output(upipe);
    upipe_hls_master_sub_clean_last_inner_probe(upipe);
    upipe_hls_master_sub_clean_urefcount(upipe);
    upipe_hls_master_sub_clean_urefcount_real(upipe);
    upipe_hls_master_sub_free_flow(upipe);
}

/** @internal @This is called when there is no external reference to the pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_hls_master_sub_no_ref(struct upipe *upipe)
{
    upipe_hls_master_sub_clean_last_inner(upipe);
    upipe_hls_master_sub_release_urefcount_real(upipe);
}

/** @internal @This dispatches commands to the pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to dispatch
 * @param args optional arguments
 * @return an error code
 */
static int upipe_hls_master_sub_control(struct upipe *upipe,
                                        int command,
                                        va_list args)
{
    UBASE_HANDLED_RETURN(
        upipe_hls_master_sub_control_super(upipe, command, args));
    return upipe_hls_master_sub_control_bin_output(upipe, command, args);
}

/** @internal @This initializes the sub pipe manager.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_hls_master_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_hls_master *upipe_hls_master =
        upipe_hls_master_from_upipe(upipe);
    memset(&upipe_hls_master->sub_mgr, 0, sizeof (upipe_hls_master->sub_mgr));
    upipe_hls_master->sub_mgr.refcount = &upipe_hls_master->urefcount;
    upipe_hls_master->sub_mgr.signature = UPIPE_HLS_MASTER_SUB_SIGNATURE;
    upipe_hls_master->sub_mgr.upipe_alloc = upipe_hls_master_sub_alloc;
    upipe_hls_master->sub_mgr.upipe_control = upipe_hls_master_sub_control;
}

/** @internal @This allocates a master pipe.
 *
 * @param mgr management structure for this pipe type
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_hls_master_alloc(struct upipe_mgr *mgr,
                                             struct uprobe *uprobe,
                                             uint32_t signature,
                                             va_list args)
{
    struct upipe *upipe =
        upipe_hls_master_alloc_void(mgr, uprobe, signature, args);
    if (!upipe)
        return NULL;

    upipe_hls_master_init_urefcount(upipe);
    upipe_hls_master_init_sub_pipes(upipe);
    upipe_hls_master_init_sub_mgr(upipe);

    struct upipe_hls_master *upipe_hls_master =
        upipe_hls_master_from_upipe(upipe);
    upipe_hls_master->flow_def = NULL;
    ulist_init(&upipe_hls_master->items);
    ulist_init(&upipe_hls_master->renditions);
    upipe_hls_master->id = 0;

    upipe_throw_ready(upipe);

    return upipe;
}

/** @internal @This flushes the master playlist.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_hls_master_flush(struct upipe *upipe)
{
    struct upipe_hls_master *upipe_hls_master =
        upipe_hls_master_from_upipe(upipe);

    struct uchain *uchain;
    while ((uchain = ulist_pop(&upipe_hls_master->items)) != NULL)
        uref_free(uref_from_uchain(uchain));
    while ((uchain = ulist_pop(&upipe_hls_master->renditions)) != NULL)
        uref_free(uref_from_uchain(uchain));
    upipe_hls_master->id = 0;
}

/** @internal @This frees a master pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_hls_master_free(struct upipe *upipe)
{
    struct upipe_hls_master *upipe_hls_master =
        upipe_hls_master_from_upipe(upipe);

    upipe_throw_dead(upipe);

    uref_free(upipe_hls_master->flow_def);
    upipe_hls_master_flush(upipe);
    upipe_hls_master_clean_sub_pipes(upipe);
    upipe_hls_master_clean_urefcount(upipe);
    upipe_hls_master_free_void(upipe);
}

/** @internal @This adds the renditions to a variant.
 *
 * @param upipe description structure of the pipe
 * @param uref the variant
 * @param type type of the rendition
 * @param id id of the rendition
 */
static void upipe_hls_master_set_renditions(struct upipe *upipe,
                                            struct uref *uref,
                                            const char *type,
                                            const char *id)
{
    struct upipe_hls_master *upipe_hls_master =
        upipe_hls_master_from_upipe(upipe);
    uint8_t count = 0;

    struct uchain *uchain;
    /** iterate throw renditions. */
    ulist_foreach(&upipe_hls_master->renditions, uchain) {
        struct uref *rend = uref_from_uchain(uchain);

        /** check if the variant own this rendition. */
        const char *rend_id, *rend_type;
        if (!ubase_check(uref_m3u_master_get_media_group(rend, &rend_id)) ||
            !ubase_check(uref_m3u_master_get_media_type(rend, &rend_type)) ||
            strcmp(rend_id, id) ||
            strcmp(rend_type, type))
            continue;

        /** check if the rendition is valid. */
        const char *name, *uri;
        if (!ubase_check(uref_m3u_master_get_media_name(rend, &name)) ||
            !ubase_check(uref_m3u_get_uri(rend, &uri)))
            continue;

        /** add the rendition to the variant. */
        uref_hls_rendition_set_type(uref, type, count);
        uref_hls_rendition_set_name(uref, name, count);
        uref_hls_rendition_set_uri(uref, uri, count);
        if (ubase_check(uref_m3u_master_get_media_default(rend)))
            uref_hls_rendition_set_default(uref, count);
        if (ubase_check(uref_m3u_master_get_media_autoselect(rend)))
            uref_hls_rendition_set_autoselect(uref, count);
        count++;
    }
    /** the the rendition count. */
    uref_hls_set_renditions(uref, count);
}

/** @internal @This is called when there is input data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref carrying the data
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_hls_master_input(struct upipe *upipe,
                                   struct uref *uref,
                                   struct upump **upump_p)
{
    struct upipe_hls_master *upipe_hls_master =
        upipe_hls_master_from_upipe(upipe);

    if (ubase_check(uref_block_get_start(uref)))
        upipe_hls_master_flush(upipe);

    if (!ubase_check(uref_uri_copy(uref, upipe_hls_master->flow_def))) {
        upipe_warn(upipe, "fail to import uri");
        uref_free(uref);
        return;
    }

    const char *type;
    if (ubase_check(uref_m3u_master_get_media_type(uref, &type))) {
        ulist_add(&upipe_hls_master->renditions, uref_to_uchain(uref));
    }
    else {
        const char *audio_id;
        if (ubase_check(uref_m3u_master_get_audio(uref, &audio_id)))
            upipe_hls_master_set_renditions(upipe, uref, "AUDIO", audio_id);
        //FIXME: handle video and subtitles
        uref_flow_set_id(uref, upipe_hls_master->id++);
        ulist_add(&upipe_hls_master->items, uref_to_uchain(uref));
    }

    if (ubase_check(uref_block_get_end(uref)))
        upipe_split_throw_update(upipe);
}

/** @internal @This iterates over variants.
 *
 * @param upipe description structure of the pipe
 * @param uref_p pointer filled with the next variant or NULL
 * @return an error code
 */
static int upipe_hls_master_split_iterate(struct upipe *upipe,
                                          struct uref **uref_p)
{
    struct upipe_hls_master *upipe_hls_master =
        upipe_hls_master_from_upipe(upipe);

    if (!uref_p)
        return UBASE_ERR_INVALID;

    struct uchain *uchain;
    if (*uref_p == NULL)
        uchain = &upipe_hls_master->items;
    else
        uchain = uref_to_uchain(*uref_p);
    if (ulist_is_last(&upipe_hls_master->items, uchain))
        *uref_p = NULL;
    else
        *uref_p = uref_from_uchain(uchain->next);
    return UBASE_ERR_NONE;
}

/** @internal @This stores a new input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def the flow format to store
 */
static void upipe_hls_master_store_flow_def(struct upipe *upipe,
                                            struct uref *flow_def)
{
    struct upipe_hls_master *upipe_hls_master =
        upipe_hls_master_from_upipe(upipe);
    if (upipe_hls_master->flow_def)
        uref_free(upipe_hls_master->flow_def);
    upipe_hls_master->flow_def = flow_def;
}

/** @internal @This sets a new input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def the flow format to set
 * @return an error code
 */
static int upipe_hls_master_set_flow_def(struct upipe *upipe,
                                         struct uref *flow_def)
{
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF));
    struct uref *flow_def_dup = uref_dup(flow_def);
    UBASE_ALLOC_RETURN(flow_def_dup);
    upipe_hls_master_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This dispatches commands to the pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to dispatch
 * @param args optional arguments
 * @return an error code
 */
static int upipe_hls_master_control(struct upipe *upipe,
                                    int command,
                                    va_list args)
{
    UBASE_HANDLED_RETURN(upipe_hls_master_control_pipes(upipe, command, args));
    UBASE_HANDLED_RETURN(upipe_control_provide_request(upipe, command, args));

    switch (command) {
    case UPIPE_SET_FLOW_DEF: {
        struct uref *flow_def = va_arg(args, struct uref *);
        return upipe_hls_master_set_flow_def(upipe, flow_def);
    }

    case UPIPE_SPLIT_ITERATE: {
        struct uref **uref_p = va_arg(args, struct uref **);
        return upipe_hls_master_split_iterate(upipe, uref_p);
    }
    }
    return UBASE_ERR_UNHANDLED;
}

/** @internal @This is the static structure for the master pipe manager. */
static struct upipe_mgr upipe_hls_master_mgr = {
    .refcount = NULL,
    .signature = UPIPE_HLS_MASTER_SIGNATURE,
    .upipe_alloc = upipe_hls_master_alloc,
    .upipe_input = upipe_hls_master_input,
    .upipe_control = upipe_hls_master_control,
};

/** @This allocates a hls master pipe manager.
 *
 * @return the pipe manager.
 */
struct upipe_mgr *upipe_hls_master_mgr_alloc(void)
{
    return &upipe_hls_master_mgr;
}
