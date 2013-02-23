/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
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
#include <upipe/upipe.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
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

/** upipe_multicat_sink structure */ 
struct upipe_multicat_sink {
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

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_multicat_sink, upipe);

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
    return upipe_fsink_set_path(upipe_multicat_sink->fsink, filepath, upipe_multicat_sink->mode);
}

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void _upipe_multicat_sink_input(struct upipe *upipe, struct uref *uref,
                                       struct upump *upump)
{
    struct upipe_multicat_sink *upipe_multicat_sink = upipe_multicat_sink_from_upipe(upipe);
    uint64_t systime = 0;
    int64_t newidx;

    if (unlikely(!uref_clock_get_systime(uref, &systime))) {
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

    upipe_input(upipe_multicat_sink->fsink, uref, upump);
}

/** @internal @This handles urefs (data & flows).
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_multicat_sink_input(struct upipe *upipe, struct uref *uref,
                                      struct upump *upump)
{
    struct upipe_multicat_sink *upipe_multicat_sink = upipe_multicat_sink_from_upipe(upipe);
    const char *def;

    if (unlikely(uref_flow_get_def(uref, &def))) {
        upipe_dbg_va(upipe, "flow definition: %s", def);
        if (upipe_multicat_sink->flow_def != NULL) {
            uref_free(upipe_multicat_sink->flow_def);
            upipe_multicat_sink->flow_def = NULL;
        }
        if (unlikely(ubase_ncmp(def, "block."))) {
            upipe_throw_flow_def_error(upipe, uref);
            uref_free(uref);
            return;
        }
        upipe_multicat_sink->flow_def = uref_dup(uref);
        if (unlikely(upipe_multicat_sink->flow_def == NULL)) {
            upipe_throw_aerror(upipe);
            return;
        }
        upipe_input(upipe_multicat_sink->fsink, uref, upump);
        return;
    }

    if (unlikely(upipe_multicat_sink->flow_def == NULL)) {
        upipe_throw_flow_def_error(upipe, uref);
        uref_free(uref);
        return;
    }

    if (unlikely(uref->ubuf == NULL)) {
        uref_free(uref);
        return;
    }

    return _upipe_multicat_sink_input(upipe, uref, upump);
}

/** @internal @This allocates multicat_sink output (fsink)
 *
 * @param upipe description structure of the pipe
 * @return false in case of error
 */
static bool _upipe_multicat_sink_output_alloc(struct upipe *upipe)
{
    struct upipe *fsink = NULL;
    struct upipe_multicat_sink *upipe_multicat_sink = upipe_multicat_sink_from_upipe(upipe);
    if (!upipe_multicat_sink->fsink_mgr) {
        upipe_err(upipe, "fsink manager required");
        return false;
    }
    fsink = upipe_alloc(upipe_multicat_sink->fsink_mgr,
                        uprobe_pfx_adhoc_alloc_va(upipe->uprobe, UPROBE_LOG_NOTICE, "fsink"));
    if (unlikely(!fsink)) {
        upipe_throw_aerror(upipe);
        return false;
    }
    if (upipe_multicat_sink->flow_def != NULL) {
        struct uref *uref = uref_dup(upipe_multicat_sink->flow_def);
        if (unlikely(uref == NULL)) {
            upipe_release(fsink);
            upipe_throw_aerror(upipe);
            return false;
        }
        upipe_input(fsink, uref, NULL);
    }
    upipe_multicat_sink->fsink = fsink;
	return true;
}

/** @internal @This is called by _control to change dirpath/suffix
 *
 * @param upipe description structure of the pipe
 * @param path directory path (or prefix)
 * @param suffix file suffix
 * @return false in case of error
 */
static bool _upipe_multicat_sink_set_path(struct upipe *upipe, const char *path, const char *suffix)
{
    struct upipe_multicat_sink *upipe_multicat_sink = upipe_multicat_sink_from_upipe(upipe);
    if (unlikely(!upipe_multicat_sink->fsink)) {
        if (unlikely(!_upipe_multicat_sink_output_alloc(upipe))) {
            return false;
        };
    }

    free(upipe_multicat_sink->dirpath);
    free(upipe_multicat_sink->suffix);
    upipe_multicat_sink->fileidx = -1;

    if (unlikely(!path || !suffix)) {
        upipe_notice(upipe, "setting NULL fsink path");
        upipe_multicat_sink->dirpath = NULL;
        upipe_multicat_sink->suffix = NULL;
        upipe_fsink_set_path(upipe_multicat_sink->fsink, NULL, UPIPE_FSINK_APPEND);
        return true;
    }

    upipe_multicat_sink->dirpath = strndup(path, MAXPATHLEN);
    upipe_multicat_sink->suffix = strndup(suffix, MAXPATHLEN);
    upipe_notice_va(upipe, "setting path and suffix: %s %s", path, suffix);
    return true;
}

/** @internal @This changes the rotate interval
 *
 * @param upipe description structure of the pipe
 * @param rotate new rotate interval
 * @return false in case of error
 */
static bool  _upipe_multicat_sink_set_rotate(struct upipe *upipe, uint64_t rotate)
{
    struct upipe_multicat_sink *upipe_multicat_sink = upipe_multicat_sink_from_upipe(upipe);
    if (unlikely(rotate < 2)) {
        upipe_warn_va(upipe, "invalid rotate interval (%"PRIu64" < 2)", rotate);
        return false;
    }
    upipe_multicat_sink->rotate = rotate;
    upipe_notice_va(upipe, "setting rotate: %"PRIu64, rotate);
    return true;
}

/** @internal @This returns the current fsink manager
 *
 * @param upipe description structure of the pipe
 * @param rotate_p filled in with the current rotate interval
 * @return false in case of error
 */
static bool _upipe_multicat_sink_get_fsink_mgr(struct upipe *upipe, struct upipe_mgr **fsink_mgr)
{
    struct upipe_multicat_sink *upipe_multicat_sink = upipe_multicat_sink_from_upipe(upipe);
    assert(fsink_mgr != NULL);
    *fsink_mgr = upipe_multicat_sink->fsink_mgr;
    return true;
}

/** @internal @This returns the current rotate interval
 *
 * @param upipe description structure of the pipe
 * @param rotate_p filled in with the current rotate interval
 * @return false in case of error
 */
static bool _upipe_multicat_sink_get_rotate(struct upipe *upipe, uint64_t *rotate_p)
{
    struct upipe_multicat_sink *upipe_multicat_sink = upipe_multicat_sink_from_upipe(upipe);
    assert(rotate_p != NULL);
    *rotate_p = upipe_multicat_sink->rotate;
    return true;
}

/** @internal @This returns the current dirpath/suffix
 *
 * @param upipe description structure of the pipe
 * @param path_p filled in with the current rotate interval
 * @param suffix_p filled in with the current rotate interval
 * @return false in case of error
 */
static bool _upipe_multicat_sink_get_path(struct upipe *upipe, char **path_p, char **suffix_p)
{
    struct upipe_multicat_sink *upipe_multicat_sink = upipe_multicat_sink_from_upipe(upipe);
    if (path_p) {
        *path_p = upipe_multicat_sink->dirpath;
    }
    if (suffix_p) {
        *suffix_p = upipe_multicat_sink->suffix;
    }
    return true;
}

/** @internal @This pushes the given upump manager to the (fsink) output
 *
 * @param upipe description structure of the pipe
 * @param upump_mgr upump manager
 * @return false in case of error
 */
static bool _upipe_multicat_sink_set_upump_mgr(struct upipe *upipe, struct upump_mgr *upump_mgr)
{
    struct upipe_multicat_sink *upipe_multicat_sink = upipe_multicat_sink_from_upipe(upipe);
    if (! upipe_multicat_sink->fsink) {
        if (unlikely(!_upipe_multicat_sink_output_alloc(upipe))) {
            return false;
        }
    }
    return upipe_set_upump_mgr(upipe_multicat_sink->fsink, upump_mgr);
}

/** @internal @This pushes the given uclock to the (fsink) output
 *
 * @param upipe description structure of the pipe
 * @param uclock uclock
 * @return false in case of error
 */
static bool _upipe_multicat_sink_set_uclock(struct upipe *upipe, struct uclock *uclock)
{
    struct upipe_multicat_sink *upipe_multicat_sink = upipe_multicat_sink_from_upipe(upipe);
    if (! upipe_multicat_sink->fsink) {
        if (unlikely(!_upipe_multicat_sink_output_alloc(upipe))) {
            return false;
        }
    }
    return upipe_set_uclock(upipe_multicat_sink->fsink, uclock);
}

/** @internal @This processes control commands on a file source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_multicat_sink_control(struct upipe *upipe, enum upipe_command command,
                               va_list args)
{
    struct upipe_multicat_sink *upipe_multicat_sink = upipe_multicat_sink_from_upipe(upipe);
    switch (command) {
        case UPIPE_GET_UPUMP_MGR: {
            if (! upipe_multicat_sink->fsink) return false;
            struct upump_mgr **p = va_arg(args, struct upump_mgr **);
            return upipe_get_upump_mgr(upipe_multicat_sink->fsink, p);
        }
        case UPIPE_SET_UPUMP_MGR: {
            struct upump_mgr *upump_mgr = va_arg(args, struct upump_mgr *);
            return _upipe_multicat_sink_set_upump_mgr(upipe, upump_mgr);
        }
        case UPIPE_GET_UCLOCK: {
            if (! upipe_multicat_sink->fsink) return false;
            struct uclock **p = va_arg(args, struct uclock **);
            return upipe_get_uclock(upipe_multicat_sink->fsink, p);
        }
        case UPIPE_SET_UCLOCK: {
            struct uclock *uclock = va_arg(args, struct uclock *);
            return _upipe_multicat_sink_set_uclock(upipe, uclock);
        }

		case UPIPE_MULTICAT_SINK_SET_MODE: {
			unsigned int signature = va_arg(args, unsigned int); 
			assert(signature == UPIPE_MULTICAT_SINK_SIGNATURE);
			upipe_multicat_sink->mode = va_arg(args, enum upipe_fsink_mode);
			return true;
		}
        case UPIPE_MULTICAT_SINK_SET_ROTATE: {
			unsigned int signature = va_arg(args, unsigned int); 
			assert(signature == UPIPE_MULTICAT_SINK_SIGNATURE);
            return _upipe_multicat_sink_set_rotate(upipe, va_arg(args, uint64_t));
        }
        case UPIPE_MULTICAT_SINK_GET_ROTATE: {
			unsigned int signature = va_arg(args, unsigned int); 
			assert(signature == UPIPE_MULTICAT_SINK_SIGNATURE);
            return _upipe_multicat_sink_get_rotate(upipe, va_arg(args, uint64_t*));
        }
        case UPIPE_MULTICAT_SINK_SET_FSINK_MGR: {
			unsigned int signature = va_arg(args, unsigned int); 
			assert(signature == UPIPE_MULTICAT_SINK_SIGNATURE);
            upipe_multicat_sink->fsink_mgr = va_arg(args, struct upipe_mgr*);
            return true;
        }
        case UPIPE_MULTICAT_SINK_GET_FSINK_MGR: {
			unsigned int signature = va_arg(args, unsigned int); 
			assert(signature == UPIPE_MULTICAT_SINK_SIGNATURE);
            return _upipe_multicat_sink_get_fsink_mgr(upipe, va_arg(args, struct upipe_mgr **));
        }
        case UPIPE_MULTICAT_SINK_SET_PATH: {
			unsigned int signature = va_arg(args, unsigned int); 
			assert(signature == UPIPE_MULTICAT_SINK_SIGNATURE);
            const char *path = va_arg(args, const char *);
            const char *ext = va_arg(args, const char *);
            return _upipe_multicat_sink_set_path(upipe, path, ext);
        }
        case UPIPE_MULTICAT_SINK_GET_PATH: {
			unsigned int signature = va_arg(args, unsigned int); 
			assert(signature == UPIPE_MULTICAT_SINK_SIGNATURE);
            return _upipe_multicat_sink_get_path(upipe, va_arg(args, char **), va_arg(args, char **));
        }
        default:
            upipe_warn(upipe, "invalid command");
            return false;
    }
}

/** @internal @This allocates a multicat_sink pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_multicat_sink_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe)
{
    struct upipe_multicat_sink *upipe_multicat_sink = malloc(sizeof(struct upipe_multicat_sink));
    if (unlikely(upipe_multicat_sink == NULL))
        return NULL;
    struct upipe *upipe = upipe_multicat_sink_to_upipe(upipe_multicat_sink);
    upipe_init(upipe, mgr, uprobe);
    upipe_multicat_sink->flow_def = NULL;
    upipe_multicat_sink->fsink = NULL;
    upipe_multicat_sink->fsink_mgr = NULL;
    upipe_multicat_sink->dirpath = NULL;
    upipe_multicat_sink->suffix = NULL;
    upipe_multicat_sink->fileidx = -1;
    upipe_multicat_sink->rotate = UPIPE_MULTICAT_SINK_DEF_ROTATE;
    upipe_multicat_sink->mode = UPIPE_FSINK_APPEND;
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
    upipe_clean(upipe);
    free(upipe_multicat_sink);
}

static struct upipe_mgr upipe_multicat_sink_mgr = {
    .signature = UPIPE_MULTICAT_SINK_SIGNATURE,

    .upipe_alloc = upipe_multicat_sink_alloc,
    .upipe_input = upipe_multicat_sink_input,
    .upipe_control = upipe_multicat_sink_control,
    .upipe_free = upipe_multicat_sink_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for multicat_sink pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_multicat_sink_mgr_alloc(void)
{
    return &upipe_multicat_sink_mgr;
}
