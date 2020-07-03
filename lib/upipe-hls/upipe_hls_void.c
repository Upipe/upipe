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

#include <upipe-hls/upipe_hls_void.h>
#include <upipe-hls/upipe_hls_variant.h>
#include <upipe-hls/upipe_hls_playlist.h>

#include <upipe-ts/upipe_ts_demux.h>

#include <upipe-framers/upipe_auto_framer.h>

#include <upipe-modules/upipe_aes_decrypt.h>
#include <upipe-modules/upipe_m3u_reader.h>

#include <upipe/upipe_helper_bin_output.h>
#include <upipe/upipe_helper_inner.h>
#include <upipe/upipe_helper_uprobe.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_urefcount_real.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_inner.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe.h>

#include <upipe/uprobe_select_flows.h>
#include <upipe/uprobe_prefix.h>

/** @internal @This is the private context of mixed rendition subpipe. */
struct upipe_hls_void_sub {
    /** public upipe structure */
    struct upipe upipe;
    /** refcount management structure */
    struct urefcount urefcount;
    /** real refcount management structure */
    struct urefcount urefcount_real;
    /** link to the super pipe */
    struct uchain uchain;
    /** list of requests */
    struct uchain requests;
    /** inner probe */
    struct uprobe probe_src;
    /** inner pipe */
    struct upipe *src;
    /** output pipe */
    struct upipe *output;
};

UPIPE_HELPER_UPIPE(upipe_hls_void_sub, upipe, UPIPE_HLS_VOID_SUB_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_hls_void_sub, urefcount,
                       upipe_hls_void_sub_no_ref);
UPIPE_HELPER_UREFCOUNT_REAL(upipe_hls_void_sub, urefcount_real,
                            upipe_hls_void_sub_free);
UPIPE_HELPER_FLOW(upipe_hls_void_sub, "block.");
UPIPE_HELPER_INNER(upipe_hls_void_sub, src);
UPIPE_HELPER_UPROBE(upipe_hls_void_sub, urefcount_real, probe_src, NULL);
UPIPE_HELPER_BIN_OUTPUT(upipe_hls_void_sub, src, output, requests);

/** @internal @This is the private context of mixed rendition pipe. */
struct upipe_hls_void {
    /** public upipe structure */
    struct upipe upipe;
    /** refcount management structure */
    struct urefcount urefcount;
    /** real refcount management structure */
    struct urefcount urefcount_real;
    /** sub pipe manager */
    struct upipe_mgr sub_mgr;
    /** list of sub pipes */
    struct uchain subs;
    /** inner source probe */
    struct uprobe probe_src;
    /** inner reader probe */
    struct uprobe probe_reader;
    /** inner playlist probe */
    struct uprobe probe_playlist;
    /** inner aes decrypt probe */
    struct uprobe probe_aes_decrypt;
    /** inner pmt probe */
    struct uprobe probe_demux_in;
    /** inner demux probe */
    struct uprobe probe_demux_out;
    /** source pipe manager */
    struct upipe_mgr *source_mgr;
    /** source pipe */
    struct upipe *src;
    /** playlist pipe */
    struct upipe *playlist;
    /** pmt pipe */
    struct upipe *pmt;
    /** attach uclock is needed */
    bool attach_uclock;
    /** uri */
    char *uri;
};

/** @hidden */
static int probe_src(struct uprobe *uprobe, struct upipe *inner,
                     int event, va_list args);

/** @hidden */
static int probe_playlist(struct uprobe *uprobe, struct upipe *inner,
                          int event, va_list args);

/** @hidden */
static int probe_demux_in(struct uprobe *uprobe, struct upipe *inner,
                          int event, va_list args);

/** @hidden */
static int probe_demux_out(struct uprobe *uprobe, struct upipe *inner,
                           int event, va_list args);

UPIPE_HELPER_UPIPE(upipe_hls_void, upipe, UPIPE_HLS_VOID_SIGNATURE);
UPIPE_HELPER_INNER(upipe_hls_void, src);
UPIPE_HELPER_INNER(upipe_hls_void, playlist);
UPIPE_HELPER_INNER(upipe_hls_void, pmt);
UPIPE_HELPER_UREFCOUNT(upipe_hls_void, urefcount, upipe_hls_void_no_ref);
UPIPE_HELPER_UREFCOUNT_REAL(upipe_hls_void, urefcount_real,
                            upipe_hls_void_free);
