/*
 * Copyright (C) 2012-2015 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
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
 * @short Upipe module - multicat file sink
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uclock.h>
#include <upipe/uref_clock.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_void.h>
#include <upipe-modules/upipe_multicat_sink.h>
#include <upipe-modules/upipe_file_sink.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <assert.h>
#include <sys/param.h>

#define EXPECTED_FLOW_DEF "block."

/** upipe_multicat_sink structure */ 
struct upipe_multicat_sink {
    /** refcount management structure */
    struct urefcount urefcount;

    /** input flow */
    struct uref *flow_def;

    /** fsink subpipe */
    struct upipe *fsink;
    /** fsink manager */
    struct upipe_mgr *fsink_mgr;

    /** directory path */
    char *dirpath;
    /** file suffix */
    char *suffix;

    /** file index */
    int64_t fileidx;

    /** rotate interval */
    uint64_t rotate;

    /** file opening mode */
    enum upipe_fsink_mode mode;
    /** sync period */
    uint64_t sync_period;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_multicat_sink, upipe, UPIPE_MULTICAT_SINK_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_multicat_sink, urefcount, upipe_multicat_sink_free)
UPIPE_HELPER_VOID(upipe_multicat_sink)

/** @internal @This generates a path from idx and send set_path to the internal
 * (fsink) output
 *
 * @param upipe description structure of the pipe
 * @param idx new file index
 * @return false in case of error
 */
static bool _upipe_multicat_sink_change_file(struct upipe *upipe, int64_t idx)
{
    struct upipe_multicat_sink *upipe_multicat_sink = upipe_multicat_sink_from_upipe(upipe);
    char filepath[MAXPATHLEN];
    if (unlikely(! (upipe_multicat_sink->dirpath
                      && upipe_multicat_sink->suffix && upipe_multicat_sink->fsink) )) {
        upipe_warn(upipe, "call set_path first !");
        return false;
    }
    snprintf(filepath, MAXPATHLEN, "%s%"PRId64"%s", upipe_multicat_sink->dirpath, idx, upipe_multicat_sink->suffix);
    if (!ubase_check(upipe_fsink_set_path(upipe_multicat_sink->fsink, filepath, upipe_multicat_sink->mode)))
        return false;
    if (upipe_multicat_sink->sync_period)
        upipe_fsink_set_sync_period(upipe_multicat_sink->fsink,
                                    upipe_multicat_sink->sync_period);
    return true;
}

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_multicat_sink_input(struct upipe *upipe, struct uref *uref,
                                      struct upump **upump_p)
{
    struct upipe_multicat_sink *upipe_multicat_sink = upipe_multicat_sink_from_upipe(upipe);
    uint64_t systime = 0;
    int64_t newidx;

    if (unlikely(!ubase_check(uref_clock_get_cr_sys(uref, &systime)))) {
        upipe_warn(upipe, "uref has no cr_sys, dropping");
        uref_free(uref);
        return;
    }
    newidx = (systime/upipe_multicat_sink->rotate);
    if (upipe_multicat_sink->fileidx != newidx) {
        if (unlikely(! _upipe_multicat_sink_change_file(upipe, newidx))) {
            upipe_warn(upipe, "couldnt change file path");
            uref_free(uref);
            return;
        }
        upipe_multicat_sink->fileidx = newidx;
    }

    upipe_input(upipe_multicat_sink->fsink, uref, upump_p);
}

/** @internal @This allocates multicat_sink output (fsink)
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int _upipe_multicat_sink_output_alloc(struct upipe *upipe)
{
    struct upipe *fsink = NULL;
    struct upipe_multicat_sink *upipe_multicat_sink = upipe_multicat_sink_from_upipe(upipe);
    if (!upipe_multicat_sink->fsink_mgr) {
        upipe_err(upipe, "fsink manager required");
        return UBASE_ERR_UNHANDLED;
    }
    fsink = upipe_void_alloc(upipe_multicat_sink->fsink_mgr,
                             uprobe_pfx_alloc_va(uprobe_use(upipe->uprobe),
                                                 UPROBE_LOG_NOTICE, "fsink"));
    if (unlikely(!fsink)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    int err;
    if (upipe_multicat_sink->flow_def != NULL &&
        (err = upipe_set_flow_def(fsink, upipe_multicat_sink->flow_def)) !=
        UBASE_ERR_NONE) {
        upipe_warn(upipe, "set_flow_def failed");
        return err;
    }
    upipe_multicat_sink->fsink = fsink;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_multicat_sink_set_flow_def(struct upipe *upipe,
                                            struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    struct upipe_multicat_sink *upipe_multicat_sink =
        upipe_multicat_sink_from_upipe(upipe);
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))
    struct uref *flow_def_dup;
    if ((flow_def_dup = uref_dup(flow_def)) == NULL)
        return UBASE_ERR_NONE;
    if (upipe_multicat_sink->flow_def != NULL)
        uref_free(upipe_multicat_sink->flow_def);
    upipe_multicat_sink->flow_def = flow_def_dup;
    if (upipe_multicat_sink->fsink != NULL)
        return upipe_set_flow_def(upipe_multicat_sink->fsink,
                                  upipe_multicat_sink->flow_def);
    return UBASE_ERR_NONE;
}

/** @internal @This is called by _control to change dirpath/suffix
 *
 * @param upipe description structure of the pipe
 * @param path directory path (or prefix)
 * @param suffix file suffix
 * @return an error code
 */
