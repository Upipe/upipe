/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * Authors: Arnaud de Turckheim
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
 * @short Upipe module to play output of a m3u reader pipe
 */

#include <stdlib.h>
#include <limits.h>
#include <libgen.h>

#include <upipe/uref_uri.h>
#include <upipe/uref_m3u.h>
#include <upipe/uref_m3u_playlist.h>
#include <upipe/uref_m3u_playlist_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_dump.h>
#include <upipe/uprobe_helper_uprobe.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_input.h>
#include <upipe-modules/upipe_m3u_playlist.h>

/** @showvalue */
#define EXPECTED_FLOW_DEF "block.m3u.playlist."

/** @internal @This is the private context of a m3u playlist pipe. */
struct upipe_m3u_playlist {
    /** for urefcount helper */
    struct urefcount urefcount;
    /** urefcount for proxy probe */
    struct urefcount urefcount_real;
    /** for upipe helper */
    struct upipe upipe;
    /** flow format of the pipe */
    struct uref *flow_def;
    /** proxy probe */
    struct uprobe proxy_probe;
    /** source pipe */
    struct upipe *src;
    /** playlist */
    struct uchain items;
    /** source manager */
    struct upipe_mgr *source_mgr;

    /** current playing index */
    uint64_t index;
    /** currently playing */
    bool playing;
};

UPIPE_HELPER_UPIPE(upipe_m3u_playlist, upipe, UPIPE_M3U_PLAYLIST_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_m3u_playlist, urefcount, upipe_m3u_playlist_no_ref)
UPIPE_HELPER_FLOW(upipe_m3u_playlist, EXPECTED_FLOW_DEF)

UBASE_FROM_TO(upipe_m3u_playlist, uprobe, proxy_probe, proxy_probe)
UBASE_FROM_TO(upipe_m3u_playlist, urefcount, urefcount_real, urefcount_real)

/** @hidden */
static int upipe_m3u_playlist_play_next(struct upipe *upipe);
/** @hidden */
static void upipe_m3u_playlist_free(struct urefcount *urefcount);

/** @internal @This catches events from the inner source pipe.
 *
 * @param uprobe structure used to raise events
 * @param inner description structure of the inner pipe
 * @param event event thrown
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_m3u_playlist_proxy_probe(struct uprobe *uprobe,
                                          struct upipe *inner,
                                          int event, va_list args)
{
    struct upipe_m3u_playlist *upipe_m3u_playlist =
        upipe_m3u_playlist_from_proxy_probe(uprobe);
    struct upipe *upipe = upipe_m3u_playlist_to_upipe(upipe_m3u_playlist);

    switch (event) {
    case UPROBE_SOURCE_END:
        return upipe_m3u_playlist_play_next(upipe);
    }

    return upipe_throw_proxy(upipe, inner, event, args);
}

/** @internal @This flushes the cached items from the input pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_m3u_playlist_flush(struct upipe *upipe)
{
    struct upipe_m3u_playlist *upipe_m3u_playlist =
        upipe_m3u_playlist_from_upipe(upipe);

    struct uchain *uchain;
    while ((uchain = ulist_pop(&upipe_m3u_playlist->items)) != NULL)
        uref_free(uref_from_uchain(uchain));
}

/** @internal @This allocates a m3u playlist pipe.
 *
 * @param mgr pointer to upipe manager
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_m3u_playlist_alloc(struct upipe_mgr *mgr,
                                              struct uprobe *uprobe,
                                              uint32_t signature,
                                              va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe =
        upipe_m3u_playlist_alloc_flow(mgr, uprobe, signature, args,
                                      &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;
    struct upipe_m3u_playlist *upipe_m3u_playlist =
        upipe_m3u_playlist_from_upipe(upipe);

    upipe_m3u_playlist_init_urefcount(upipe);
    urefcount_init(upipe_m3u_playlist_to_urefcount_real(upipe_m3u_playlist),
                   upipe_m3u_playlist_free);
    uprobe_init(upipe_m3u_playlist_to_proxy_probe(upipe_m3u_playlist),
                upipe_m3u_playlist_proxy_probe, NULL);
    upipe_m3u_playlist->proxy_probe.refcount =
        upipe_m3u_playlist_to_urefcount_real(upipe_m3u_playlist);
    ulist_init(&upipe_m3u_playlist->items);
    upipe_m3u_playlist->flow_def = flow_def;
    upipe_m3u_playlist->src = NULL;
    upipe_m3u_playlist->source_mgr = NULL;
    upipe_m3u_playlist->index = (uint64_t)-1;
    upipe_m3u_playlist->playing = false;

    upipe_throw_ready(upipe);

    uref_dump(flow_def, uprobe);

    return upipe;
}

/** @internal @This frees the pipe.
 *
 * @param urefcount pointer to the embedded urefcount
 */
