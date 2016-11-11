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

/** @file
 * @short Upipe module to play output of a m3u reader pipe
 */

#include <upipe-hls/upipe_hls_playlist.h>

#include <upipe-modules/uref_aes_flow.h>
#include <upipe-modules/upipe_probe_uref.h>
#include <upipe-modules/upipe_setflowdef.h>

#include <upipe/upipe_helper_inner.h>
#include <upipe/upipe_helper_uprobe.h>
#include <upipe/upipe_helper_bin_output.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_urefcount_real.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe.h>

#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_helper_alloc.h>
#include <upipe/uprobe_helper_uprobe.h>
#include <upipe/uprobe.h>

#include <upipe/uref_m3u_master.h>
#include <upipe/uref_m3u_playlist_flow.h>
#include <upipe/uref_m3u_playlist.h>
#include <upipe/uref_m3u.h>
#include <upipe/uref_dump.h>
#include <upipe/uref_block.h>
#include <upipe/uref_uri.h>

#include <stdlib.h>
#include <limits.h>
#include <libgen.h>

/** @showvalue */
#define EXPECTED_FLOW_DEF "block.m3u.playlist."

static inline int upipe_hls_playlist_throw_reloaded(struct upipe *upipe)
{
    upipe_dbg(upipe, "throw reloaded");
    return upipe_throw(upipe, UPROBE_HLS_PLAYLIST_RELOADED,
                       UPIPE_HLS_PLAYLIST_SIGNATURE);
}

static inline int upipe_hls_playlist_throw_item_end(struct upipe *upipe)
{
    upipe_dbg(upipe, "throw item end");
    return upipe_throw(upipe, UPROBE_HLS_PLAYLIST_ITEM_END,
                       UPIPE_HLS_PLAYLIST_SIGNATURE);
}

/** @internal @This is the private context of a m3u playlist pipe. */
struct upipe_hls_playlist {
    /** for urefcount helper */
    struct urefcount urefcount;
    /** urefcount for proxy probe */
    struct urefcount urefcount_real;
    /** for upipe helper */
    struct upipe upipe;
    /** input flow format of the pipe */
    struct uref *input_flow_def;
    /** output flow format of the pipe */
    struct uref *flow_def;
    /** playlist */
    struct uchain items;
    /** source manager */
    struct upipe_mgr *source_mgr;
    /** request list */
    struct uchain requests;

    /** source pipe */
    struct upipe *src;
    /** key pipe */
    struct upipe *upipe_key;
    /** last inner pipe */
    struct upipe *setflowdef;
    /** output pipe */
    struct upipe *output;

    /** source probe */
    struct uprobe probe_src;
    /** proxy probe */
    struct uprobe probe_setflowdef;
    /** probe for key source */
    struct uprobe probe_key_src;
    /** key probe */
    struct uprobe probe_key;

    /** current index in the playlist */
    uint64_t index;
    /** reloading */
    bool reloading;
    /** output size for src */
    unsigned int output_size;
    /** current item */
    struct uref *item;
    /** current key */
    struct {
        char *uri;
        char *method;
    } key;
    /** attach uclock was called */
    bool attach_uclock;
};

static int probe_key_src(struct uprobe *uprobe, struct upipe *inner,
                         int event, va_list args);
static int probe_key(struct uprobe *uprobe, struct upipe *inner,
                     int event, va_list args);
static int probe_src(struct uprobe *uprobe, struct upipe *inner,
                     int event, va_list args);

UPIPE_HELPER_UPIPE(upipe_hls_playlist, upipe, UPIPE_HLS_PLAYLIST_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_hls_playlist, urefcount, upipe_hls_playlist_no_ref);
UPIPE_HELPER_UREFCOUNT_REAL(upipe_hls_playlist, urefcount_real,
                            upipe_hls_playlist_free);
UPIPE_HELPER_VOID(upipe_hls_playlist);
UPIPE_HELPER_INNER(upipe_hls_playlist, src);
UPIPE_HELPER_INNER(upipe_hls_playlist, upipe_key);
UPIPE_HELPER_INNER(upipe_hls_playlist, setflowdef);
UPIPE_HELPER_UPROBE(upipe_hls_playlist, urefcount_real,
                    probe_key_src, probe_key_src);
UPIPE_HELPER_UPROBE(upipe_hls_playlist, urefcount_real, probe_key, probe_key);
UPIPE_HELPER_UPROBE(upipe_hls_playlist, urefcount_real, probe_src, probe_src);
UPIPE_HELPER_UPROBE(upipe_hls_playlist, urefcount_real, probe_setflowdef, NULL);
UPIPE_HELPER_BIN_OUTPUT(upipe_hls_playlist, setflowdef, output, requests);

