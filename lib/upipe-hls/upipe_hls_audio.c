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

#include <upipe-hls/upipe_hls_audio.h>
#include <upipe-hls/upipe_hls_variant.h>
#include <upipe-hls/upipe_hls_playlist.h>

#include <upipe-modules/upipe_aes_decrypt.h>
#include <upipe-modules/upipe_id3v2.h>
#include <upipe-modules/upipe_m3u_reader.h>

#include <upipe-framers/upipe_mpga_framer.h>

#include <upipe/upipe_helper_uprobe.h>
#include <upipe/upipe_helper_bin_output.h>
#include <upipe/upipe_helper_inner.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_urefcount_real.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_upipe.h>

#include <upipe/uprobe_prefix.h>

/** @internal @This is the private context of an audio rendition pipe. */
struct upipe_hls_audio {
    /** public pipe structure */
    struct upipe upipe;
    /** refcount structure */
    struct urefcount urefcount;
    /** real refcount structure */
    struct urefcount urefcount_real;
    /** output request list */
    struct uchain requests;
    /** source manager */
    struct upipe_mgr *source_mgr;
    /** source pipe */
    struct upipe *src;
    /** playlist pipe */
    struct upipe *playlist;
    /** last inner pipe */
    struct upipe *last_inner;
    /** output pipe */
    struct upipe *output;
    /** source probe */
    struct uprobe probe_src;
    /** m3u reader probe */
    struct uprobe probe_reader;
    /** playlist probe */
    struct uprobe probe_playlist;
    /** id3v2 probe */
    struct uprobe probe_id3v2;
    /** last inner probe */
    struct uprobe probe_last_inner;
    /** aes decrypt inner probe */
    struct uprobe probe_aes_decrypt;
    /** attach uclock needed */
    bool attach_uclock;
};

/** @hidden */
static int probe_src(struct uprobe *uprobe, struct upipe *inner,
                     int event, va_list args);

/** @hidden */
static int probe_playlist(struct uprobe *uprobe, struct upipe *inner,
                          int event, va_list args);

/** @hidden */
static int upipe_hls_audio_set_playlist(struct upipe *upipe,
                                        struct upipe *playlist);

UPIPE_HELPER_UPIPE(upipe_hls_audio, upipe, UPIPE_HLS_AUDIO_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_hls_audio, urefcount, upipe_hls_audio_no_ref);
UPIPE_HELPER_UREFCOUNT_REAL(upipe_hls_audio, urefcount_real,
                            upipe_hls_audio_free);
UPIPE_HELPER_VOID(upipe_hls_audio);
UPIPE_HELPER_INNER(upipe_hls_audio, src);
UPIPE_HELPER_INNER(upipe_hls_audio, playlist);
UPIPE_HELPER_INNER(upipe_hls_audio, last_inner);
UPIPE_HELPER_UPROBE(upipe_hls_audio, urefcount_real, probe_src, probe_src);
UPIPE_HELPER_UPROBE(upipe_hls_audio, urefcount_real, probe_reader, NULL);
UPIPE_HELPER_UPROBE(upipe_hls_audio, urefcount_real, probe_playlist, probe_playlist);
UPIPE_HELPER_UPROBE(upipe_hls_audio, urefcount_real, probe_id3v2, NULL);
UPIPE_HELPER_UPROBE(upipe_hls_audio, urefcount_real, probe_last_inner, NULL);
UPIPE_HELPER_UPROBE(upipe_hls_audio, urefcount_real, probe_aes_decrypt, NULL);
UPIPE_HELPER_BIN_OUTPUT(upipe_hls_audio, last_inner, output, requests);