static void upipe_m3u_playlist_free(struct urefcount *urefcount)
{
    struct upipe_m3u_playlist *upipe_m3u_playlist =
        upipe_m3u_playlist_from_urefcount_real(urefcount);
    struct upipe *upipe = upipe_m3u_playlist_to_upipe(upipe_m3u_playlist);

    upipe_throw_dead(upipe);

    upipe_m3u_playlist_flush(upipe);
    uref_free(upipe_m3u_playlist->flow_def);
    urefcount_clean(upipe_m3u_playlist_to_urefcount_real(upipe_m3u_playlist));
    upipe_m3u_playlist_clean_urefcount(upipe);
    upipe_m3u_playlist_free_flow(upipe);
}

/** @internal @This is called when there is no external reference to the pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_m3u_playlist_no_ref(struct upipe *upipe)
{
    struct upipe_m3u_playlist *upipe_m3u_playlist =
        upipe_m3u_playlist_from_upipe(upipe);

    upipe_release(upipe_m3u_playlist->src);
    upipe_mgr_release(upipe_m3u_playlist->source_mgr);
    urefcount_release(upipe_m3u_playlist_to_urefcount_real(upipe_m3u_playlist));
}

/** @internal @This plays an URI.
 *
 * @param upipe description structure of the pipe
 * @param item item to play
 * @param uuri the URI of the item to play
 * @return an error code
 */
static int upipe_m3u_playlist_play_uri(struct upipe *upipe,
                                       struct uref *item,
                                       struct uuri *uuri)
{
    struct upipe_m3u_playlist *upipe_m3u_playlist =
        upipe_m3u_playlist_from_upipe(upipe);

    size_t len;
    UBASE_RETURN(uuri_len(uuri, &len));
    char uri[len + 1];
    UBASE_RETURN(uuri_to_buffer(uuri, uri, sizeof (uri)));

    upipe_notice_va(upipe, "play next item sequence %"PRIu64" %s",
                    upipe_m3u_playlist->index, uri);

    uint64_t range_off = 0;
    if (!ubase_check(uref_m3u_playlist_get_byte_range_off(item, &range_off)))
        range_off = 0;
    uint64_t range_len = 0;
    if (!ubase_check(uref_m3u_playlist_get_byte_range_len(item, &range_len)))
        range_len = (uint64_t)-1;

    UBASE_RETURN(upipe_set_uri(upipe_m3u_playlist->src, uri));
    UBASE_RETURN(upipe_src_set_range(upipe_m3u_playlist->src,
                                     range_off, range_len));
    upipe_m3u_playlist->playing = true;
    return UBASE_ERR_NONE;
}

/** @internal @This plays an item.
 *
 * @param upipe description structure of the pipe
 * @param item item to play
 * @return an error code
 */
static int upipe_m3u_playlist_play_item(struct upipe *upipe, struct uref *item)
{
    struct upipe_m3u_playlist *upipe_m3u_playlist =
        upipe_m3u_playlist_from_upipe(upipe);
    int ret;

    upipe_verbose_va(upipe, "play next item sequence %"PRIu64,
                     upipe_m3u_playlist->index);
    uref_dump(item, upipe->uprobe);

    const char *m3u_uri;
    UBASE_RETURN(uref_m3u_get_uri(item, &m3u_uri));

    struct uuri uuri;
    if (ubase_check(uuri_from_str(&uuri, m3u_uri)))
        /* this is a valid URI, we can directly play it */
        return upipe_m3u_playlist_play_uri(upipe, item, &uuri);

    UBASE_RETURN(uref_uri_get(upipe_m3u_playlist->flow_def, &uuri));
    uuri.query = ustring_null();
    uuri.fragment = ustring_null();
    if (strlen(m3u_uri) && *m3u_uri == '/') {
        /* use the item absolute path with the input scheme */
        uuri.path = ustring_from_str(m3u_uri);
        return upipe_m3u_playlist_play_uri(upipe, item, &uuri);
    }

    /* use the item relative path with the input path as root path */
    char tmp[uuri.path.len + 1];
    ustring_cpy(uuri.path, tmp, sizeof (tmp));
    const char *root = dirname(tmp);
    char new_path[strlen(root) + 1 + strlen(m3u_uri) + 1];
    ret = snprintf(new_path, sizeof (new_path), "%s/%s", root, m3u_uri);
    if (ret < 0 || (unsigned)ret >= sizeof (new_path))
        return UBASE_ERR_NOSPC;
    uuri.path = ustring_from_str(new_path);
    return upipe_m3u_playlist_play_uri(upipe, item, &uuri);
}

