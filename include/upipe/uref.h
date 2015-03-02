/*
 * Copyright (C) 2012-2014 OpenHeadend S.A.R.L.
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
 * @short Upipe uref structure handling
 * This file defines the API to manipulate references to buffers and attributes.
 */

#ifndef _UPIPE_UREF_H_
/** @hidden */
#define _UPIPE_UREF_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/ubuf.h>
#include <upipe/udict.h>

#include <assert.h>
#include <inttypes.h>

/** @hidden */
struct uref_mgr;

/** @This defines the type of the date. */
enum uref_date_type {
    /** no date is defined */
    UREF_DATE_NONE = 0,
    /** clock ref is defined, DTS and PTS are derived */
    UREF_DATE_CR = 1,
    /** DTS is defined, CR and PTS are derived */
    UREF_DATE_DTS = 2,
    /** PTS is defined, CR and DTS are derived */
    UREF_DATE_PTS = 3
};

/** the upstream pipe has disconnected */
#define UREF_FLAG_FLOW_END 0x1
/** there is a discontinuity in the flow */
#define UREF_FLAG_FLOW_DISC 0x2
/** the block is a starting point */
#define UREF_FLAG_BLOCK_START 0x4
/** the block is an ending point */
#define UREF_FLAG_BLOCK_END 0x8
/** the block contains a clock reference */
#define UREF_FLAG_CLOCK_REF 0x10

/** position of the bitfield for the type of sys date */
#define UREF_FLAG_DATE_SYS 0x0400000000000000
/** position of the bitfield for the type of prog date */
#define UREF_FLAG_DATE_PROG 0x1000000000000000
/** position of the bitfield for the type of orig date */
#define UREF_FLAG_DATE_ORIG 0x4000000000000000

/** @internal @This is the number of bits to shift to get sys date type. */
#define UREF_FLAG_DATE_SYS_SHIFT 58
/** @internal @This is the number of bits to shift to get prog date type. */
#define UREF_FLAG_DATE_PROG_SHIFT 60
/** @internal @This is the number of bits to shift to get orig date type. */
#define UREF_FLAG_DATE_ORIG_SHIFT 62

/** @This stores references to a ubuf wth attributes. */
struct uref {
    /** structure for double-linked lists */
    struct uchain uchain;
    /** pointer to the entity responsible for the management */
    struct uref_mgr *mgr;

    /** pointer to ubuf */
    struct ubuf *ubuf;
    /** pointer to udict */
    struct udict *udict;

    /** void flags */
    uint64_t flags;
    /** date in system time */
    uint64_t date_sys;
    /** date in program time */
    uint64_t date_prog;
    /** original date */
    uint64_t date_orig;
    /** duration between DTS and PTS */
    uint64_t dts_pts_delay;
    /** duration between CR and DTS */
    uint64_t cr_dts_delay;
    /** duration between RAP and CR */
    uint64_t rap_cr_delay;
    /** private for local pipe user */
    uint64_t priv;
};

UBASE_FROM_TO(uref, uchain, uchain, uchain)

/** @This defines standard commands which uref managers may implement. */
enum uref_mgr_command {
    /** release all buffers kept in pools (void) */
    UREF_MGR_VACUUM,

    /** non-standard manager commands implemented by a module type can start
     * from there (first arg = signature) */
    UREF_MGR_CONTROL_LOCAL = 0x8000
};

/** @This stores common management parameters for a uref pool.
 */
struct uref_mgr {
    /** pointer to refcount management structure */
    struct urefcount *refcount;
    /** minimum size of a control uref */
    size_t control_attr_size;
    /** udict manager */
    struct udict_mgr *udict_mgr;

    /** function to allocate a uref */
    struct uref *(*uref_alloc)(struct uref_mgr *);
    /** function to free a uref */
    void (*uref_free)(struct uref *);

    /** control function for standard or local manager commands - all parameters
     * belong to the caller */
    int (*uref_mgr_control)(struct uref_mgr *, int, va_list);
};

/** @This frees a uref and other sub-structures.
 *
 * @param uref structure to free
 */
static inline void uref_free(struct uref *uref)
{
    if (uref == NULL)
        return;
    ubuf_free(uref->ubuf);
    udict_free(uref->udict);
    uref->mgr->uref_free(uref);
}

/** @This initializes a new uref.
 *
 * @param uref allocated uref
 */
static inline void uref_init(struct uref *uref)
{
    uref->ubuf = NULL;
    uref->udict = NULL;

    uref->flags = 0;
    uref->date_sys = UINT64_MAX;
    uref->date_prog = UINT64_MAX;
    uref->date_orig = UINT64_MAX;
    uref->dts_pts_delay = UINT64_MAX;
    uref->cr_dts_delay = UINT64_MAX;
    uref->rap_cr_delay = UINT64_MAX;
    uref->priv = UINT64_MAX;
}

/** @This allocates and initializes a new uref.
 *
 * @param mgr management structure for this buffer pool
 * @return allocated uref or NULL in case of allocation failure
 */
static inline struct uref *uref_alloc(struct uref_mgr *mgr)
{
    struct uref *uref = mgr->uref_alloc(mgr);
    if (unlikely(uref == NULL))
        return NULL;

    uref_init(uref);
    return uref;
}

/** @This allocates and initializes a new uref, allocated with a manager from
 * an existing uref.
 *
 * @param uref existing uref
 * @return allocated uref or NULL in case of allocation failure
 */
static inline struct uref *uref_sibling_alloc(struct uref *uref)
{
    return uref_alloc(uref->mgr);
}