UPIPE_HELPER_VOID(upipe_hls_void);
UPIPE_HELPER_SUBPIPE(upipe_hls_void, upipe_hls_void_sub, pipe, sub_mgr,
                     subs, uchain);
UPIPE_HELPER_UPROBE(upipe_hls_void, urefcount_real, probe_src, probe_src);
UPIPE_HELPER_UPROBE(upipe_hls_void, urefcount_real, probe_reader, NULL);
UPIPE_HELPER_UPROBE(upipe_hls_void, urefcount_real,
                    probe_playlist, probe_playlist);
UPIPE_HELPER_UPROBE(upipe_hls_void, urefcount_real, probe_aes_decrypt, NULL);
UPIPE_HELPER_UPROBE(upipe_hls_void, urefcount_real,
                    probe_demux_in, probe_demux_in);
UPIPE_HELPER_UPROBE(upipe_hls_void, urefcount_real,
                    probe_demux_out, probe_demux_out);

/** @internal @This allocates a mixed hls variant subpipe.
 *
 * @param mgr pointer to manager
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return a pointer to the allocated pipe
 */
static struct upipe *upipe_hls_void_sub_alloc(struct upipe_mgr *mgr,
                                              struct uprobe *uprobe,
                                              uint32_t signature,
                                              va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe =
        upipe_hls_void_sub_alloc_flow(mgr, uprobe, signature, args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_hls_void_sub_init_urefcount(upipe);
    upipe_hls_void_sub_init_urefcount_real(upipe);
    upipe_hls_void_sub_init_sub(upipe);
    upipe_hls_void_sub_init_probe_src(upipe);
    upipe_hls_void_sub_init_bin_output(upipe);

    upipe_throw_ready(upipe);

    struct upipe_hls_void_sub *upipe_hls_void_sub =
        upipe_hls_void_sub_from_upipe(upipe);
    struct upipe_hls_void *upipe_hls_void = upipe_hls_void_from_sub_mgr(mgr);
    struct upipe *inner = NULL;
    if (likely(upipe_hls_void->pmt != NULL))
        inner = upipe_flow_alloc_sub(
            upipe_hls_void->pmt,
            uprobe_pfx_alloc(uprobe_use(&upipe_hls_void_sub->probe_src),
                             UPROBE_LOG_VERBOSE, "ts"),
            flow_def);
    upipe_hls_void_sub_store_bin_output(upipe, inner);
    uref_free(flow_def);

    return upipe;
}

/** @internal @This frees the sub pipe.
 *
 * @param upipe description structure of the subpipe
 */
static void upipe_hls_void_sub_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_hls_void_sub_clean_bin_output(upipe);
    upipe_hls_void_sub_clean_probe_src(upipe);
    upipe_hls_void_sub_clean_sub(upipe);
    upipe_hls_void_sub_clean_urefcount(upipe);
    upipe_hls_void_sub_clean_urefcount_real(upipe);
    upipe_hls_void_sub_free_flow(upipe);
}

/** @internal @This is called when there is no external reference to
 * the subpipe.
 *
 * @param upipe description structure of the subpipe
 */
static void upipe_hls_void_sub_no_ref(struct upipe *upipe)
{
    upipe_hls_void_sub_clean_src(upipe);
    upipe_hls_void_sub_release_urefcount_real(upipe);
}

/** @internal @This dispatches commands to the subpipe.
 *
 * @param upipe description structure of the subpipe
 * @param command type of command to dispatch
 * @param args optional arguments
 * @return an error code
 */
static int upipe_hls_void_sub_control(struct upipe *upipe,
                                      int command,
                                      va_list args)
{
    UBASE_HANDLED_RETURN(
        upipe_hls_void_sub_control_super(upipe, command, args));
    switch (command) {
    case UPIPE_BIN_GET_FIRST_INNER: {
        struct upipe_hls_void_sub *upipe_hls_void_sub =
            upipe_hls_void_sub_from_upipe(upipe);
        struct upipe **p = va_arg(args, struct upipe **);
        *p = upipe_hls_void_sub->src;
        return (*p != NULL) ? UBASE_ERR_NONE : UBASE_ERR_UNHANDLED;
    }
    }
    return upipe_hls_void_sub_control_bin_output(upipe, command, args);
}

