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
#include <upipe-hls/upipe_hls.h>
#include <upipe-hls/upipe_hls_playlist.h>

#include <upipe-modules/upipe_m3u_reader.h>

#include <upipe/upipe_helper_bin_output.h>
#include <upipe/upipe_helper_bin_input.h>
#include <upipe/upipe_helper_uprobe.h>
#include <upipe/upipe_helper_inner.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_urefcount_real.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe.h>

#include <upipe/uprobe_prefix.h>

#include <upipe/uref_m3u.h>
#include <upipe/uref_m3u_master.h>
#include <upipe/uref_uri.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_block.h>

#include <upipe/uclock.h>

#include <stdlib.h>
#include <stdarg.h>
#include <libgen.h>

/** @internal @This is the private context of a hls pipe. */
struct upipe_hls {
    /** public upipe structure */
    struct upipe upipe;
    /** urefcount management structure */
    struct urefcount urefcount;
    /** real urefcount management structure */
    struct urefcount urefcount_real;
    /** input request list */
    struct uchain input_requests;
    /** output request list */
    struct uchain output_requests;

    /** first inner proxy probe */
    struct uprobe probe_reader;
    /** master proxy probe */
    struct uprobe probe_master;

    /** m3u reader inner pipe */
    struct upipe *first_inner;
    /** last inner pipe (reader or master) */
    struct upipe *last_inner;
    /** output pipe if any */
    struct upipe *output;
};

/** @hidden */
static int probe_master(struct uprobe *uprobe, struct upipe *inner,
                        int event, va_list args);

/** @hidden */
static int probe_reader(struct uprobe *uprobe, struct upipe *inner,
                        int event, va_list args);

UPIPE_HELPER_UPIPE(upipe_hls, upipe, UPIPE_HLS_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_hls, urefcount, upipe_hls_no_ref);
UPIPE_HELPER_UREFCOUNT_REAL(upipe_hls, urefcount_real, upipe_hls_free);
UPIPE_HELPER_VOID(upipe_hls);
UPIPE_HELPER_UPROBE(upipe_hls, urefcount_real, probe_reader, probe_reader);
UPIPE_HELPER_UPROBE(upipe_hls, urefcount_real, probe_master, probe_master);
UPIPE_HELPER_INNER(upipe_hls, first_inner);
UPIPE_HELPER_INNER(upipe_hls, last_inner);
UPIPE_HELPER_BIN_INPUT(upipe_hls, first_inner, input_requests);
UPIPE_HELPER_BIN_OUTPUT(upipe_hls, last_inner, output, output_requests);

/** @internal @This handles inner master pipe events.
 *
 * @param uprobe structure used to raise events
 * @param inner pointer to inner pipe
 * @param event event thrown
 * @param args optional arguments
 * @return an error code
 */
static int probe_master(struct uprobe *uprobe, struct upipe *inner,
                        int event, va_list args)
{
    struct upipe_hls *upipe_hls = upipe_hls_from_probe_master(uprobe);
    struct upipe *upipe = upipe_hls_to_upipe(upipe_hls);

    switch (event) {
    case UPROBE_NEW_FLOW_DEF:
        return UBASE_ERR_NONE;
    case UPROBE_NEED_OUTPUT:
        return UBASE_ERR_INVALID;
    }
    return upipe_throw_proxy(upipe, inner, event, args);
}

/** @internal @This handles inner reader pipe events.
 *
 * @param uprobe structure used to raise events
 * @param inner pointer to inner pipe
 * @param event event thrown
 * @param args optional arguments
 * @return an error code
 */