/** @internal @This catches the inner key source pipe event.
 *
 * @param uprobe structure used to raise events
 * @param inner the inner pipe
 * @param event event thrown
 * @param args optional arguments
 * @return an error code
 */
static int probe_key_src(struct uprobe *uprobe, struct upipe *inner,
                         int event, va_list args)
{
    struct upipe_hls_playlist *upipe_hls_playlist =
        upipe_hls_playlist_from_probe_key_src(uprobe);
    struct upipe *upipe = upipe_hls_playlist_to_upipe(upipe_hls_playlist);

    switch (event) {
    case UPROBE_SOURCE_END:
        upipe_hls_playlist_clean_upipe_key(upipe);
        upipe_hls_playlist_play(upipe);
        return UBASE_ERR_NONE;
    }
    return upipe_throw_proxy(upipe, inner, event, args);
}

/** @internal @This catches the inner probe uref pipe events.
 *
 * @param uprobe structure used to raise events
 * @param inner the inner pipe
 * @param event event thrown
 * @param args optional arguments
 * @return an error code
 */
static int probe_key(struct uprobe *uprobe, struct upipe *inner,
                     int event, va_list args)
{
    struct upipe_hls_playlist *upipe_hls_playlist =
        upipe_hls_playlist_from_probe_key(uprobe);
    struct upipe *upipe = upipe_hls_playlist_to_upipe(upipe_hls_playlist);
    struct uref *flow_def = upipe_hls_playlist->flow_def;

    switch (event) {
    case UPROBE_PROBE_UREF: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_PROBE_UREF_SIGNATURE);
        struct uref *uref = va_arg(args, struct uref *);
        bool *drop = va_arg(args, bool *);
        *drop = true;

        size_t size;
        uint8_t key[16];
        if (unlikely(!ubase_check(uref_block_size(uref, &size))) ||
            unlikely(size != 16) ||
            unlikely(!ubase_check(uref_block_extract(uref, 0, 16, key))))
            return UBASE_ERR_INVALID;

        char key_str[sizeof (key) * 2 + 1];
        for (unsigned i = 0; i < sizeof (key); i++)
            snprintf(key_str + i * 2, sizeof (key_str) - i * 2, "%x", key[i]);
        upipe_notice_va(upipe, "key: %s", key_str);

        UBASE_RETURN(uref_aes_set_key(flow_def, key, sizeof (key)));
        return UBASE_ERR_NONE;
    }
    case UPROBE_NEW_FLOW_DEF:
        return UBASE_ERR_NONE;
    case UPROBE_NEED_OUTPUT:
        return UBASE_ERR_INVALID;
    }
    return upipe_throw_proxy(upipe, inner, event, args);
}

/** @internal @This catches the inner source pipe events.
 *
 * @param uprobe structure used to raise events
 * @param inner the inner pipe
 * @param event event thrown
 * @param args optional arguments
 * @return an error code
 */
static int probe_src(struct uprobe *uprobe, struct upipe *inner,
                     int event, va_list args)
{
    struct upipe_hls_playlist *upipe_hls_playlist =
        upipe_hls_playlist_from_probe_src(uprobe);
    struct upipe *upipe = upipe_hls_playlist_to_upipe(upipe_hls_playlist);

    switch (event) {
    case UPROBE_NEED_OUTPUT:
        return upipe_set_output(inner, upipe_hls_playlist->setflowdef);

    case UPROBE_SOURCE_END:
        return upipe_hls_playlist_throw_item_end(upipe);
    }
    return upipe_throw_proxy(upipe, inner, event, args);
}

