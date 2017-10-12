/*
 * Copyright (C) 2014-2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
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
 * @short Upipe structure used to provide an upstream pipe with a structure
 */

#ifndef _UPIPE_UREQUEST_H_
/** @hidden */
#define _UPIPE_UREQUEST_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/uref.h>

#include <stdbool.h>
#include <stdarg.h>
#include <assert.h>

/** @hidden */
struct uclock;
/** @hidden */
struct ubuf_mgr;
/** @hidden */
struct urequest;

/** @This defines standard requests which upipe modules may need. */
enum urequest_type {
    /** a uref manager is requested (struct uref_mgr *) */
    UREQUEST_UREF_MGR = 0,
    /** a flow format is requested (struct uref *) */
    UREQUEST_FLOW_FORMAT,
    /** a ubuf manager is requested (struct ubuf_mgr *, struct uref *) */
    UREQUEST_UBUF_MGR,
    /** a uclock is requested (struct uclock *) */
    UREQUEST_UCLOCK,
    /** the latency of the sink is requested (uint64_t) */
    UREQUEST_SINK_LATENCY,

    /** non-standard requests implemented by a module type can start from
     * there (first arg = signature) */
    UREQUEST_LOCAL = 0x8000
};

/** @This converts an urequest type into a string.
 *
 * @param type urequest type id to convert
 * @return a string or NULL if unknown
 */
static inline const char *urequest_type_str(int type)
{
    switch ((enum urequest_type)type) {
        case UREQUEST_UREF_MGR:     return "uref mgr";
        case UREQUEST_FLOW_FORMAT:  return "flow format";
        case UREQUEST_UBUF_MGR:     return "ubuf mgr";
        case UREQUEST_UCLOCK:       return "uclock";
        case UREQUEST_SINK_LATENCY: return "sink latency";
        case UREQUEST_LOCAL:        break;
    }
    return NULL;
}

/** @This is the call-back type for urequest structures. */
typedef int (*urequest_func)(struct urequest *, va_list);
/** @This frees the request. */
typedef void (*urequest_free_func)(struct urequest *);

/** @This is a structure passed to a module when the upstream requests a
 * structure. */
struct urequest {
    /** structure for double-linked lists - for use by the upstream pipe only */
    struct uchain uchain;
    /** opaque - for use by the upstream pipe only */
    void *opaque;
    /** true if the urequest was already registered */
    bool registered;

    /** request type */
    int type;
    /** optional uref argument */
    struct uref *uref;
    /** function to provide a structure */
    urequest_func urequest_provide;
    /** function to free the request */
    urequest_free_func urequest_free;
};

UBASE_FROM_TO(urequest, uchain, uchain, uchain)

/** @internal @This initializes a urequest structure.
 *
 * @param urequest pointer to urequest
 * @param type request type
 * @param urequest_provide function used to provide a structure
 * @param urequest_free function used to free the request
 */
static inline void urequest_init(struct urequest *urequest, int type,
                                 struct uref *uref,
                                 urequest_func urequest_provide,
                                 urequest_free_func urequest_free)
{
    assert(urequest != NULL);
    uchain_init(&urequest->uchain);
    urequest->registered = false;
    urequest->type = type;
    urequest->uref = uref;
    urequest->urequest_provide = urequest_provide;
    urequest->urequest_free = urequest_free;
}

/** @This initializes a urequest structure asking for a uref manager.
 *
 * @param urequest pointer to urequest
 * @param urequest_provide function used to provide a structure
 * @param urequest_free function used to free the request
 */
static inline void urequest_init_uref_mgr(struct urequest *urequest,
                                          urequest_func urequest_provide,
                                          urequest_free_func urequest_free)
{
    urequest_init(urequest, UREQUEST_UREF_MGR, NULL, urequest_provide,
                  urequest_free);
}

/** @This initializes a urequest structure asking for a flow format. The
 * downstream pipes will provide a modified flow format that will be compatible.
 *
 * @param urequest pointer to urequest
 * @param flow_format proposed flow format
 * @param urequest_provide function used to provide a structure
 * @param urequest_free function used to free the request
 */
static inline void urequest_init_flow_format(struct urequest *urequest,
                                             struct uref *flow_format,
                                             urequest_func urequest_provide,
                                             urequest_free_func urequest_free)
{
    urequest_init(urequest, UREQUEST_FLOW_FORMAT, flow_format,
                  urequest_provide, urequest_free);
}

/** @This initializes a urequest structure asking for a ubuf manager. The
 * downstream pipes may amend the request to change alignment or padding.
 *
 * @param urequest pointer to urequest
 * @param flow_format requested flow format
 * @param urequest_provide function used to provide a structure
 * @param urequest_free function used to free the request
 */
static inline void urequest_init_ubuf_mgr(struct urequest *urequest,
                                          struct uref *flow_format,
                                          urequest_func urequest_provide,
                                          urequest_free_func urequest_free)
{
    urequest_init(urequest, UREQUEST_UBUF_MGR, flow_format,
                  urequest_provide, urequest_free);
}

/** @This initializes a urequest structure asking for a uclock.
 *
 * @param urequest pointer to urequest
 * @param flow_format requested flow format
 * @param urequest_provide function used to provide a structure
 * @param urequest_free function used to free the request
 */