/** @internal @This catches event from the pmt inner pipe.
 *
 * @param uprobe structure used to raise events
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args optional arguments
 * @return an error code
 */
static int probe_demux_in(struct uprobe *uprobe, struct upipe *inner,
                          int event, va_list args)
{
    struct upipe_hls_void *upipe_hls_void =
        upipe_hls_void_from_probe_demux_in(uprobe);
    struct upipe *upipe = upipe_hls_void_to_upipe(upipe_hls_void);

    switch (event) {
    case UPROBE_SOURCE_END:
        upipe_hls_void_store_pmt(upipe, NULL);
        return UBASE_ERR_NONE;

    case UPROBE_SPLIT_UPDATE:
        upipe_hls_void_store_pmt(upipe, upipe_use(inner));
        UBASE_ALLOC_RETURN(upipe_hls_void->pmt);
        return upipe_split_throw_update(upipe);
    }
    return upipe_throw_proxy(upipe, inner, event, args);
}

/** @internal @This catches event from the ts demux inner pipe.
 *
 * @param uprobe structure used to raise events
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args optional arguments
 * @return an error code
 */
static int probe_demux_out(struct uprobe *uprobe, struct upipe *inner,
                           int event, va_list args)
{
    struct upipe_hls_void *upipe_hls_void =
        upipe_hls_void_from_probe_demux_out(uprobe);
    struct upipe *upipe = upipe_hls_void_to_upipe(upipe_hls_void);

    switch (event) {
    case UPROBE_SPLIT_UPDATE:
        return UBASE_ERR_NONE;
    }
    return upipe_throw_proxy(upipe, inner, event, args);
}

/** @internal @This checks if source manager is set and asks for it if not.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_hls_void_check_source_mgr(struct upipe *upipe)
{
    struct upipe_hls_void *upipe_hls_void = upipe_hls_void_from_upipe(upipe);
    if (unlikely(upipe_hls_void->source_mgr == NULL))
        return upipe_throw_need_source_mgr(upipe, &upipe_hls_void->source_mgr);
    return UBASE_ERR_NONE;
}

static int upipe_hls_void_reload(struct upipe *upipe)
{
    struct upipe_hls_void *upipe_hls_void = upipe_hls_void_from_upipe(upipe);

    UBASE_RETURN(upipe_hls_void_check_source_mgr(upipe));

    struct upipe *src = upipe_hls_void->src;
    if (unlikely(!src)) {
        upipe_warn(upipe, "no source pipe to reload");
        return UBASE_ERR_INVALID;
    }

    struct upipe *output;
    int ret = upipe_get_output(src, &output);
    if (unlikely(!ubase_check(ret))) {
        upipe_warn(upipe, "source pipe has no output pipe");
        return ret;
    }

    upipe_dbg_va(upipe, "reloading %s", upipe_hls_void->uri);

    struct upipe *inner = upipe_void_alloc(
        upipe_hls_void->source_mgr,
        uprobe_pfx_alloc(uprobe_use(&upipe_hls_void->probe_src),
                         UPROBE_LOG_VERBOSE, "src"));
    UBASE_ALLOC_RETURN(inner);

    ret = upipe_set_output(inner, output);
    if (unlikely(!ubase_check(ret))) {
        upipe_release(inner);
        return UBASE_ERR_INVALID;
    }

    ret = upipe_set_uri(inner, upipe_hls_void->uri);
    if (unlikely(!ubase_check(ret))) {
        upipe_release(inner);
        return ret;
    }

    upipe_hls_void_store_src(upipe, inner);
    return UBASE_ERR_NONE;
}

/** @internal @This catches event from the playlist inner pipe.
 *
 * @param uprobe structure used to raise events
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args optional arguments
 * @return an error code
 */