/** @internal @This allocates a m3u playlist pipe.
 *
 * @param mgr pointer to upipe manager
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_hls_playlist_alloc(struct upipe_mgr *mgr,
                                              struct uprobe *uprobe,
                                              uint32_t signature,
                                              va_list args)
{
    struct upipe *upipe =
        upipe_hls_playlist_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_hls_playlist_init_urefcount(upipe);
    upipe_hls_playlist_init_urefcount_real(upipe);
    upipe_hls_playlist_init_probe_src(upipe);
    upipe_hls_playlist_init_probe_key_src(upipe);
    upipe_hls_playlist_init_probe_key(upipe);
    upipe_hls_playlist_init_probe_setflowdef(upipe);
    upipe_hls_playlist_init_src(upipe);
    upipe_hls_playlist_init_upipe_key(upipe);
    upipe_hls_playlist_init_bin_output(upipe);

    struct upipe_hls_playlist *upipe_hls_playlist =
        upipe_hls_playlist_from_upipe(upipe);
    ulist_init(&upipe_hls_playlist->items);
    upipe_hls_playlist->input_flow_def = NULL;
    upipe_hls_playlist->flow_def = NULL;
    upipe_hls_playlist->source_mgr = NULL;
    upipe_hls_playlist->index = (uint64_t)-1;
    upipe_hls_playlist->reloading = false;
    upipe_hls_playlist->output_size = 0;
    upipe_hls_playlist->item = NULL;
    upipe_hls_playlist->key.uri = NULL;
    upipe_hls_playlist->key.method = NULL;
    upipe_hls_playlist->attach_uclock = false;

    upipe_throw_ready(upipe);

    struct upipe_mgr *upipe_setflowdef_mgr = upipe_setflowdef_mgr_alloc();
    if (unlikely(upipe_setflowdef_mgr == NULL)) {
        upipe_release(upipe);
        return NULL;
    }
    struct upipe *setflowdef = upipe_void_alloc(
        upipe_setflowdef_mgr,
        uprobe_pfx_alloc(uprobe_use(&upipe_hls_playlist->probe_setflowdef),
                         UPROBE_LOG_VERBOSE, "setflowdef"));
    upipe_mgr_release(upipe_setflowdef_mgr);
    if (unlikely(setflowdef == NULL)) {
        upipe_release(upipe);
        return NULL;
    }
    upipe_hls_playlist_store_bin_output(upipe, setflowdef);

    return upipe;
}

/** @internal @This flushes the cached items from the input pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_hls_playlist_flush(struct upipe *upipe)
{
    struct upipe_hls_playlist *upipe_hls_playlist =
        upipe_hls_playlist_from_upipe(upipe);

    upipe_hls_playlist->item = NULL;
    free(upipe_hls_playlist->key.uri);
    upipe_hls_playlist->key.uri = NULL;
    free(upipe_hls_playlist->key.method);
    upipe_hls_playlist->key.method = NULL;
    struct uchain *uchain;
    while ((uchain = ulist_pop(&upipe_hls_playlist->items)) != NULL)
        uref_free(uref_from_uchain(uchain));
}

/** @internal @This frees the pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_hls_playlist_free(struct upipe *upipe)
{
    struct upipe_hls_playlist *upipe_hls_playlist =
        upipe_hls_playlist_from_upipe(upipe);

    upipe_throw_dead(upipe);

    free(upipe_hls_playlist->key.uri);
    free(upipe_hls_playlist->key.method);
    uref_free(upipe_hls_playlist->flow_def);
    uref_free(upipe_hls_playlist->input_flow_def);
    upipe_hls_playlist_clean_bin_output(upipe);
    upipe_hls_playlist_flush(upipe);
    upipe_hls_playlist_clean_probe_src(upipe);
    upipe_hls_playlist_clean_probe_key(upipe);
    upipe_hls_playlist_clean_probe_key_src(upipe);
    upipe_hls_playlist_clean_probe_setflowdef(upipe);
    upipe_hls_playlist_clean_urefcount(upipe);
    upipe_hls_playlist_clean_urefcount_real(upipe);
    upipe_hls_playlist_free_void(upipe);
}

/** @internal @This is called when there is no external reference to the pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_hls_playlist_no_ref(struct upipe *upipe)
{
    struct upipe_hls_playlist *upipe_hls_playlist =
        upipe_hls_playlist_from_upipe(upipe);

    upipe_hls_playlist_clean_upipe_key(upipe);
    upipe_hls_playlist_clean_setflowdef(upipe);
    upipe_hls_playlist_clean_src(upipe);
    upipe_mgr_release(upipe_hls_playlist->source_mgr);
    upipe_hls_playlist_release_urefcount_real(upipe);
}

/** @internal @This sets the inner source pipe of the playlist.
 *
 * @param upipe description structure of the pipe
 * @param src the inner source pipe to set
 * @return an error code
 */
static int upipe_hls_playlist_set_src(struct upipe *upipe,
                                      struct upipe *src)
{
    struct upipe_hls_playlist *upipe_hls_playlist =
        upipe_hls_playlist_from_upipe(upipe);
    int ret;

    if (src) {
        if (upipe_hls_playlist->attach_uclock) {
            ret = upipe_attach_uclock(src);
            if (unlikely(!ubase_check(ret))) {
                upipe_release(src);
                return ret;
            }
        }
        if (upipe_hls_playlist->output_size) {
            ret = upipe_set_output_size(src, upipe_hls_playlist->output_size);
            if (unlikely(!ubase_check(ret))) {
                upipe_release(src);
                return ret;
            }
        }
    }
    upipe_hls_playlist_store_src(upipe, src);
    return UBASE_ERR_NONE;
}

