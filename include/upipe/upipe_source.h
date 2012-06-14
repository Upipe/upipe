/*****************************************************************************
 * upipe_source.h: common declarations of linear source pipes (one output)
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

#ifndef _UPIPE_UPIPE_SOURCE_H_
/** @hidden */
#define _UPIPE_UPIPE_SOURCE_H_

#include <upipe/ubase.h>
#include <upipe/uref_flow.h>
#include <upipe/uclock.h>
#include <upipe/upump.h>
#include <upipe/upipe.h>
#include <upipe/upipe_linear.h>

#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>

/** super-set of the upipe_linear structure with additional members */
struct upipe_source {
    /** uclock structure, if not NULL we are in live mode */
    struct uclock *uclock;
    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** read watcher */
    struct upump *upump;
    /** flow name */
    char *flow_name;
    /** read size */
    size_t read_size;

    /** linear sources are a special case of linear pipes */
    struct upipe_linear upipe_linear;
};

/** @This returns the high-level upipe structure.
 *
 * @param upipe_source pointer to the upipe_source structure
 * @return pointer to the upipe structure
 */
static inline struct upipe *upipe_source_to_upipe(struct upipe_source *upipe_source)
{
    return upipe_linear_to_upipe(&upipe_source->upipe_linear);
}

/** @This returns the private struct upipe_source structure.
 *
 * @param upipe description structure of the pipe
 * @return pointer to the upipe_source structure
 */
static inline struct upipe_source *upipe_source_from_upipe(struct upipe *upipe)
{
    struct upipe_linear *upipe_linear = upipe_linear_from_upipe(upipe);
    return container_of(upipe_linear, struct upipe_source, upipe_linear);
}

UPIPE_STRUCT_TEMPLATE(source, uclock, struct uclock *)
UPIPE_STRUCT_TEMPLATE(source, upump_mgr, struct upump_mgr *)
UPIPE_STRUCT_TEMPLATE(source, upump, struct upump *)
UPIPE_STRUCT_TEMPLATE(source, read_size, size_t)

/** @This sets the upump to use.
 *
 * @param upipe description structure of the pipe
 * @param upump upump structure to use
 */
static inline void upipe_source_set_upump(struct upipe *upipe,
                                          struct upump *upump)
{
    struct upipe_source *upipe_source = upipe_source_from_upipe(upipe);
    if (unlikely(upipe_source->upump != NULL)) {
        upump_stop(upipe_source->upump);
        upump_free(upipe_source->upump);
    }
    upipe_source->upump = upump;
}

/** @This checks if the source pipe is ready to process data.
 *
 * @param upipe description structure of the pipe
 */
static inline bool upipe_source_ready(struct upipe *upipe)
{
    struct upipe_source *upipe_source = upipe_source_from_upipe(upipe);
    return upipe_linear_ready(upipe) && upipe_linear_ubuf_mgr(upipe) != NULL &&
           upipe_source->upump_mgr != NULL && upipe_source->flow_name != NULL;
}

/** @This initializes the common members of source pipes.
 *
 * @param upipe description structure of the pipe
 * @param read_size default read size
 */
static inline void upipe_source_init(struct upipe *upipe, size_t read_size)
{
    struct upipe_source *upipe_source = upipe_source_from_upipe(upipe);
    upipe_source->uclock = NULL;
    upipe_source->upump_mgr = NULL;
    upipe_source->upump = NULL;
    upipe_source->flow_name = NULL;
    upipe_source->read_size = read_size;
    upipe_linear_init(upipe);
}

/** @This sets the flow definition of the source. May only be called when
 * upipe_source_ready() (more precisely, when flow_name has been set).
 *
 * @param upipe description structure of the pipe
 * @param flow_def control packet describing the source flow
 */