static int probe_playlist(struct uprobe *uprobe, struct upipe *inner,
                          int event, va_list args)
{
    struct upipe_hls_void *upipe_hls_void =
        upipe_hls_void_from_probe_playlist(uprobe);
    struct upipe *upipe = upipe_hls_void_to_upipe(upipe_hls_void);

    switch (event) {
    case UPROBE_NEED_OUTPUT: {
        struct uref *flow_def = va_arg(args, struct uref *);
        struct upipe *output = upipe_use(inner);
        int ret;

        /* AES decrypt?
         */
        if (ubase_check(uref_flow_match_def(flow_def, "block.aes."))) {
            struct upipe_mgr *upipe_aes_decrypt_mgr =
                upipe_aes_decrypt_mgr_alloc();
            if (unlikely(upipe_aes_decrypt_mgr == NULL)) {
                upipe_release(output);
                return UBASE_ERR_ALLOC;
            }
            output = upipe_void_chain_output(
                output, upipe_aes_decrypt_mgr,
                uprobe_pfx_alloc(
                    uprobe_use(&upipe_hls_void->probe_aes_decrypt),
                    UPROBE_LOG_VERBOSE, "aes decrypt"));
            upipe_mgr_release(upipe_aes_decrypt_mgr);
            UBASE_ALLOC_RETURN(output);
        }

        /* ts demux
         */
        struct upipe_mgr *upipe_ts_demux_mgr = upipe_ts_demux_mgr_alloc();
        if (unlikely(upipe_ts_demux_mgr == NULL)) {
            upipe_release(output);
            return UBASE_ERR_ALLOC;
        }

        struct upipe_mgr *upipe_autof_mgr = upipe_autof_mgr_alloc();
        if (unlikely(upipe_autof_mgr == NULL)) {
            upipe_mgr_release(upipe_ts_demux_mgr);
            upipe_release(output);
            return UBASE_ERR_ALLOC;
        }
        ret = upipe_ts_demux_mgr_set_autof_mgr(upipe_ts_demux_mgr,
                                               upipe_autof_mgr);
        upipe_mgr_release(upipe_autof_mgr);
        if (unlikely(!ubase_check(ret))) {
            upipe_mgr_release(upipe_ts_demux_mgr);
            upipe_release(output);
            return ret;
        }
        output = upipe_void_chain_output(
            output, upipe_ts_demux_mgr,
            uprobe_pfx_alloc(
                uprobe_selflow_alloc(
                    uprobe_pfx_alloc(
                        uprobe_use(&upipe_hls_void->probe_demux_out),
                        UPROBE_LOG_VERBOSE, "demux out"),
                    uprobe_pfx_alloc(
                        uprobe_use(&upipe_hls_void->probe_demux_in),
                        UPROBE_LOG_VERBOSE, "demux in"),
                    UPROBE_SELFLOW_VOID, "auto"),
                UPROBE_LOG_VERBOSE, "demux"));
        upipe_mgr_release(upipe_ts_demux_mgr);
        UBASE_ALLOC_RETURN(output);
        upipe_release(output);
        return UBASE_ERR_NONE;
    }
    }

    if (event >= UPROBE_LOCAL) {
        switch (ubase_get_signature(args)) {
        case UPIPE_HLS_PLAYLIST_SIGNATURE:
            switch (event) {
            case UPROBE_HLS_PLAYLIST_NEED_RELOAD:
                return upipe_hls_void_reload(upipe);
            };
        }
    }

    return upipe_throw_proxy(upipe, inner, event, args);
}

/** @internal @This catches event from the source inner pipe.
 *
 * @param uprobe structure used to raise events
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args optional arguments
 * @return an error code
 */