/** @internal @This checks if the source manager is set and asks for it if not.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_hls_playlist_check_source_mgr(struct upipe *upipe)
{
    struct upipe_hls_playlist *upipe_hls_playlist =
        upipe_hls_playlist_from_upipe(upipe);
    if (unlikely(upipe_hls_playlist->source_mgr == NULL))
        return upipe_throw_need_source_mgr(
            upipe, &upipe_hls_playlist->source_mgr);
    return UBASE_ERR_NONE;
}

/** @internal @This creates the inner pipeline to get a key.
 *
 * @param upipe description structure of the pipe
 * @param key key information
 * @param uuri the uri of the key to retrieve
 * @return an error code
 */
static int upipe_hls_playlist_get_key_uri(struct upipe *upipe,
                                          struct uuri *uuri)
{
    struct upipe_hls_playlist *upipe_hls_playlist =
        upipe_hls_playlist_from_upipe(upipe);
    int ret;

    size_t uri_len;
    UBASE_RETURN(uuri_len(uuri, &uri_len));
    char uri[uri_len + 1];
    UBASE_RETURN(uuri_to_buffer(uuri, uri, sizeof (uri)));

    UBASE_RETURN(upipe_hls_playlist_check_source_mgr(upipe));
    struct upipe *upipe_key = upipe_void_alloc(
        upipe_hls_playlist->source_mgr,
        uprobe_pfx_alloc(
            uprobe_use(&upipe_hls_playlist->probe_key_src),
            UPROBE_LOG_VERBOSE, "key src"));
    if (unlikely(upipe_key == NULL))
        return UBASE_ERR_ALLOC;

    struct upipe_mgr *upipe_probe_uref_mgr = upipe_probe_uref_mgr_alloc();
    if (unlikely(upipe_probe_uref_mgr == NULL)) {
        upipe_release(upipe_key);
        return UBASE_ERR_ALLOC;
    }
    struct upipe *output = upipe_void_alloc_output(
        upipe_key, upipe_probe_uref_mgr,
        uprobe_pfx_alloc(
            uprobe_use(&upipe_hls_playlist->probe_key),
            UPROBE_LOG_VERBOSE, "key"));
    upipe_mgr_release(upipe_probe_uref_mgr);
    if (unlikely(output == NULL)) {
        upipe_release(upipe_key);
        return UBASE_ERR_ALLOC;
    }
    upipe_release(output);

    ret = upipe_set_uri(upipe_key, uri);
    if (unlikely(!ubase_check(ret))) {
        upipe_release(upipe_key);
        return ret;
    }

    upipe_hls_playlist_store_upipe_key(upipe, upipe_key);
    return UBASE_ERR_NONE;
}

/** @internal @This makes the uuri of a key from it uri and
 * creates the inner pipeline to retrieve it.
 *
 * @param upipe description structure of the pipe
 * @param key key information
 * @param uri the uri of the key to retrieve
 * @return an error code
 */
static int upipe_hls_playlist_get_key(struct upipe *upipe,
                                      const char *method,
                                      const char *uri)
{
    struct upipe_hls_playlist *upipe_hls_playlist =
        upipe_hls_playlist_from_upipe(upipe);
    struct uref *input_flow_def = upipe_hls_playlist->input_flow_def;

    struct uref *flow_def = upipe_hls_playlist->flow_def;
    uref_aes_delete(flow_def);

    upipe_hls_playlist_clean_upipe_key(upipe);
    free(upipe_hls_playlist->key.uri);
    upipe_hls_playlist->key.uri = strdup(uri);
    free(upipe_hls_playlist->key.method);
    upipe_hls_playlist->key.method = strdup(method);

    UBASE_RETURN(uref_flow_set_def(flow_def, "block.aes."));
    UBASE_RETURN(uref_aes_set_method(flow_def, method));
    struct uuri uuri;
    if (ubase_check(uuri_from_str(&uuri, uri)))
        return upipe_hls_playlist_get_key_uri(upipe, &uuri);

    UBASE_RETURN(uref_uri_get(input_flow_def, &uuri));
    uuri.query = ustring_null();
    uuri.fragment = ustring_null();
    if (*uri == '/') {
        uuri.path = ustring_from_str(uri);
        return upipe_hls_playlist_get_key_uri(upipe, &uuri);
    }

    char tmp[uuri.path.len + 1];
    UBASE_RETURN(ustring_cpy(uuri.path, tmp, sizeof (tmp)));
    const char *root = dirname(tmp);
    char new_path[strlen(root) + 1 + strlen(uri) + 1];
    int ret = snprintf(new_path, sizeof (new_path), "%s/%s", root, uri);
    if (ret < 0 || (unsigned)ret >= sizeof (new_path))
        return UBASE_ERR_INVALID;
    uuri.path = ustring_from_str(new_path);
    return upipe_hls_playlist_get_key_uri(upipe, &uuri);
}