static inline void upipe_source_set_flow_def(struct upipe *upipe,
                                             struct uref *flow_def)
{
    struct upipe_source *upipe_source = upipe_source_from_upipe(upipe);
    assert(upipe_source->flow_name != NULL);
    uref_flow_set_name(&flow_def, upipe_source->flow_name);
    upipe_linear_set_flow_def(upipe, flow_def);
}

/** @This outputs a packet to the appropriate flow on the output.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure to send
 */
static inline void upipe_source_output(struct upipe *upipe, struct uref *uref)
{
    upipe_linear_output(upipe, uref);
}

/** @internal @This returns the uclock structure.
 *
 * @param upipe description structure of the pipe
 * @param uclock_p reference to a value, will be modified (or NULL-ed)
 * @return false in case of error
 */
static inline bool _upipe_source_get_uclock(struct upipe *upipe,
                                            struct uclock **uclock_p)
{
    struct upipe_source *upipe_source = upipe_source_from_upipe(upipe);
    assert(uclock_p != NULL);
    *uclock_p = upipe_source->uclock;
    return true;
}

/** @internal @This sets the uclock structure. If it is unset or NULL, we
 * are in live mode and systime is taken into account.
 *
 * @param upipe description structure of the pipe
 * @param uclock new value
 * @return false in case of error
 */
static inline bool _upipe_source_set_uclock(struct upipe *upipe,
                                            struct uclock *uclock)
{
    struct upipe_source *upipe_source = upipe_source_from_upipe(upipe);
    if (unlikely(upipe_source->uclock != NULL))
        uclock_release(upipe_source->uclock);
    upipe_source->uclock = uclock;
    if (likely(upipe_source->uclock != NULL))
        uclock_use(upipe_source->uclock);
    upipe_source_set_upump(upipe, NULL);
    return true;
}

/** @internal @This gets the current upump_mgr.
 *
 * @param upipe description structure of the pipe
 * @param p filled in with the upump_mgr
 * @return false in case of error
 */
static inline bool _upipe_source_get_upump_mgr(struct upipe *upipe,
                                               struct upump_mgr **p)
{
    struct upipe_source *upipe_source = upipe_source_from_upipe(upipe);
    assert(p != NULL);
    *p = upipe_source->upump_mgr;
    return true;
}

/** @internal @This sets the upump_mgr.
 *
 * @param upipe description structure of the pipe
 * @param upump_mgr new upump_mgr
 * @return false in case of error
 */
static inline bool _upipe_source_set_upump_mgr(struct upipe *upipe,
                                               struct upump_mgr *upump_mgr)
{
    struct upipe_source *upipe_source = upipe_source_from_upipe(upipe);
    if (unlikely(upipe_source->upump != NULL)) {
        upump_stop(upipe_source->upump);
        upump_free(upipe_source->upump);
        upipe_source->upump = NULL;
    }
    if (unlikely(upipe_source->upump_mgr != NULL))
        upump_mgr_release(upipe_source->upump_mgr);

    upipe_source->upump_mgr = upump_mgr;
    if (likely(upump_mgr != NULL))
        upump_mgr_use(upipe_source->upump_mgr);
    return true;
}

/** @internal @This gets the current flow name of the source.
 *
 * @param upipe description structure of the pipe
 * @param p filled in with the flow name
 * @return false in case of error
 */
static inline bool _upipe_source_get_flow(struct upipe *upipe, const char **p)
{
    struct upipe_source *upipe_source = upipe_source_from_upipe(upipe);
    assert(p != NULL);
    *p = upipe_source->flow_name;
    return true;
}

/** @internal @This sets the flow name of the source.
 *
 * @param upipe description structure of the pipe
 * @param flow_name new name of the flow
 * @return false in case of error
 */
