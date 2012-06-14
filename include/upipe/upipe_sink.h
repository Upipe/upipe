/*****************************************************************************
 * upipe_sink.h: common declarations of sink pipes
 *****************************************************************************
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
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
 *****************************************************************************/

#ifndef _UPIPE_UPIPE_SINK_H_
/** @hidden */
#define _UPIPE_UPIPE_SINK_H_

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/uclock.h>
#include <upipe/upump.h>
#include <upipe/upipe.h>

#include <stdarg.h>
#include <assert.h>

/** super-set of the upipe structure with additional members */
struct upipe_sink {
    /** uclock structure, if not NULL we are in live mode */
    struct uclock *uclock;
    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** write watcher */
    struct upump *upump;
    /** delay applied to systime attribute when uclock is provided */
    uint64_t delay;

    /** sinks are a special case of pipes */
    struct upipe upipe;
};

/** @This returns the high-level upipe structure.
 *
 * @param upipe_sink pointer to the upipe_sink structure
 * @return pointer to the upipe structure
 */
static inline struct upipe *upipe_sink_to_upipe(struct upipe_sink *upipe_sink)
{
    return &upipe_sink->upipe;
}

/** @This returns the private struct upipe_sink structure.
 *
 * @param upipe description structure of the pipe
 * @return pointer to the upipe_sink structure
 */
static inline struct upipe_sink *upipe_sink_from_upipe(struct upipe *upipe)
{
    return container_of(upipe, struct upipe_sink, upipe);
}

UPIPE_STRUCT_TEMPLATE(sink, uclock, struct uclock *)
UPIPE_STRUCT_TEMPLATE(sink, upump_mgr, struct upump_mgr *)
UPIPE_STRUCT_TEMPLATE(sink, upump, struct upump *)
UPIPE_STRUCT_TEMPLATE(sink, delay, uint64_t)

/** @This sets the upump to use.
 *
 * @param upipe description structure of the pipe
 * @param upump upump structure to use
 */
static inline void upipe_sink_set_upump(struct upipe *upipe,
                                        struct upump *upump)
{
    struct upipe_sink *upipe_sink = upipe_sink_from_upipe(upipe);
    if (unlikely(upipe_sink->upump != NULL)) {
        upump_stop(upipe_sink->upump);
        upump_free(upipe_sink->upump);
    }
    upipe_sink->upump = upump;
}

/** @This checks if the sink pipe is ready to process data.
 *
 * @param upipe description structure of the pipe
 */
static inline bool upipe_sink_ready(struct upipe *upipe)
{
    struct upipe_sink *upipe_sink = upipe_sink_from_upipe(upipe);
    return upipe_sink->upump_mgr != NULL;
}

/** @This initializes the common members of sink pipes.
 *
 * @param upipe description structure of the pipe
 * @param delay default systime delay
 */
static inline void upipe_sink_init(struct upipe *upipe, uint64_t delay)
{
    struct upipe_sink *upipe_sink = upipe_sink_from_upipe(upipe);
    upipe_sink->uclock = NULL;
    upipe_sink->upump_mgr = NULL;
    upipe_sink->upump = NULL;
    upipe_sink->delay = delay;
}

/** @internal @This returns the uclock structure.
 *
 * @param upipe description structure of the pipe
 * @param uclock_p reference to a value, will be modified (or NULL-ed)
 * @return false in case of error
 */
static bool _upipe_sink_get_uclock(struct upipe *upipe, struct uclock **uclock_p)
{
    struct upipe_sink *upipe_sink = upipe_sink_from_upipe(upipe);
    assert(uclock_p != NULL);
    *uclock_p = upipe_sink->uclock;
    return true;
}

/** @internal @This sets the uclock structure. If it is unset or NULL, we
 * are in live mode and systime is taken into account.
 *
 * @param upipe description structure of the pipe
 * @param uclock new value
 * @return false in case of error
 */
static bool _upipe_sink_set_uclock(struct upipe *upipe, struct uclock *uclock)
{
    struct upipe_sink *upipe_sink = upipe_sink_from_upipe(upipe);
    if (unlikely(upipe_sink->uclock != NULL))
        uclock_release(upipe_sink->uclock);
    upipe_sink->uclock = uclock;
    if (likely(upipe_sink->uclock != NULL))
        uclock_use(upipe_sink->uclock);
    upipe_sink_set_upump(upipe, NULL);
    return true;
}