/** @internal @This update the inner setflowdef dict.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_hls_playlist_update_flow_def(struct upipe *upipe)
{
    struct upipe_hls_playlist *upipe_hls_playlist =
        upipe_hls_playlist_from_upipe(upipe);

    if (unlikely(upipe_hls_playlist->setflowdef == NULL) ||
        unlikely(upipe_hls_playlist->flow_def == NULL))
        return UBASE_ERR_INVALID;

    return upipe_setflowdef_set_dict(upipe_hls_playlist->setflowdef,
                                     upipe_hls_playlist->flow_def);
}

/** @internal @This plays an URI.
 *
 * @param upipe description structure of the pipe
 * @param item item to play
 * @param uuri the URI of the item to play
 * @return an error code
 */
static int upipe_hls_playlist_play_uri(struct upipe *upipe,
                                       struct uref *item,
                                       struct uuri *uuri)
{
    struct upipe_hls_playlist *upipe_hls_playlist =
        upipe_hls_playlist_from_upipe(upipe);
    struct uref *input_flow_def = upipe_hls_playlist->input_flow_def;

    size_t len;
    UBASE_RETURN(uuri_len(uuri, &len));
    char uri[len + 1];
    UBASE_RETURN(uuri_to_buffer(uuri, uri, sizeof (uri)));

    upipe_notice_va(upipe, "play next item sequence %"PRIu64" %s",
                    upipe_hls_playlist->index, uri);

    struct uref *flow_def = upipe_hls_playlist->flow_def;
    if (ubase_check(uref_flow_match_def(flow_def, "block.aes."))) {
        const uint8_t *iv;
        size_t iv_size;
        if (ubase_check(uref_aes_get_iv(input_flow_def, &iv, &iv_size))) {
            UBASE_RETURN(uref_aes_set_iv(flow_def, iv, iv_size));
        }
        else {
            uint8_t iv_buf[16];

            memset(&iv_buf, 0, sizeof(iv_buf));
            for (unsigned i = 0; i < 8; i++)
                iv_buf[15 - i] = upipe_hls_playlist->index >> (i * 8);
            UBASE_RETURN(uref_aes_set_iv(flow_def, iv_buf, sizeof(iv_buf)));
        }
    }
    UBASE_RETURN(upipe_hls_playlist_update_flow_def(upipe));

    UBASE_RETURN(upipe_hls_playlist_check_source_mgr(upipe));
    struct upipe *inner = upipe_void_alloc(
        upipe_hls_playlist->source_mgr,
        uprobe_pfx_alloc(
            uprobe_use(&upipe_hls_playlist->probe_src),
            UPROBE_LOG_VERBOSE, "src"));
    UBASE_ALLOC_RETURN(inner);
    UBASE_RETURN(upipe_hls_playlist_set_src(upipe, inner));
    UBASE_RETURN(upipe_set_uri(inner, uri));

    uint64_t range_off = 0;
    uref_m3u_playlist_get_byte_range_off(item, &range_off);
    uint64_t range_len = (uint64_t)-1;
    uref_m3u_playlist_get_byte_range_len(item, &range_len);
    UBASE_RETURN(upipe_src_set_range(inner, range_off, range_len));
    return UBASE_ERR_NONE;
}

/** @internal @This plays an item.
 *
 * @param upipe description structure of the pipe
 * @param item item to play
 * @return an error code
 */