/** @This returns a new uref with extra attributes space.
 * This is typically useful for control messages.
 *
 * @param mgr management structure for this uref pool
 * @return allocated uref or NULL in case of allocation failure
 */
static inline struct uref *uref_alloc_control(struct uref_mgr *mgr)
{
    struct uref *uref = uref_alloc(mgr);
    if (unlikely(uref == NULL))
        return NULL;

    uref->udict = udict_alloc(mgr->udict_mgr, mgr->control_attr_size);
    if (unlikely(uref->udict == NULL)) {
        uref_free(uref);
        return NULL;
    }

    return uref;
}

/** @This returns a new uref with extra attributes space, allocated with a
 * manager from an existing uref.
 * This is typically useful for control messages.
 *
 * @param uref existing uref
 * @return allocated uref or NULL in case of allocation failure
 */
static inline struct uref *uref_sibling_alloc_control(struct uref *uref)
{
    return uref_alloc_control(uref->mgr);
}

/** @internal @This duplicates a uref without duplicating the ubuf.
 *
 * @param uref source structure to duplicate
 * @return duplicated uref or NULL in case of allocation failure
 */
static inline struct uref *uref_dup_inner(struct uref *uref)
{
    assert(uref != NULL);
    struct uref *new_uref = uref->mgr->uref_alloc(uref->mgr);
    if (unlikely(new_uref == NULL))
        return NULL;

    new_uref->ubuf = NULL;
    if (uref->udict != NULL) {
        new_uref->udict = udict_dup(uref->udict);
        if (unlikely(new_uref->udict == NULL)) {
            uref_free(new_uref);
            return NULL;
        }
    } else
        new_uref->udict = NULL;

    new_uref->flags = uref->flags;
    new_uref->date_sys = uref->date_sys;
    new_uref->date_prog = uref->date_prog;
    new_uref->date_orig = uref->date_orig;
    new_uref->dts_pts_delay = uref->dts_pts_delay;
    new_uref->cr_dts_delay = uref->cr_dts_delay;
    new_uref->rap_cr_delay = uref->rap_cr_delay;
    new_uref->priv = uref->priv;

    return new_uref;
}

/** @This duplicates a uref.
 *
 * @param uref source structure to duplicate
 * @return duplicated uref or NULL in case of allocation failure
 */
static inline struct uref *uref_dup(struct uref *uref)
{
    struct uref *new_uref = uref_dup_inner(uref);
    if (unlikely(new_uref == NULL))
        return NULL;

    if (uref->ubuf != NULL) {
        new_uref->ubuf = ubuf_dup(uref->ubuf);
        if (unlikely(new_uref->ubuf == NULL)) {
            uref_free(new_uref);
            return NULL;
        }
    }
    return new_uref;
}

/** @This attaches a ubuf to a given uref. The ubuf pointer may no longer be
 * used by the module afterwards.
 *
 * @param uref pointer to uref structure
 * @param ubuf pointer to ubuf structure to attach to uref
 */
static inline void uref_attach_ubuf(struct uref *uref, struct ubuf *ubuf)
{
    if (uref->ubuf != NULL)
        ubuf_free(uref->ubuf);

    uref->ubuf = ubuf;
}

/** @This detaches a ubuf from a uref. The returned ubuf must be freed
 * or re-attached at some point, otherwise it will leak.
 *
 * @param uref pointer to uref structure
 * @return pointer to detached ubuf structure
 */
static inline struct ubuf *uref_detach_ubuf(struct uref *uref)
{
    struct ubuf *ubuf = uref->ubuf;
    uref->ubuf = NULL;
    return ubuf;
}

/** @This increments the reference count of a uref manager.
 *
 * @param mgr pointer to uref manager
 * @return same pointer to uref manager
 */
static inline struct uref_mgr *uref_mgr_use(struct uref_mgr *mgr)
{
    if (mgr == NULL)
        return NULL;
    urefcount_use(mgr->refcount);
    return mgr;
}

/** @This decrements the reference count of a uref manager or frees it.
 *
 * @param mgr pointer to uref manager
 */
static inline void uref_mgr_release(struct uref_mgr *mgr)
{
    if (mgr != NULL)
        urefcount_release(mgr->refcount);
}

/** @internal @This sends a control command to the uref manager. Note that all
 * arguments are owned by the caller.
 *
 * @param mgr pointer to uref manager
 * @param command manager control command to send
 * @param args optional read or write parameters
 * @return an error code
 */
static inline int uref_mgr_control_va(struct uref_mgr *mgr,
                                      int command, va_list args)
{
    assert(mgr != NULL);
    if (mgr->uref_mgr_control == NULL)
        return UBASE_ERR_UNHANDLED;

    return mgr->uref_mgr_control(mgr, command, args);
}

/** @internal @This sends a control command to the uref manager. Note that all
 * arguments are owned by the caller.
 *
 * @param mgr pointer to uref manager
 * @param command control manager command to send, followed by optional read
 * or write parameters
 * @return an error code
 */
static inline int uref_mgr_control(struct uref_mgr *mgr, int command, ...)
{
    int err;
    va_list args;
    va_start(args, command);
    err = uref_mgr_control_va(mgr, command, args);
    va_end(args);
    return err;
}

/** @This instructs an existing uref manager to release all structures
 * currently kept in pools. It is inteded as a debug tool only.
 *
 * @param mgr pointer to uref manager
 * @return an error code
 */
static inline int uref_mgr_vacuum(struct uref_mgr *mgr)
{
    return uref_mgr_control(mgr, UREF_MGR_VACUUM);
}

#ifdef __cplusplus
}
#endif
#endif