static inline bool _upipe_source_set_flow(struct upipe *upipe,
                                          const char *flow_name)
{
    struct upipe_source *upipe_source = upipe_source_from_upipe(upipe);
    free(upipe_source->flow_name);
    upipe_source->flow_name = strdup(flow_name);

    struct uref *flow_def = upipe_linear_flow_def(upipe);
    if (unlikely(flow_def != NULL)) {
        struct uref_mgr *uref_mgr = upipe_linear_uref_mgr(upipe);
        if (likely(uref_mgr != NULL)) {
            struct uref *uref = uref_flow_dup(uref_mgr, flow_def, flow_name);
            upipe_linear_set_flow_def(upipe, uref);
        } else
            upipe_linear_set_flow_def(upipe, NULL);
    }
    return true;
}

/** @internal @This gets the current read size of the source.
 *
 * @param upipe description structure of the pipe
 * @param p filled in with the read size
 * @return false in case of error
 */
static inline bool _upipe_source_get_read_size(struct upipe *upipe,
                                               unsigned int *p)
{
    struct upipe_source *upipe_source = upipe_source_from_upipe(upipe);
    assert(p != NULL);
    *p = upipe_source->read_size;
    return true;
}

/** @internal @This sets the read size of the source.
 *
 * @param upipe description structure of the pipe
 * @param s new read size of the source
 * @return false in case of error
 */
static inline bool _upipe_source_set_read_size(struct upipe *upipe,
                                               unsigned int s)
{
    struct upipe_source *upipe_source = upipe_source_from_upipe(upipe);
    upipe_source->read_size = s;
    return true;
}

/** @This processes common control commands on a source pipe.
 *
 * @param upipe description structure of the pipe
 * @param control type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static inline bool upipe_source_control(struct upipe *upipe,
                                        enum upipe_control control,
                                        va_list args)
{
    switch (control) {
        case UPIPE_GET_UCLOCK: {
            struct uclock **p = va_arg(args, struct uclock **);
            return _upipe_source_get_uclock(upipe, p);
        }
        case UPIPE_SET_UCLOCK: {
            struct uclock *uclock = va_arg(args, struct uclock *);
            return _upipe_source_set_uclock(upipe, uclock);
        }
        case UPIPE_GET_UPUMP_MGR: {
            struct upump_mgr **p = va_arg(args, struct upump_mgr **);
            return _upipe_source_get_upump_mgr(upipe, p);
        }
        case UPIPE_SET_UPUMP_MGR: {
            struct upump_mgr *upump_mgr = va_arg(args, struct upump_mgr *);
            return _upipe_source_set_upump_mgr(upipe, upump_mgr);
        }
        case UPIPE_SOURCE_GET_FLOW: {
            const char **flow_name_p = va_arg(args, const char **);
            return _upipe_source_get_flow(upipe, flow_name_p);
        }
        case UPIPE_SOURCE_SET_FLOW: {
            const char *flow_name = va_arg(args, const char *);
            return _upipe_source_set_flow(upipe, flow_name);
        }
        case UPIPE_SOURCE_GET_READ_SIZE: {
            unsigned int *size_p = va_arg(args, unsigned int *);
            return _upipe_source_get_read_size(upipe, size_p);
        }
        case UPIPE_SOURCE_SET_READ_SIZE: {
            unsigned int size = va_arg(args, unsigned int);
            return _upipe_source_set_read_size(upipe, size);
        }
        default:
            return upipe_linear_control(upipe, control, args);
    }
}

/** @This cleans up the common members of source pipes.
 *
 * @param upipe description structure of the pipe
 */
static inline void upipe_source_clean(struct upipe *upipe)
{
    struct upipe_source *upipe_source = upipe_source_from_upipe(upipe);
    if (unlikely(upipe_source->uclock != NULL))
        uclock_release(upipe_source->uclock);
    if (likely(upipe_source->upump != NULL)) {
        upump_stop(upipe_source->upump);
        upump_free(upipe_source->upump);
    }
    if (likely(upipe_source->upump_mgr != NULL))
        upump_mgr_release(upipe_source->upump_mgr);
    free(upipe_source->flow_name);
    upipe_linear_clean(upipe);
}

#endif