static int probe_src(struct uprobe *uprobe, struct upipe *inner,
                     int event, va_list args)
{
    struct upipe_hls_void *upipe_hls_void =
        upipe_hls_void_from_probe_src(uprobe);
    struct upipe *upipe = upipe_hls_void_to_upipe(upipe_hls_void);

    switch (event) {
    case UPROBE_NEED_OUTPUT: {
        /* m3u reader pipe
        */
        struct upipe_mgr *upipe_m3u_reader_mgr = upipe_m3u_reader_mgr_alloc();
        UBASE_ALLOC_RETURN(upipe_m3u_reader_mgr);
        struct upipe *upipe_output = upipe_void_alloc_output(
            inner, upipe_m3u_reader_mgr,
            uprobe_pfx_alloc(uprobe_use(&upipe_hls_void->probe_reader),
                             UPROBE_LOG_VERBOSE, "m3u"));
        upipe_mgr_release(upipe_m3u_reader_mgr);
        UBASE_ALLOC_RETURN(upipe_output);

        /* playlist pipe
        */
        struct upipe_mgr *upipe_hls_playlist_mgr =
            upipe_hls_playlist_mgr_alloc();
        if (unlikely(upipe_hls_playlist_mgr == NULL)) {
            upipe_release(upipe_output);
            return UBASE_ERR_ALLOC;
        }
        upipe_output = upipe_void_chain_output(
            upipe_output, upipe_hls_playlist_mgr,
            uprobe_pfx_alloc(uprobe_use(&upipe_hls_void->probe_playlist),
                             UPROBE_LOG_VERBOSE, "playlist"));
        upipe_mgr_release(upipe_hls_playlist_mgr);
        UBASE_ALLOC_RETURN(upipe_output);
        if (upipe_hls_void->attach_uclock)
            upipe_attach_uclock(upipe_output);
        upipe_hls_void_store_playlist(upipe, upipe_use(upipe_output));
        upipe_release(upipe_output);

        return UBASE_ERR_NONE;
    }

    case UPROBE_SOURCE_END:
        return UBASE_ERR_NONE;
    }
    return upipe_throw_proxy(upipe, inner, event, args);
}

/** @internal @This initializes the sub pipe manager.
 *
 * @param upipe description structure of the subpipe
 */
static void upipe_hls_void_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_hls_void *upipe_hls_void = upipe_hls_void_from_upipe(upipe);
    memset(&upipe_hls_void->sub_mgr, 0, sizeof (struct upipe_mgr));
    upipe_hls_void->sub_mgr.refcount = &upipe_hls_void->urefcount_real;
    upipe_hls_void->sub_mgr.signature = UPIPE_HLS_VOID_SUB_SIGNATURE;
    upipe_hls_void->sub_mgr.upipe_alloc = upipe_hls_void_sub_alloc;
    upipe_hls_void->sub_mgr.upipe_control = upipe_hls_void_sub_control;
}

/** @internal @This allocates a mixed hls variant pipe.
 *
 * @param mgr pointer to manager
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return a pointer to the allocated pipe
 */
static struct upipe *upipe_hls_void_alloc(struct upipe_mgr *mgr,
                                          struct uprobe *uprobe,
                                          uint32_t signature,
                                          va_list args)
{
    struct upipe *upipe =
        upipe_hls_void_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_hls_void_init_urefcount(upipe);
    upipe_hls_void_init_urefcount_real(upipe);
    upipe_hls_void_init_probe_src(upipe);
    upipe_hls_void_init_probe_reader(upipe);
    upipe_hls_void_init_probe_playlist(upipe);
    upipe_hls_void_init_probe_aes_decrypt(upipe);
    upipe_hls_void_init_probe_demux_in(upipe);
    upipe_hls_void_init_probe_demux_out(upipe);
    upipe_hls_void_init_sub_mgr(upipe);
    upipe_hls_void_init_sub_pipes(upipe);
    upipe_hls_void_init_src(upipe);
    upipe_hls_void_init_playlist(upipe);
    upipe_hls_void_init_pmt(upipe);

    struct upipe_hls_void *upipe_hls_void = upipe_hls_void_from_upipe(upipe);
    upipe_hls_void->source_mgr = NULL;
    upipe_hls_void->attach_uclock = false;
    upipe_hls_void->uri = NULL;

    upipe_throw_ready(upipe);

    return upipe;
}

/** @internal @This frees the pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_hls_void_free(struct upipe *upipe)
{
    struct upipe_hls_void *upipe_hls_void = upipe_hls_void_from_upipe(upipe);
    upipe_throw_dead(upipe);

    free(upipe_hls_void->uri);
    upipe_hls_void_clean_sub_pipes(upipe);
    upipe_hls_void_clean_probe_src(upipe);
    upipe_hls_void_clean_probe_reader(upipe);
    upipe_hls_void_clean_probe_playlist(upipe);
    upipe_hls_void_clean_probe_aes_decrypt(upipe);
    upipe_hls_void_clean_probe_demux_in(upipe);
    upipe_hls_void_clean_probe_demux_out(upipe);
    upipe_hls_void_clean_urefcount(upipe);
    upipe_hls_void_clean_urefcount_real(upipe);
    upipe_hls_void_free_void(upipe);
}

/** @internal @This is called when there is no external reference to the pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_hls_void_no_ref(struct upipe *upipe)
{
    struct upipe_hls_void *upipe_hls_void = upipe_hls_void_from_upipe(upipe);

    upipe_hls_void_clean_pmt(upipe);
    upipe_hls_void_clean_playlist(upipe);
    upipe_hls_void_clean_src(upipe);
    upipe_mgr_release(upipe_hls_void->source_mgr);
    upipe_hls_void_release_urefcount_real(upipe);
}

/** @internal @This sets the uri.
 *
 * @param upipe description structure of the pipe
 * @param uri the uri to set
 * @return an error code
 */
