/*
 * Copyright (C) 2013-2014 OpenHeadend S.A.R.L.
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
 * @short Upipe null module - free incoming urefs
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_dump.h>
#include <upipe/upipe.h>
#include <upipe/udict.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe-modules/upipe_null.h>

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


/** upipe_null structure */
struct upipe_null {
    /** refcount management structure */
    struct urefcount urefcount;

    /** dump dict */
    bool dump;
    /** counter */
    uint64_t counter;
    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_null, upipe, UPIPE_NULL_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_null, urefcount, upipe_null_free)
UPIPE_HELPER_VOID(upipe_null);

/** @internal @This allocates a null pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_null_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_null_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_null *upipe_null = upipe_null_from_upipe(upipe);
    upipe_null_init_urefcount(upipe);
    upipe_null->dump = false;
    upipe_null->counter = 0;
    upipe_throw_ready(&upipe_null->upipe);
    return &upipe_null->upipe;
}

/** @internal @This sends data to devnull.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_null_input(struct upipe *upipe, struct uref *uref, struct upump **upump_p)
{
    struct upipe_null *upipe_null = upipe_null_from_upipe(upipe);
    upipe_verbose(upipe, "sending uref to devnull");
    upipe_null->counter++;
    if (upipe_null->dump)
        uref_dump(uref, upipe->uprobe);
    uref_free(uref);
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_null_control(struct upipe *upipe, int command, va_list args)
{
    struct upipe_null *upipe_null = upipe_null_from_upipe(upipe);

    UBASE_HANDLED_RETURN(upipe_control_provide_request(upipe, command, args));

    switch (command) {
        case UPIPE_SET_FLOW_DEF:
            return UBASE_ERR_NONE;

        case UPIPE_NULL_DUMP_DICT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_NULL_SIGNATURE)
            upipe_null->dump = (va_arg(args, int) == 1 ? true : false);
            return UBASE_ERR_NONE;
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_null_free(struct upipe *upipe)
{
    struct upipe_null *upipe_null = upipe_null_from_upipe(upipe);
    upipe_warn_va(upipe, "freed %"PRIu64" packets", upipe_null->counter);
    upipe_throw_dead(upipe);
    upipe_null_clean_urefcount(upipe);
    upipe_null_free_void(upipe);
}

/** upipe_null (/dev/null) */
static struct upipe_mgr upipe_null_mgr = {
    .refcount = NULL,
    .signature = UPIPE_NULL_SIGNATURE,

    .upipe_alloc = upipe_null_alloc,
    .upipe_input = upipe_null_input,
    .upipe_control = upipe_null_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for null pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_null_mgr_alloc(void)
{
    return &upipe_null_mgr;
}