static int probe_reader(struct uprobe *uprobe, struct upipe *inner,
                        int event, va_list args)
{
    struct upipe_hls *upipe_hls = upipe_hls_from_probe_reader(uprobe);
    struct upipe *upipe = upipe_hls_to_upipe(upipe_hls);

    if (event >= UPROBE_LOCAL)
        return UBASE_ERR_NONE;

    if (event == UPROBE_NEED_OUTPUT) {
        struct uref *flow_format = va_arg(args, struct uref *);
        const char *def;
        UBASE_RETURN(uref_flow_get_def(flow_format, &def));

        if (!strcmp(def, "block.m3u.playlist.")) {
            upipe_hls_store_bin_output(upipe, upipe_use(inner));
            return upipe_throw_need_output(upipe, flow_format);
        }
        else if (!strcmp(def, "block.m3u.master.")) {
            struct upipe_mgr *upipe_hls_master_mgr =
                upipe_hls_master_mgr_alloc();
            UBASE_ALLOC_RETURN(upipe_hls_master_mgr);
            struct upipe *output = upipe_void_alloc_output(
                inner, upipe_hls_master_mgr,
                uprobe_pfx_alloc(uprobe_use(&upipe_hls->probe_master),
                                 UPROBE_LOG_VERBOSE, "master"));
            upipe_mgr_release(upipe_hls_master_mgr);
            UBASE_ALLOC_RETURN(output);
            upipe_hls_store_bin_output(upipe, output);
            return upipe_set_output(inner, output);
        }
        else
            upipe_warn_va(upipe, "unsupported flow format %s", def);
        return UBASE_ERR_INVALID;
    }

    return upipe_throw_proxy(upipe, inner, event, args);
}

/** @internal @This allocates a hls pipe.
 *
 * @param mgr management structure for this pipe type
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to allocated pipe, or NULL in case of failure
 */
static struct upipe *upipe_hls_alloc(struct upipe_mgr *mgr,
                                     struct uprobe *uprobe,
                                     uint32_t signature,
                                     va_list args)
{
    struct upipe *upipe =
        upipe_hls_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_hls_init_urefcount(upipe);
    upipe_hls_init_urefcount_real(upipe);
    upipe_hls_init_probe_reader(upipe);
    upipe_hls_init_probe_master(upipe);
    upipe_hls_init_first_inner(upipe);
    upipe_hls_init_last_inner(upipe);
    upipe_hls_init_bin_input(upipe);
    upipe_hls_init_bin_output(upipe);

    upipe_throw_ready(upipe);

    struct upipe_hls *upipe_hls = upipe_hls_from_upipe(upipe);
    struct upipe *inner = NULL;
    struct upipe_mgr *upipe_m3u_reader_mgr = upipe_m3u_reader_mgr_alloc();
    if (likely(upipe_m3u_reader_mgr != NULL)) {
        inner = upipe_void_alloc(
            upipe_m3u_reader_mgr,
            uprobe_pfx_alloc(uprobe_use(&upipe_hls->probe_reader),
                             UPROBE_LOG_VERBOSE, "reader"));
        upipe_mgr_release(upipe_m3u_reader_mgr);
    }
    upipe_hls_store_bin_input(upipe, inner);

    return upipe;
}

/** @internal @This frees the pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_hls_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_hls_clean_bin_output(upipe);
    upipe_hls_clean_bin_input(upipe);
    upipe_hls_clean_probe_master(upipe);
    upipe_hls_clean_probe_reader(upipe);
    upipe_hls_clean_urefcount(upipe);
    upipe_hls_clean_urefcount_real(upipe);
    upipe_hls_free_void(upipe);
}

/** @internal @This is called when there is no external reference to the pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_hls_no_ref(struct upipe *upipe)
{
    upipe_hls_clean_first_inner(upipe);
    upipe_hls_clean_last_inner(upipe);
    upipe_hls_release_urefcount_real(upipe);
}

/** @internal @This dispatches commands to the pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to dispatch
 * @param args optional arguments
 * @return an error code
 */
static int upipe_hls_control(struct upipe *upipe, int command, va_list args)
{
    if (command > UPIPE_CONTROL_LOCAL) {
        switch (ubase_get_signature(args)) {
        case UPIPE_HLS_MASTER_SIGNATURE:
            return upipe_hls_control_last_inner(upipe, command, args);
        }
    }

    if (!ubase_check(upipe_hls_control_bin_input(upipe, command, args)))
        return upipe_hls_control_bin_output(upipe, command, args);
    return UBASE_ERR_NONE;
}

/** @internal @This is the static structure for hls pipe manager. */
static struct upipe_mgr upipe_hls_mgr = {
    .refcount = NULL,
    .signature = UPIPE_HLS_SIGNATURE,
    .upipe_alloc = upipe_hls_alloc,
    .upipe_input = upipe_hls_bin_input,
    .upipe_control = upipe_hls_control,
    .upipe_mgr_control = NULL,
};

/** @This returns the management structure for hls pipes.
 *
 * @return the pipe manager
 */
struct upipe_mgr *upipe_hls_mgr_alloc(void)
{
    return &upipe_hls_mgr;
}