static int upipe_hls_void_set_uri(struct upipe *upipe, const char *uri)
{
    struct upipe_hls_void *upipe_hls_void = upipe_hls_void_from_upipe(upipe);

    upipe_hls_void_clean_src(upipe);
    UBASE_RETURN(upipe_hls_void_check_source_mgr(upipe));

    struct upipe *inner = upipe_void_alloc(
        upipe_hls_void->source_mgr,
        uprobe_pfx_alloc(uprobe_use(&upipe_hls_void->probe_src),
                         UPROBE_LOG_VERBOSE, "src"));
    UBASE_ALLOC_RETURN(inner);

    int ret = upipe_set_uri(inner, uri);
    if (unlikely(!ubase_check(ret))) {
        upipe_release(inner);
        return ret;
    }

    upipe_hls_void->uri = uri ? strdup(uri) : NULL;
    upipe_hls_void_store_src(upipe, inner);
    return UBASE_ERR_NONE;
}

/** @internal @This attaches an uclock.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_hls_void_attach_uclock(struct upipe *upipe)
{
    struct upipe_hls_void *upipe_hls_void = upipe_hls_void_from_upipe(upipe);
    upipe_hls_void->attach_uclock = true;
    if (upipe_hls_void->playlist)
        return upipe_attach_uclock(upipe_hls_void->playlist);
    return UBASE_ERR_NONE;
}

/** @internal @This dispatches commands to the pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to dispatch
 * @param args optional arguments
 * @return an error code
 */
static int upipe_hls_void_control(struct upipe *upipe,
                                  int command,
                                  va_list args)
{
    UBASE_HANDLED_RETURN(upipe_hls_void_control_pipes(upipe, command, args));

    switch (command) {
    case UPIPE_ATTACH_UCLOCK:
        return upipe_hls_void_attach_uclock(upipe);

    case UPIPE_SET_URI: {
        const char *uri = va_arg(args, const char *);
        return upipe_hls_void_set_uri(upipe, uri);
    }

    case UPIPE_SPLIT_ITERATE:
        return upipe_hls_void_control_pmt(upipe, command, args);
    case UPIPE_BIN_GET_FIRST_INNER: {
        struct upipe_hls_void *upipe_hls_void =
            upipe_hls_void_from_upipe(upipe);
        struct upipe **p = va_arg(args, struct upipe **);
        *p = upipe_hls_void->src;
        return (*p != NULL) ? UBASE_ERR_NONE : UBASE_ERR_UNHANDLED;
    }
    case UPIPE_BIN_GET_LAST_INNER: {
        struct upipe_hls_void *upipe_hls_void =
            upipe_hls_void_from_upipe(upipe);
        struct upipe **p = va_arg(args, struct upipe **);
        *p = upipe_hls_void->playlist;
        return (*p != NULL) ? UBASE_ERR_NONE : UBASE_ERR_UNHANDLED;
    }
    }

    if (command >= UPROBE_LOCAL) {
        switch (ubase_get_signature(args)) {
        case UPIPE_HLS_PLAYLIST_SIGNATURE:
            return upipe_hls_void_control_playlist(upipe, command, args);
        }
    }

    return UBASE_ERR_UNHANDLED;
}

/** @internal @This is the static structure for manager. */
static struct upipe_mgr upipe_hls_void_mgr = {
    .refcount = NULL,
    .signature = UPIPE_HLS_VOID_SIGNATURE,
    .upipe_alloc = upipe_hls_void_alloc,
    .upipe_control = upipe_hls_void_control,
};

/** @This returns the management structure.
 *
 * @return a pointer to the manager.
 */
struct upipe_mgr *upipe_hls_void_mgr_alloc(void)
{
    return &upipe_hls_void_mgr;
}