static inline void urequest_init_uclock(struct urequest *urequest,
                                        urequest_func urequest_provide,
                                        urequest_free_func urequest_free)
{
    urequest_init(urequest, UREQUEST_UCLOCK, NULL, urequest_provide,
                  urequest_free);
}

/** @This initializes a urequest structure asking for the sink latency. The
 * downstream pipes will provide a modified flow format that will be compatible.
 *
 * @param urequest pointer to urequest
 * @param urequest_provide function used to provide a structure
 * @param urequest_free function used to free the request
 */
static inline void urequest_init_sink_latency(struct urequest *urequest,
                                              urequest_func urequest_provide,
                                              urequest_free_func urequest_free)
{
    urequest_init(urequest, UREQUEST_SINK_LATENCY, NULL,
                  urequest_provide, urequest_free);
}

/** @This cleans up a urequest structure.
 *
 * @param urequest pointer to urequest
 */
static inline void urequest_clean(struct urequest *urequest)
{
    assert(urequest != NULL);
    assert(!urequest->registered);
    uref_free(urequest->uref);
}

/** @This frees a urequest structure. Note that you must clean it first, if it
 * has been initialized.
 *
 * @param urequest pointer to urequest
 */
static inline void urequest_free(struct urequest *urequest)
{
    assert(urequest != NULL);
    if (urequest->urequest_free != NULL)
        urequest->urequest_free(urequest);
}

/** @This gets the opaque member of a request.
 *
 * @param urequest pointer to urequest
 * @param type type to cast to
 * @return opaque
 */
#define urequest_get_opaque(urequest, type) (type)(urequest)->opaque

/** @This sets the opaque member of a request.
 *
 * @param urequest pointer to urequest
 * @param opaque opaque
 */
static inline void urequest_set_opaque(struct urequest *urequest, void *opaque)
{
    urequest->opaque = opaque;
}

/** @internal @This provides a urequest with optional arguments. All arguments
 * belong to the callee.
 *
 * @param urequest pointer to urequest
 * @param args list of arguments
 * @return an error code
 */
static inline int urequest_provide_va(struct urequest *urequest, va_list args)
{
    assert(urequest != NULL);
    return urequest->urequest_provide(urequest, args);
}

/** @internal @This provides a urequest with optional arguments. All arguments
 * belong to the callee.
 *
 * @param urequest pointer to urequest, followed with optional arguments
 * @return an error code
 */
static inline int urequest_provide(struct urequest *urequest, ...)
{
    va_list args;
    va_start(args, urequest);
    int err = urequest_provide_va(urequest, args);
    va_end(args);
    return err;
}

/** @This provides a urequest with a new uref manager. All arguments
 * belong to the callee.
 *
 * @param urequest pointer to urequest
 * @param uref_mgr pointer to uref manager
 * @return an error code
 */
static inline int urequest_provide_uref_mgr(struct urequest *urequest,
                                            struct uref_mgr *uref_mgr)
{
    if (unlikely(urequest->type != UREQUEST_UREF_MGR))
        return UBASE_ERR_INVALID;
    return urequest_provide(urequest, uref_mgr);
}

/** @This provides a urequest with a new flow format. All arguments
 * belong to the callee.
 *
 * @param urequest pointer to urequest
 * @param flow format suggested flow format
 * @return an error code
 */
static inline int urequest_provide_flow_format(struct urequest *urequest,
                                               struct uref *flow_format)
{
    if (unlikely(urequest->type != UREQUEST_FLOW_FORMAT))
        return UBASE_ERR_INVALID;
    return urequest_provide(urequest, flow_format);
}

/** @This provides a urequest with a new ubuf manager. All arguments
 * belong to the callee.
 *
 * @param urequest pointer to urequest
 * @param ubuf_mgr pointer to ubuf manager
 * @param flow format allocated flow format
 * @return an error code
 */
static inline int urequest_provide_ubuf_mgr(struct urequest *urequest,
                                            struct ubuf_mgr *ubuf_mgr,
                                            struct uref *flow_format)
{
    if (unlikely(urequest->type != UREQUEST_UBUF_MGR))
        return UBASE_ERR_INVALID;
    return urequest_provide(urequest, ubuf_mgr, flow_format);
}

/** @This provides a urequest with a new uclock. All arguments
 * belong to the callee.
 *
 * @param urequest pointer to urequest
 * @param uclock pointer to uclock
 * @return an error code
 */
static inline int urequest_provide_uclock(struct urequest *urequest,
                                          struct uclock *uclock)
{
    if (unlikely(urequest->type != UREQUEST_UCLOCK))
        return UBASE_ERR_INVALID;
    return urequest_provide(urequest, uclock);
}

/** @This provides a urequest with a new sink latency. All arguments
 * belong to the callee.
 *
 * @param urequest pointer to urequest
 * @param latency sink latency
 * @return an error code
 */
static inline int urequest_provide_sink_latency(struct urequest *urequest,
                                                uint64_t latency)
{
    if (unlikely(urequest->type != UREQUEST_SINK_LATENCY))
        return UBASE_ERR_INVALID;
    return urequest_provide(urequest, latency);
}

#ifdef __cplusplus
}
#endif
#endif