static int probe_playlist(struct uprobe *uprobe, struct upipe *inner,
                          int event, va_list args)
{
    struct upipe_hls_audio *upipe_hls_audio =
        upipe_hls_audio_from_probe_playlist(uprobe);
    struct upipe *upipe = upipe_hls_audio_to_upipe(upipe_hls_audio);

    switch (event) {
    case UPROBE_NEED_OUTPUT: {
        struct uref *flow_def = va_arg(args, struct uref *);
        struct upipe *output = upipe_use(inner);

        /* AES decrypt ?
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
                    uprobe_use(&upipe_hls_audio->probe_aes_decrypt),
                    UPROBE_LOG_VERBOSE, "aes decrypt"));
            upipe_mgr_release(upipe_aes_decrypt_mgr);
            UBASE_ALLOC_RETURN(output);
        }

        //FIXME: handle ts format

        /** id3v2 pipe
         */
        struct upipe_mgr *upipe_id3v2_mgr = upipe_id3v2_mgr_alloc();
        if (unlikely(upipe_id3v2_mgr == NULL)) {
            upipe_release(output);
            return UBASE_ERR_ALLOC;
        }
        output = upipe_void_chain_output(
            output, upipe_id3v2_mgr,
            uprobe_pfx_alloc(uprobe_use(&upipe_hls_audio->probe_id3v2),
                             UPROBE_LOG_VERBOSE, "id3v2"));
        upipe_mgr_release(upipe_id3v2_mgr);
        UBASE_ALLOC_RETURN(output);

        /** aac framer
         */
        struct upipe_mgr *upipe_mpgaf_mgr = upipe_mpgaf_mgr_alloc();
        if (unlikely(upipe_mpgaf_mgr == NULL)) {
            upipe_release(output);
            return UBASE_ERR_ALLOC;
        }
        output = upipe_void_chain_output(
            output, upipe_mpgaf_mgr,
            uprobe_pfx_alloc(uprobe_use(&upipe_hls_audio->probe_last_inner),
                             UPROBE_LOG_VERBOSE, "mpgaf"));
        upipe_mgr_release(upipe_mpgaf_mgr);
        UBASE_ALLOC_RETURN(output);
        upipe_hls_audio_store_bin_output(upipe, output);
        return UBASE_ERR_NONE;
    }
    }

    return upipe_throw_proxy(upipe, inner, event, args);
}

static int probe_src(struct uprobe *uprobe, struct upipe *inner,
                     int event, va_list args)
{
    struct upipe_hls_audio *upipe_hls_audio =
        upipe_hls_audio_from_probe_src(uprobe);
    struct upipe *upipe = upipe_hls_audio_to_upipe(upipe_hls_audio);

    switch (event) {
    case UPROBE_NEED_OUTPUT: {
        /* m3u reader pipe
        */
        struct upipe_mgr *upipe_m3u_reader_mgr = upipe_m3u_reader_mgr_alloc();
        UBASE_ALLOC_RETURN(upipe_m3u_reader_mgr);
        struct upipe *output = upipe_void_alloc_output(
            inner, upipe_m3u_reader_mgr,
            uprobe_pfx_alloc(uprobe_use(&upipe_hls_audio->probe_reader),
                             UPROBE_LOG_VERBOSE, "m3u"));
        upipe_mgr_release(upipe_m3u_reader_mgr);
        UBASE_ALLOC_RETURN(output);

        /* playlist pipe
        */
        struct upipe_mgr *upipe_hls_playlist_mgr =
            upipe_hls_playlist_mgr_alloc();
        if (unlikely(upipe_hls_playlist_mgr == NULL)) {
            upipe_release(output);
            return UBASE_ERR_ALLOC;
        }
        output = upipe_void_chain_output(
            output, upipe_hls_playlist_mgr,
            uprobe_pfx_alloc(uprobe_use(&upipe_hls_audio->probe_playlist),
                             UPROBE_LOG_VERBOSE, "playlist"));
        upipe_mgr_release(upipe_hls_playlist_mgr);
        UBASE_ALLOC_RETURN(output);

        int ret = upipe_set_output_size(output, 1024);
        if (unlikely(!ubase_check(ret))) {
            upipe_release(output);
            return ret;
        }
        upipe_hls_audio_set_playlist(upipe, output);
        return UBASE_ERR_NONE;
    }
    }

    return upipe_throw_proxy(upipe, inner, event, args);
}

/** @internal @This allocates an audio hls variant pipe.
 *
 * @param mgr management structure for this pipe type
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return a pointer to the allocated pipe
 */
static struct upipe *upipe_hls_audio_alloc(struct upipe_mgr *mgr,
                                           struct uprobe *uprobe,
                                           uint32_t signature,
                                           va_list args)
{
    struct upipe *upipe =
        upipe_hls_audio_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_hls_audio_init_urefcount(upipe);
    upipe_hls_audio_init_urefcount_real(upipe);
    upipe_hls_audio_init_probe_id3v2(upipe);
    upipe_hls_audio_init_probe_src(upipe);
    upipe_hls_audio_init_probe_reader(upipe);
    upipe_hls_audio_init_probe_playlist(upipe);
    upipe_hls_audio_init_probe_aes_decrypt(upipe);
    upipe_hls_audio_init_probe_last_inner(upipe);
    upipe_hls_audio_init_src(upipe);
    upipe_hls_audio_init_playlist(upipe);
    upipe_hls_audio_init_bin_output(upipe);

    struct upipe_hls_audio *upipe_hls_audio = upipe_hls_audio_from_upipe(upipe);
    upipe_hls_audio->attach_uclock = false;
    upipe_hls_audio->source_mgr = NULL;

    upipe_throw_ready(upipe);

    return upipe;
}