/** @internal @This gets the current upump_mgr.
 *
 * @param upipe description structure of the pipe
 * @param p filled in with the upump_mgr
 * @return false in case of error
 */
static bool _upipe_sink_get_upump_mgr(struct upipe *upipe, struct upump_mgr **p)
{
    struct upipe_sink *upipe_sink = upipe_sink_from_upipe(upipe);
    assert(p != NULL);
    *p = upipe_sink->upump_mgr;
    return true;
}

/** @internal @This sets the upump_mgr.
 *
 * @param upipe description structure of the pipe
 * @param upump_mgr new upump_mgr
 * @return false in case of error
 */
static bool _upipe_sink_set_upump_mgr(struct upipe *upipe,
                                      struct upump_mgr *upump_mgr)
{
    struct upipe_sink *upipe_sink = upipe_sink_from_upipe(upipe);
    if (unlikely(upipe_sink->upump != NULL)) {
        upump_stop(upipe_sink->upump);
        upump_free(upipe_sink->upump);
        upipe_sink->upump = NULL;
    }
    if (unlikely(upipe_sink->upump_mgr != NULL))
        upump_mgr_release(upipe_sink->upump_mgr);

    upipe_sink->upump_mgr = upump_mgr;
    if (likely(upump_mgr != NULL))
        upump_mgr_use(upipe_sink->upump_mgr);
    return true;
}

/** @internal @This gets the current delay.
 *
 * @param upipe description structure of the pipe
 * @param p filled in with the delay
 * @return false in case of error
 */
static bool _upipe_sink_get_delay(struct upipe *upipe, uint64_t *p)
{
    struct upipe_sink *upipe_sink = upipe_sink_from_upipe(upipe);
    assert(p != NULL);
    *p = upipe_sink->delay;
    return true;
}

/** @internal @This sets the delay.
 *
 * @param upipe description structure of the pipe
 * @param delay new delay
 * @return false in case of error
 */
static bool _upipe_sink_set_delay(struct upipe *upipe, uint64_t delay)
{
    struct upipe_sink *upipe_sink = upipe_sink_from_upipe(upipe);
    if (unlikely(upipe_sink->upump != NULL)) {
        upump_stop(upipe_sink->upump);
        upump_free(upipe_sink->upump);
        upipe_sink->upump = NULL;
    }

    upipe_sink->delay = delay;
    return true;
}

/** @This processes common control commands on a sink pipe.
 *
 * @param upipe description structure of the pipe
 * @param control type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static inline bool upipe_sink_control(struct upipe *upipe,
                                      enum upipe_control control,
                                      va_list args)
{
    switch (control) {
        case UPIPE_GET_UCLOCK: {
            struct uclock **p = va_arg(args, struct uclock **);
            return _upipe_sink_get_uclock(upipe, p);
        }
        case UPIPE_SET_UCLOCK: {
            struct uclock *uclock = va_arg(args, struct uclock *);
            return _upipe_sink_set_uclock(upipe, uclock);
        }
        case UPIPE_GET_UPUMP_MGR: {
            struct upump_mgr **p = va_arg(args, struct upump_mgr **);
            return _upipe_sink_get_upump_mgr(upipe, p);
        }
        case UPIPE_SET_UPUMP_MGR: {
            struct upump_mgr *upump_mgr = va_arg(args, struct upump_mgr *);
            return _upipe_sink_set_upump_mgr(upipe, upump_mgr);
        }
        case UPIPE_SINK_GET_DELAY: {
            uint64_t *p = va_arg(args, uint64_t *);
            return _upipe_sink_get_delay(upipe, p);
        }
        case UPIPE_SINK_SET_DELAY: {
            uint64_t delay = va_arg(args, uint64_t);
            return _upipe_sink_set_delay(upipe, delay);
        }
        default:
            return false;
    }
}

/** @This cleans up the common members of sink pipes.
 *
 * @param upipe description structure of the pipe
 */
static inline void upipe_sink_cleanup(struct upipe *upipe)
{
    struct upipe_sink *upipe_sink = upipe_sink_from_upipe(upipe);
    if (unlikely(upipe_sink->uclock != NULL))
        uclock_release(upipe_sink->uclock);
    if (likely(upipe_sink->upump != NULL)) {
        upump_stop(upipe_sink->upump);
        upump_free(upipe_sink->upump);
    }
    if (likely(upipe_sink->upump_mgr != NULL))
        upump_mgr_release(upipe_sink->upump_mgr);
}

#endif