/** @internal @This plays the next item in the playlist.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_m3u_playlist_play_next(struct upipe *upipe)
{
    struct upipe_m3u_playlist *upipe_m3u_playlist =
        upipe_m3u_playlist_from_upipe(upipe);
    struct uchain *uchain;
    struct uref *item;

    upipe_m3u_playlist->playing = false;

    uint64_t media_sequence;
    if (!ubase_check(uref_m3u_playlist_flow_get_media_sequence(
                upipe_m3u_playlist->flow_def, &media_sequence)))
        media_sequence = 0;

    if (upipe_m3u_playlist->index == (uint64_t)-1)
        upipe_m3u_playlist->index = media_sequence;

    if (media_sequence > upipe_m3u_playlist->index)
        return upipe_throw_source_end(upipe);

    uchain = ulist_at(&upipe_m3u_playlist->items,
                      upipe_m3u_playlist->index - media_sequence);
    if (!uchain)
        return upipe_throw_source_end(upipe);

    item = uref_from_uchain(uchain);
    upipe_m3u_playlist->index++;
    return upipe_m3u_playlist_play_item(upipe, item);
}

/** @internal @This is called when there is input data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref carrying the data
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_m3u_playlist_input(struct upipe *upipe,
                                     struct uref *uref,
                                     struct upump **upump_p)
{
    struct upipe_m3u_playlist *upipe_m3u_playlist =
        upipe_m3u_playlist_from_upipe(upipe);

    if (ubase_check(uref_block_get_start(uref))) {
        upipe_dbg(upipe, "playlist start");
        upipe_m3u_playlist_flush(upipe);
    }

    ulist_add(&upipe_m3u_playlist->items, uref_to_uchain(uref));
    if (ubase_check(uref_block_get_end(uref))) {
        upipe_dbg(upipe, "playlist end");
        if (!upipe_m3u_playlist->playing)
            upipe_m3u_playlist_play_next(upipe);
    }
}

/** @internal @This stores a new flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def pointer to the flow definition
 */
static void upipe_m3u_playlist_store_flow_def(struct upipe *upipe,
                                              struct uref *flow_def)
{
    struct upipe_m3u_playlist *upipe_m3u_playlist =
        upipe_m3u_playlist_from_upipe(upipe);

    uref_free(upipe_m3u_playlist->flow_def);
    upipe_m3u_playlist->flow_def = flow_def;
    upipe_throw_new_flow_def(upipe, flow_def);
}

/** @internal @This sets a new flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def pointer to the flow definition
 * @return an error code
 */