/** @internal @This frees the pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_hls_audio_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_hls_audio_clean_bin_output(upipe);
    upipe_hls_audio_clean_probe_last_inner(upipe);
    upipe_hls_audio_clean_probe_id3v2(upipe);
    upipe_hls_audio_clean_probe_src(upipe);
    upipe_hls_audio_clean_probe_reader(upipe);
    upipe_hls_audio_clean_probe_playlist(upipe);
    upipe_hls_audio_clean_probe_aes_decrypt(upipe);
    upipe_hls_audio_clean_urefcount(upipe);
    upipe_hls_audio_clean_urefcount_real(upipe);
    upipe_hls_audio_free_void(upipe);
}

/** @internal @This is called when there is no external reference to the pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_hls_audio_no_ref(struct upipe *upipe)
{
    upipe_hls_audio_clean_playlist(upipe);
    upipe_hls_audio_clean_src(upipe);
    upipe_hls_audio_clean_last_inner(upipe);
    upipe_hls_audio_release_urefcount_real(upipe);
}

static int upipe_hls_audio_check_source_mgr(struct upipe *upipe)
{
    struct upipe_hls_audio *upipe_hls_audio = upipe_hls_audio_from_upipe(upipe);
    if (unlikely(upipe_hls_audio->source_mgr == NULL))
        return upipe_throw_need_source_mgr(upipe, &upipe_hls_audio->source_mgr);
    return UBASE_ERR_NONE;
}

/** @internal @This sets the playlist inner pipe.
 *
 * @param upipe description structure of the pipe
 * @param playlist the inner playlist pipe
 * @return an error code
 */
static int upipe_hls_audio_set_playlist(struct upipe *upipe,
                                        struct upipe *playlist)
{
    struct upipe_hls_audio *upipe_hls_audio = upipe_hls_audio_from_upipe(upipe);
    int ret;

    if (upipe_hls_audio->attach_uclock) {
        ret = upipe_attach_uclock(playlist);
        if (unlikely(!ubase_check(ret))) {
            upipe_release(playlist);
            return ret;
        }
    }
    upipe_hls_audio_store_playlist(upipe, playlist);
    return UBASE_ERR_NONE;
}

/** @internal @This sets the uri.
 *
 * @param upipe description structure of the pipe
 * @param uri the uri to set
 * @return an error code
 */
static int upipe_hls_audio_set_uri(struct upipe *upipe, const char *uri)
{
    struct upipe_hls_audio *upipe_hls_audio = upipe_hls_audio_from_upipe(upipe);

    upipe_hls_audio_clean_src(upipe);

    if (unlikely(uri == NULL))
        return UBASE_ERR_NONE;

    UBASE_RETURN(upipe_hls_audio_check_source_mgr(upipe));

    struct upipe *src = upipe_void_alloc(
        upipe_hls_audio->source_mgr,
        uprobe_pfx_alloc(uprobe_use(&upipe_hls_audio->probe_src),
                         UPROBE_LOG_VERBOSE, "src"));
    UBASE_ALLOC_RETURN(src);
    upipe_hls_audio_store_src(upipe, src);
    return upipe_set_uri(src, uri);
}

/** @internal @This attaches an uclock.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_hls_audio_attach_uclock(struct upipe *upipe)
{
    struct upipe_hls_audio *upipe_hls_audio = upipe_hls_audio_from_upipe(upipe);
    upipe_hls_audio->attach_uclock = true;
    if (upipe_hls_audio->playlist)
        return upipe_attach_uclock(upipe_hls_audio->playlist);
    return UBASE_ERR_NONE;
}

/** @internal @This dispatches commands to the pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to dispatch
 * @param args optional arguments
 * @return an error code
 */
static int upipe_hls_audio_control(struct upipe *upipe,
                                   int command, va_list args)
{
    switch (command) {
    case UPIPE_ATTACH_UCLOCK:
        return upipe_hls_audio_attach_uclock(upipe);

    case UPIPE_SET_URI: {
        const char *uri = va_arg(args, const char *);
        return upipe_hls_audio_set_uri(upipe, uri);
    }
    }

    if (command >= UPROBE_LOCAL) {
        switch (ubase_get_signature(args)) {
        case UPIPE_HLS_PLAYLIST_SIGNATURE:
            return upipe_hls_audio_control_playlist(upipe, command, args);
        }
    }
    return upipe_hls_audio_control_bin_output(upipe, command, args);
}

/** @internal @This is the static structure for manager. */
static struct upipe_mgr upipe_hls_audio_mgr = {
    .refcount = NULL,
    .signature = UPIPE_HLS_AUDIO_SIGNATURE,
    .upipe_alloc = upipe_hls_audio_alloc,
    .upipe_control = upipe_hls_audio_control,
};

/** @This returns the management structure.
 *
 * @return a pointer to the manager. 
 */
struct upipe_mgr *upipe_hls_audio_mgr_alloc(void)
{
    return &upipe_hls_audio_mgr;
}