static int upipe_hls_playlist_play_item(struct upipe *upipe, struct uref *item)
{
    struct upipe_hls_playlist *upipe_hls_playlist =
        upipe_hls_playlist_from_upipe(upipe);
    struct uref *input_flow_def = upipe_hls_playlist->input_flow_def;
    int ret;

    if (unlikely(input_flow_def == NULL) || unlikely(item == NULL))
        return UBASE_ERR_INVALID;

    upipe_verbose_va(upipe, "play item sequence %"PRIu64,
                     upipe_hls_playlist->index);
    uref_dump(item, upipe->uprobe);

    const char *m3u_uri;
    UBASE_RETURN(uref_m3u_get_uri(item, &m3u_uri));

    struct uuri uuri;
    if (ubase_check(uuri_from_str(&uuri, m3u_uri)))
        /* this is a valid URI, we can directly play it */
        return upipe_hls_playlist_play_uri(upipe, item, &uuri);

    UBASE_RETURN(uref_uri_get(input_flow_def, &uuri));
    uuri.query = ustring_null();
    uuri.fragment = ustring_null();
    if (strlen(m3u_uri) && *m3u_uri == '/') {
        /* use the item absolute path with the input scheme */
        uuri.path = ustring_from_str(m3u_uri);
        return upipe_hls_playlist_play_uri(upipe, item, &uuri);
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
    return upipe_hls_playlist_play_uri(upipe, item, &uuri);
}

/** @internal @This gets a media sequence by its sequence number.
 *
 * @param upipe description structure of the pipe
 * @param index the sequence number
 * @param item_p pointer filled with the media sequence
 * @param key_p pointer filled with the key if any
 * @return an error code
 */
static int upipe_hls_playlist_get_item_at(struct upipe *upipe,
                                          uint64_t index,
                                          struct uref **item_p)
{
    struct upipe_hls_playlist *upipe_hls_playlist =
        upipe_hls_playlist_from_upipe(upipe);
    struct uref *input_flow_def = upipe_hls_playlist->input_flow_def;

    uint64_t media_sequence = 0;
    uref_m3u_playlist_flow_get_media_sequence(input_flow_def, &media_sequence);
    if (index < media_sequence)
        return UBASE_ERR_INVALID;
    index -= media_sequence;

    struct uchain *uchain;
    ulist_foreach(&upipe_hls_playlist->items, uchain) {
        struct uref *uref = uref_from_uchain(uchain);

        if (index-- == 0) {
            *item_p = uref;
            return UBASE_ERR_NONE;
        }
    }
    upipe_notice(upipe, "nothing to play");
    return UBASE_ERR_INVALID;
}

/** @internal @This plays the next item in the playlist.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int _upipe_hls_playlist_play(struct upipe *upipe)
{
    struct upipe_hls_playlist *upipe_hls_playlist =
        upipe_hls_playlist_from_upipe(upipe);
    struct uref *input_flow_def = upipe_hls_playlist->input_flow_def;

    if (unlikely(input_flow_def == NULL))
        return UBASE_ERR_INVALID;

    uint64_t media_sequence = 0;
    uref_m3u_playlist_flow_get_media_sequence(input_flow_def, &media_sequence);

    if (upipe_hls_playlist->index == (uint64_t)-1)
        upipe_hls_playlist->index = media_sequence;

    if (media_sequence > upipe_hls_playlist->index)
        return UBASE_ERR_INVALID;

    struct uref *item = NULL;
    UBASE_RETURN(upipe_hls_playlist_get_item_at(
            upipe, upipe_hls_playlist->index, &item));

    const char *method;
    if (ubase_check(uref_m3u_playlist_key_get_method(item, &method))) {
        if (strcasecmp(method, "AES-128"))
            return UBASE_ERR_UNHANDLED;

        const char *key_uri;
        UBASE_RETURN(uref_m3u_playlist_key_get_uri(item, &key_uri));

        if (upipe_hls_playlist->key.uri == NULL ||
            upipe_hls_playlist->key.method == NULL ||
            strcmp(method, upipe_hls_playlist->key.method) ||
            strcmp(key_uri, upipe_hls_playlist->key.uri))
            return upipe_hls_playlist_get_key(upipe, method, key_uri);
    }

    return upipe_hls_playlist_play_item(upipe, item);
}

/** @internal @This goes to the next element in the playlist.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int _upipe_hls_playlist_next(struct upipe *upipe)
{
    struct upipe_hls_playlist *upipe_hls_playlist =
        upipe_hls_playlist_from_upipe(upipe);
    struct uref *input_flow_def = upipe_hls_playlist->input_flow_def;

    if (unlikely(input_flow_def == NULL))
        return UBASE_ERR_INVALID;

    uint64_t media_sequence;
    if (!ubase_check(uref_m3u_playlist_flow_get_media_sequence(
                input_flow_def, &media_sequence)))
        media_sequence = 0;

    if (upipe_hls_playlist->index == (uint64_t)-1)
        upipe_hls_playlist->index = media_sequence;

    if (media_sequence > upipe_hls_playlist->index)
        return UBASE_ERR_INVALID;

    upipe_hls_playlist->index++;
    upipe_dbg_va(upipe, "next item %"PRIu64, upipe_hls_playlist->index);
    return UBASE_ERR_NONE;
}

/** @internal @This is called when there is input data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref carrying the data
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_hls_playlist_input(struct upipe *upipe,
                                     struct uref *uref,
                                     struct upump **upump_p)
{
    struct upipe_hls_playlist *upipe_hls_playlist =
        upipe_hls_playlist_from_upipe(upipe);

    if (unlikely(!upipe_hls_playlist->reloading)) {
        upipe_dbg(upipe, "playlist start");
        upipe_hls_playlist_flush(upipe);
        upipe_hls_playlist->reloading = true;
    }

    ulist_add(&upipe_hls_playlist->items, uref_to_uchain(uref));
    if (ubase_check(uref_block_get_end(uref))) {
        upipe_dbg(upipe, "playlist end");
        upipe_hls_playlist->reloading = false;
        upipe_hls_playlist_throw_reloaded(upipe);
    }
}

/** @internal @This stores a new input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def pointer to the new input flow definition
 */
static void upipe_hls_playlist_store_input_flow_def(struct upipe *upipe,
                                                    struct uref *flow_def)
{
    struct upipe_hls_playlist *upipe_hls_playlist =
        upipe_hls_playlist_from_upipe(upipe);

    uref_free(upipe_hls_playlist->input_flow_def);
    upipe_hls_playlist->input_flow_def = flow_def;
}

/** @internal @This sets a new flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def pointer to the flow definition
 * @return an error code
 */
static int upipe_hls_playlist_set_flow_def(struct upipe *upipe,
                                           struct uref *input_flow_def)
{
    struct upipe_hls_playlist *upipe_hls_playlist =
        upipe_hls_playlist_from_upipe(upipe);

    UBASE_RETURN(uref_flow_match_def(input_flow_def, EXPECTED_FLOW_DEF));

    if (unlikely(upipe_hls_playlist->flow_def == NULL)) {
        struct uref *flow_def = uref_sibling_alloc(input_flow_def);
        if (unlikely(flow_def == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
        upipe_hls_playlist->flow_def = flow_def;
    }
    struct uref *flow_def = upipe_hls_playlist->flow_def;
    UBASE_RETURN(uref_m3u_master_copy(flow_def, input_flow_def));
    UBASE_RETURN(uref_m3u_playlist_flow_copy(flow_def, input_flow_def));

    struct uref *flow_def_dup = uref_dup(input_flow_def);
    if (unlikely(flow_def_dup == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    upipe_hls_playlist_store_input_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This gets the current index in the playlist.
 *
 * @param upipe description structure of the pipe
 * @param index_p filled with the index
 * @return an error code
 */
static int _upipe_hls_playlist_get_index(struct upipe *upipe,
                                         uint64_t *index_p)
{
    struct upipe_hls_playlist *upipe_hls_playlist =
        upipe_hls_playlist_from_upipe(upipe);
    if (likely(index_p != NULL))
        *index_p = upipe_hls_playlist->index;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the current index in the playlist.
 *
 * @param upipe description structure of the pipe
 * @param index index to set
 * @return an error code
 */
static int _upipe_hls_playlist_set_index(struct upipe *upipe,
                                         uint64_t index)
{
    struct upipe_hls_playlist *upipe_hls_playlist =
        upipe_hls_playlist_from_upipe(upipe);
    upipe_hls_playlist->index = index;
    return UBASE_ERR_NONE;
}

/** @internal @This seeks into the playlist the corresponding media sequence
 * for a given offset.
 *
 * @param upipe description structure of the pipe
 * @param at offset to seek
 * @param offset_p filled with remaining offset to seek in the current item,
 * may be NULL.
 * @return an error code
 */
static int _upipe_hls_playlist_seek(struct upipe *upipe,
                                    uint64_t at, uint64_t *offset_p)
{
    struct upipe_hls_playlist *upipe_hls_playlist =
        upipe_hls_playlist_from_upipe(upipe);
    struct uref *flow = upipe_hls_playlist->input_flow_def;

    /* if playlist type is set, it must be "VOD" or "EVENT". */
    const char *type;
    if (ubase_check(uref_m3u_playlist_flow_get_type(flow, &type)) &&
        strcmp(type, "VOD") && strcmp(type, "EVENT"))
        return UBASE_ERR_UNHANDLED;

    /* we can't seek if items has been removed from the playlist,
     * ie. media sequence is not 0. */
    uint64_t seq;
    if (!ubase_check(uref_m3u_playlist_flow_get_media_sequence(flow, &seq)))
        seq = 0;
    if (seq != 0)
        return UBASE_ERR_UNHANDLED;

    uint64_t index = 0;
    struct uchain *uchain;
    ulist_foreach(&upipe_hls_playlist->items, uchain) {
        struct uref *item = uref_from_uchain(uchain);
        uint64_t seq_duration;
        UBASE_RETURN(uref_m3u_playlist_get_seq_duration(item, &seq_duration));
        if (at < seq_duration) {
            if (offset_p)
                *offset_p = at;
            return _upipe_hls_playlist_set_index(upipe, index);
        }
        at -= seq_duration;
        index++;
    }

    return UBASE_ERR_INVALID;
}

/** @internal @This sets the inner pipe output size.
 *
 * @param upipe description structure of the pipe
 * @param output_size new output size
 * @return an error code
 */
static int upipe_hls_playlist_set_output_size(struct upipe *upipe,
                                              unsigned int output_size)
{
    struct upipe_hls_playlist *upipe_hls_playlist =
        upipe_hls_playlist_from_upipe(upipe);
    upipe_hls_playlist->output_size = output_size;
    if (likely(upipe_hls_playlist->src != NULL))
        return upipe_set_output_size(upipe_hls_playlist->src, output_size);
    return UBASE_ERR_NONE;
}

/** @internal @This attaches an uclock to the inner pipe.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_hls_playlist_attach_uclock(struct upipe *upipe)
{
    struct upipe_hls_playlist *upipe_hls_playlist =
        upipe_hls_playlist_from_upipe(upipe);
    upipe_hls_playlist->attach_uclock = true;
    if (upipe_hls_playlist->src != NULL)
        return upipe_attach_uclock(upipe_hls_playlist->src);
    return UBASE_ERR_NONE;
}

/** @internal @This dispatches commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args optional arguments
 */
static int upipe_hls_playlist_control(struct upipe *upipe,
                                      int command,
                                      va_list args)
{
    switch (command) {
    case UPIPE_REGISTER_REQUEST: {
        struct urequest *urequest = va_arg(args, struct urequest *);
        return upipe_throw_provide_request(upipe, urequest);
    }
    case UPIPE_UNREGISTER_REQUEST:
        return UBASE_ERR_NONE;

    case UPIPE_ATTACH_UCLOCK:
        return upipe_hls_playlist_attach_uclock(upipe);

    case UPIPE_SET_FLOW_DEF: {
        struct uref *flow = va_arg(args, struct uref *);
        return upipe_hls_playlist_set_flow_def(upipe, flow);
    }

    case UPIPE_SET_OUTPUT_SIZE: {
        unsigned int output_size = va_arg(args, unsigned int);
        return upipe_hls_playlist_set_output_size(upipe, output_size);
    }

    case UPIPE_HLS_PLAYLIST_GET_INDEX: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_HLS_PLAYLIST_SIGNATURE)
        uint64_t *index_p = va_arg(args, uint64_t *);
        return _upipe_hls_playlist_get_index(upipe, index_p);
    }
    case UPIPE_HLS_PLAYLIST_SET_INDEX: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_HLS_PLAYLIST_SIGNATURE)
        uint64_t index = va_arg(args, uint64_t);
        return _upipe_hls_playlist_set_index(upipe, index);
    }

    case UPIPE_HLS_PLAYLIST_PLAY: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_HLS_PLAYLIST_SIGNATURE)
        return _upipe_hls_playlist_play(upipe);
    }

    case UPIPE_HLS_PLAYLIST_NEXT: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_HLS_PLAYLIST_SIGNATURE)
        return _upipe_hls_playlist_next(upipe);
    }

    case UPIPE_HLS_PLAYLIST_SEEK: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_HLS_PLAYLIST_SIGNATURE);
        uint64_t at = va_arg(args, uint64_t);
        uint64_t *offset_p = va_arg(args, uint64_t *);
        return _upipe_hls_playlist_seek(upipe, at, offset_p);
    }

    default:
        return upipe_hls_playlist_control_bin_output(upipe, command, args);
    }
}

/** @internal m3u playlist manager static descriptor */
static struct upipe_mgr upipe_hls_playlist_mgr = {
    .refcount = NULL,
    .signature = UPIPE_HLS_PLAYLIST_SIGNATURE,

    .upipe_command_str = upipe_hls_playlist_command_str,
    .upipe_event_str = uprobe_hls_playlist_event_str,

    .upipe_alloc = upipe_hls_playlist_alloc,
    .upipe_input = upipe_hls_playlist_input,
    .upipe_control = upipe_hls_playlist_control,
};

/** @This returns the m3u playlist manager.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_hls_playlist_mgr_alloc(void)
{
    return &upipe_hls_playlist_mgr;
}