static int upipe_m3u_playlist_set_flow_def(struct upipe *upipe,
                                           struct uref *flow_def)
{
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF));

    struct uref *flow_def_dup = uref_dup(flow_def);
    if (unlikely(flow_def_dup == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    upipe_m3u_playlist_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This gets the flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def_p filled in with the flow definition
 * @return an error code
 */
static int upipe_m3u_playlist_get_flow_def(struct upipe *upipe,
                                           struct uref **flow_def_p)
{
    struct upipe_m3u_playlist *upipe_m3u_playlist =
        upipe_m3u_playlist_from_upipe(upipe);
    if (likely(flow_def_p != NULL))
        *flow_def_p = upipe_m3u_playlist->flow_def;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the output pipe.
 *
 * @param upipe description structure of the pipe
 * @param output output pipe
 * @return an error code
 */
static int upipe_m3u_playlist_set_output(struct upipe *upipe,
                                         struct upipe *output)
{
    struct upipe_m3u_playlist *upipe_m3u_playlist =
        upipe_m3u_playlist_from_upipe(upipe);
    if (unlikely(upipe_m3u_playlist->src == NULL))
        return UBASE_ERR_INVALID;
    return upipe_set_output(upipe_m3u_playlist->src, output);
}

/** @internal @This gets the output pipe.
 *
 * @param upipe description structure of the pipe
 * @param output_p filled with the output pipe.
 * @return an error code
 */
static int upipe_m3u_playlist_get_output(struct upipe *upipe,
                                         struct upipe **output_p)
{
    struct upipe_m3u_playlist *upipe_m3u_playlist =
        upipe_m3u_playlist_from_upipe(upipe);
    if (unlikely(upipe_m3u_playlist->src == NULL))
        return UBASE_ERR_INVALID;
    return upipe_get_output(upipe_m3u_playlist->src, output_p);
}

/** @internal @This sets the source manager.
 *
 * @param upipe description structure of the pipe
 * @param upipe_mgr pointer to upipe source manager
 * @return an error code
 */
static int _upipe_m3u_playlist_set_source_mgr(struct upipe *upipe,
                                              struct upipe_mgr *upipe_mgr)
{
    struct upipe_m3u_playlist *upipe_m3u_playlist =
        upipe_m3u_playlist_from_upipe(upipe);
    if (upipe_m3u_playlist->source_mgr)
        upipe_mgr_release(upipe_m3u_playlist->source_mgr);
    upipe_m3u_playlist->source_mgr = upipe_mgr_use(upipe_mgr);
    if (upipe_m3u_playlist->src)
        upipe_release(upipe_m3u_playlist->src);

    upipe_m3u_playlist->src = upipe_void_alloc(
        upipe_m3u_playlist->source_mgr,
        uprobe_pfx_alloc(
            uprobe_use(upipe_m3u_playlist_to_proxy_probe(upipe_m3u_playlist)),
            UPROBE_LOG_VERBOSE, "src"));
    if (unlikely(upipe_m3u_playlist->src == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    return UBASE_ERR_NONE;
}

/** @internal @This gets the current index in the playlist.
 *
 * @param upipe description structure of the pipe
 * @param index_p filled with the index
 * @return an error code
 */
static int _upipe_m3u_playlist_get_index(struct upipe *upipe,
                                         uint64_t *index_p)
{
    struct upipe_m3u_playlist *upipe_m3u_playlist =
        upipe_m3u_playlist_from_upipe(upipe);
    if (likely(index_p != NULL))
        *index_p = upipe_m3u_playlist->index;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the current index in the playlist.
 *
 * @param upipe description structure of the pipe
 * @param index index to set
 * @return an error code
 */
static int _upipe_m3u_playlist_set_index(struct upipe *upipe,
                                         uint64_t index)
{
    struct upipe_m3u_playlist *upipe_m3u_playlist =
        upipe_m3u_playlist_from_upipe(upipe);
    upipe_m3u_playlist->index = index;
    return UBASE_ERR_NONE;
}

/** @internal @This dispatches commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args optional arguments
 */
static int upipe_m3u_playlist_control(struct upipe *upipe,
                                      int command,
                                      va_list args)
{
    switch (command) {
    case UPIPE_REGISTER_REQUEST: {
        struct urequest *request = va_arg(args, struct urequest *);
        return upipe_throw_provide_request(upipe, request);
    }
    case UPIPE_UNREGISTER_REQUEST:
        return UBASE_ERR_NONE;

    case UPIPE_SET_FLOW_DEF: {
        struct uref *flow = va_arg(args, struct uref *);
        return upipe_m3u_playlist_set_flow_def(upipe, flow);
    }
    case UPIPE_GET_FLOW_DEF: {
        struct uref **flow_def_p = va_arg(args, struct uref **);
        return upipe_m3u_playlist_get_flow_def(upipe, flow_def_p);
    }

    case UPIPE_SET_OUTPUT: {
        struct upipe *output = va_arg(args, struct upipe *);
        return upipe_m3u_playlist_set_output(upipe, output);
    }
    case UPIPE_GET_OUTPUT: {
        struct upipe **output_p = va_arg(args, struct upipe **);
        return upipe_m3u_playlist_get_output(upipe, output_p);
    }

    case UPIPE_M3U_PLAYLIST_SET_SOURCE_MGR: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_M3U_PLAYLIST_SIGNATURE)
        struct upipe_mgr *upipe_mgr = va_arg(args, struct upipe_mgr *);
        return _upipe_m3u_playlist_set_source_mgr(upipe, upipe_mgr);
    }

    case UPIPE_M3U_PLAYLIST_GET_INDEX: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_M3U_PLAYLIST_SIGNATURE)
        uint64_t *index_p = va_arg(args, uint64_t *);
        return _upipe_m3u_playlist_get_index(upipe, index_p);
    }
    case UPIPE_M3U_PLAYLIST_SET_INDEX: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_M3U_PLAYLIST_SIGNATURE)
        uint64_t index = va_arg(args, uint64_t);
        return _upipe_m3u_playlist_set_index(upipe, index);
    }

    default:
        return UBASE_ERR_UNHANDLED;
    }
}

/** @internal m3u playlist manager static descriptor */
static struct upipe_mgr upipe_m3u_playlist_mgr = {
    .refcount = NULL,
    .signature = UPIPE_M3U_PLAYLIST_SIGNATURE,

    .upipe_command_str = upipe_m3u_playlist_command_str,

    .upipe_alloc = upipe_m3u_playlist_alloc,
    .upipe_input = upipe_m3u_playlist_input,
    .upipe_control = upipe_m3u_playlist_control,
};

/** @This returns the m3u playlist manager.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_m3u_playlist_mgr_alloc(void)
{
    return &upipe_m3u_playlist_mgr;
}