static int _upipe_multicat_sink_set_path(struct upipe *upipe, const char *path, const char *suffix)
{
    struct upipe_multicat_sink *upipe_multicat_sink = upipe_multicat_sink_from_upipe(upipe);
    if (unlikely(!upipe_multicat_sink->fsink)) {
        UBASE_RETURN(_upipe_multicat_sink_output_alloc(upipe));
    }

    free(upipe_multicat_sink->dirpath);
    free(upipe_multicat_sink->suffix);
    upipe_multicat_sink->fileidx = -1;

    if (unlikely(!path || !suffix)) {
        upipe_notice(upipe, "setting NULL fsink path");
        upipe_multicat_sink->dirpath = NULL;
        upipe_multicat_sink->suffix = NULL;
        upipe_fsink_set_path(upipe_multicat_sink->fsink, NULL, UPIPE_FSINK_APPEND);
        return UBASE_ERR_NONE;
    }

    upipe_multicat_sink->dirpath = strndup(path, MAXPATHLEN);
    upipe_multicat_sink->suffix = strndup(suffix, MAXPATHLEN);
    upipe_notice_va(upipe, "setting path and suffix: %s %s", path, suffix);
    return UBASE_ERR_NONE;
}

/** @internal @This changes the rotate interval
 *
 * @param upipe description structure of the pipe
 * @param rotate new rotate interval
 * @return an error code
 */
static int  _upipe_multicat_sink_set_rotate(struct upipe *upipe,
        uint64_t rotate)
{
    struct upipe_multicat_sink *upipe_multicat_sink = upipe_multicat_sink_from_upipe(upipe);
    if (unlikely(rotate < 2)) {
        upipe_warn_va(upipe, "invalid rotate interval (%"PRIu64" < 2)", rotate);
        return UBASE_ERR_INVALID;
    }
    upipe_multicat_sink->rotate = rotate;
    upipe_notice_va(upipe, "setting rotate: %"PRIu64, rotate);
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current fsink manager
 *
 * @param upipe description structure of the pipe
 * @param rotate_p filled in with the current rotate interval
 * @return an error code
 */
static int _upipe_multicat_sink_get_fsink_mgr(struct upipe *upipe,
        struct upipe_mgr **fsink_mgr)
{
    struct upipe_multicat_sink *upipe_multicat_sink = upipe_multicat_sink_from_upipe(upipe);
    assert(fsink_mgr != NULL);
    *fsink_mgr = upipe_multicat_sink->fsink_mgr;
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current rotate interval
 *
 * @param upipe description structure of the pipe
 * @param rotate_p filled in with the current rotate interval
 * @return an error code
 */
static int _upipe_multicat_sink_get_rotate(struct upipe *upipe,
        uint64_t *rotate_p)
{
    struct upipe_multicat_sink *upipe_multicat_sink = upipe_multicat_sink_from_upipe(upipe);
    assert(rotate_p != NULL);
    *rotate_p = upipe_multicat_sink->rotate;
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current dirpath/suffix
 *
 * @param upipe description structure of the pipe
 * @param path_p filled in with the current rotate interval
 * @param suffix_p filled in with the current rotate interval
 * @return an error code
 */
static int _upipe_multicat_sink_get_path(struct upipe *upipe,
        char **path_p, char **suffix_p)
{
    struct upipe_multicat_sink *upipe_multicat_sink = upipe_multicat_sink_from_upipe(upipe);
    if (path_p) {
        *path_p = upipe_multicat_sink->dirpath;
    }
    if (suffix_p) {
        *suffix_p = upipe_multicat_sink->suffix;
    }
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a file source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_multicat_sink_control(struct upipe *upipe,
                                       int command, va_list args)
{
    struct upipe_multicat_sink *upipe_multicat_sink =
        upipe_multicat_sink_from_upipe(upipe);
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_multicat_sink_set_flow_def(upipe, flow_def);
        }
        case UPIPE_REGISTER_REQUEST:
        case UPIPE_UNREGISTER_REQUEST:
        case UPIPE_ATTACH_UREF_MGR:
        case UPIPE_ATTACH_UPUMP_MGR:
        case UPIPE_ATTACH_UBUF_MGR:
        case UPIPE_ATTACH_UCLOCK:
            if (!upipe_multicat_sink->fsink) {
                UBASE_RETURN(_upipe_multicat_sink_output_alloc(upipe));
            }
            return upipe_control_va(upipe_multicat_sink->fsink, command, args);

        case UPIPE_MULTICAT_SINK_SET_MODE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_MULTICAT_SINK_SIGNATURE)
            upipe_multicat_sink->mode = va_arg(args, enum upipe_fsink_mode);
            return UBASE_ERR_NONE;
        }
        case UPIPE_MULTICAT_SINK_SET_ROTATE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_MULTICAT_SINK_SIGNATURE)
            return _upipe_multicat_sink_set_rotate(upipe, va_arg(args, uint64_t));
        }
        case UPIPE_MULTICAT_SINK_GET_ROTATE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_MULTICAT_SINK_SIGNATURE)
            return _upipe_multicat_sink_get_rotate(upipe, va_arg(args, uint64_t*));
        }
        case UPIPE_MULTICAT_SINK_SET_FSINK_MGR: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_MULTICAT_SINK_SIGNATURE)
            upipe_multicat_sink->fsink_mgr = va_arg(args, struct upipe_mgr*);
            return UBASE_ERR_NONE;
        }
        case UPIPE_MULTICAT_SINK_GET_FSINK_MGR: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_MULTICAT_SINK_SIGNATURE)
            return _upipe_multicat_sink_get_fsink_mgr(upipe, va_arg(args, struct upipe_mgr **));
        }
        case UPIPE_MULTICAT_SINK_SET_PATH: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_MULTICAT_SINK_SIGNATURE)
            const char *path = va_arg(args, const char *);
            const char *ext = va_arg(args, const char *);
            return _upipe_multicat_sink_set_path(upipe, path, ext);
        }
        case UPIPE_MULTICAT_SINK_GET_PATH: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_MULTICAT_SINK_SIGNATURE)
            return _upipe_multicat_sink_get_path(upipe, va_arg(args, char **), va_arg(args, char **));
        }
        case UPIPE_FSINK_SET_SYNC_PERIOD: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_FSINK_SIGNATURE)
            uint64_t sync_period = va_arg(args, uint64_t);
            upipe_multicat_sink->sync_period = sync_period;
            if (upipe_multicat_sink->fsink != NULL)
                return upipe_control_va(upipe_multicat_sink->fsink,
                                        command, args);
            return UBASE_ERR_NONE;
        }
        case UPIPE_FSINK_GET_SYNC_PERIOD: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_FSINK_SIGNATURE)
            uint64_t *p = va_arg(args, uint64_t *);
            *p = upipe_multicat_sink->sync_period;
            return UBASE_ERR_NONE;
        }
        default:
            if (upipe_multicat_sink->fsink != NULL)
                return upipe_control_va(upipe_multicat_sink->fsink,
                                        command, args);
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This allocates a multicat_sink pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_multicat_sink_alloc(struct upipe_mgr *mgr,
                                               struct uprobe *uprobe,
                                               uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_multicat_sink_alloc_void(mgr, uprobe, signature,
                                                         args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_multicat_sink *upipe_multicat_sink =
        upipe_multicat_sink_from_upipe(upipe);
    upipe_init(upipe, mgr, uprobe);
    upipe_multicat_sink_init_urefcount(upipe);
    upipe_multicat_sink->flow_def = NULL;
    upipe_multicat_sink->fsink = NULL;
    upipe_multicat_sink->fsink_mgr = NULL;
    upipe_multicat_sink->dirpath = NULL;
    upipe_multicat_sink->suffix = NULL;
    upipe_multicat_sink->fileidx = -1;
    upipe_multicat_sink->rotate = UPIPE_MULTICAT_SINK_DEF_ROTATE;
    upipe_multicat_sink->mode = UPIPE_FSINK_APPEND;
    upipe_multicat_sink->sync_period = 0;
    upipe_multicat_sink->flow_def = NULL;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_multicat_sink_free(struct upipe *upipe)
{
    struct upipe_multicat_sink *upipe_multicat_sink = upipe_multicat_sink_from_upipe(upipe);
    if (upipe_multicat_sink->flow_def != NULL)
        uref_free(upipe_multicat_sink->flow_def);
    if (upipe_multicat_sink->fsink != NULL)
        upipe_release(upipe_multicat_sink->fsink);

    upipe_dbg_va(upipe, "releasing pipe %p", upipe);
    upipe_throw_dead(upipe);

    free(upipe_multicat_sink->dirpath);
    free(upipe_multicat_sink->suffix);
    upipe_multicat_sink_clean_urefcount(upipe);
    upipe_multicat_sink_free_void(upipe);
}

static struct upipe_mgr upipe_multicat_sink_mgr = {
    .refcount = NULL,
    .signature = UPIPE_MULTICAT_SINK_SIGNATURE,

    .upipe_alloc = upipe_multicat_sink_alloc,
    .upipe_input = upipe_multicat_sink_input,
    .upipe_control = upipe_multicat_sink_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for multicat_sink pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_multicat_sink_mgr_alloc(void)
{
    return &upipe_multicat_sink_mgr;
}
