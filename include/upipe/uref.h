/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
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

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/ubuf.h>
#include <upipe/udict.h>

#include <inttypes.h>

/** @hidden */
struct uref_mgr;

/** @This stores references to a ubuf and a udict.
 */
struct uref {
    /** structure for double-linked lists */
    struct uchain uchain;
    /** pointer to the entity responsible for the management */
    struct uref_mgr *mgr;

    /** pointer to ubuf */
    struct ubuf *ubuf;
    /** pointer to udict */
    struct udict *udict;

    /** true in case of discontinuity */
    bool flow_disc;
    /** true if the block is a starting point */
    bool block_start;
    /** reception systime */
    uint64_t systime;
    /** reception systime of the random access point */
    uint64_t systime_rap;
    /** presentation timestamp in simulated stream clock */
    uint64_t pts;
    /** presentation timestamp in original stream clock */
    uint64_t pts_orig;
    /** presentation timestamp in system clock */
    uint64_t pts_sys;
    /** decoding timestamp in simulated stream clock */
    uint64_t dts;
    /** decoding timestamp in original stream clock */
    uint64_t dts_orig;
    /** decoding timestamp in system clock */
    uint64_t dts_sys;
};

/** @This stores common management parameters for a uref pool.
 */
struct uref_mgr {
    /** refcount management structure */
    urefcount refcount;
    /** minimum size of a control uref */
    size_t control_attr_size;
    /** udict manager */
    struct udict_mgr *udict_mgr;

    /** function to allocate a uref */
    struct uref *(*uref_alloc)(struct uref_mgr *);
    /** function to free a uref */
    void (*uref_free)(struct uref *);

    /** function to release all buffers kept in pools */
    void (*uref_mgr_vacuum)(struct uref_mgr *);
    /** function to free the uref manager */
    void (*uref_mgr_free)(struct uref_mgr *);
};

/** @This frees a uref and other sub-structures.
 *
 * @param uref structure to free
 */
static inline void uref_free(struct uref *uref)
{
    if (uref->ubuf != NULL)
        ubuf_free(uref->ubuf);
    if (uref->udict != NULL)
        udict_free(uref->udict);
    uref->mgr->uref_free(uref);
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

    uref->ubuf = NULL;
    uref->udict = udict_alloc(mgr->udict_mgr, 0);

    uref->flow_disc = false;
    uref->block_start = false;
    uref->systime = UINT64_MAX;
    uref->systime_rap = UINT64_MAX;
    uref->pts = UINT64_MAX;
    uref->pts_orig = UINT64_MAX;
    uref->pts_sys = UINT64_MAX;
    uref->dts = UINT64_MAX;
    uref->dts_orig = UINT64_MAX;
    uref->dts_sys = UINT64_MAX;

    return uref;
}

/** @This returns a new uref with extra attributes space.
 * This is typically useful for control messages.
 *
 * @param mgr management structure for this uref pool
 * @return allocated uref or NULL in case of allocation failure
 */
static inline struct uref *uref_alloc_control(struct uref_mgr *mgr)
{
    struct uref *uref = mgr->uref_alloc(mgr);
    if (unlikely(uref == NULL))
        return NULL;

    uref->ubuf = NULL;
    uref->udict = udict_alloc(mgr->udict_mgr, mgr->control_attr_size);
    if (unlikely(uref->udict == NULL)) {
        uref_free(uref);
        return NULL;
    }

    return uref;
}

/** @This duplicates a uref.
 *
 * @param uref source structure to duplicate
 * @return duplicated uref or NULL in case of allocation failure
 */
static inline struct uref *uref_dup(struct uref *uref)
{
    struct uref *new_uref = uref->mgr->uref_alloc(uref->mgr);
    if (unlikely(new_uref == NULL))
        return NULL;

    new_uref->ubuf = NULL;
    new_uref->udict = udict_dup(uref->udict);
    if (unlikely(new_uref->udict == NULL)) {
        uref_free(new_uref);
        return NULL;
    }

    new_uref->flow_disc = uref->flow_disc;
    new_uref->block_start = uref->block_start;
    new_uref->systime = uref->systime;
    new_uref->systime_rap = uref->systime_rap;
    new_uref->pts = uref->pts;
    new_uref->pts_orig = uref->pts_orig;
    new_uref->pts_sys = uref->pts_sys;
    new_uref->dts = uref->dts;
    new_uref->dts_orig = uref->dts_orig;
    new_uref->dts_sys = uref->dts_sys;

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

/** @This returns the high-level uref structure.
 *
 * @param uchain pointer to the uchain structure wrapped into the uref
 * @return pointer to the uref structure
 */
static inline struct uref *uref_from_uchain(struct uchain *uchain)
{
    return container_of(uchain, struct uref, uchain);
}

/** @This returns the uchain structure used for FIFO, LIFO and lists.
 *
 * @param uref uref structure
 * @return pointer to the uchain structure
 */
static inline struct uchain *uref_to_uchain(struct uref *uref)
{
    return &uref->uchain;
}

/** @This instructs an existing uref manager to release all structures currently
 * kept in pools. It is intended as a debug tool only.
 *
 * @param mgr pointer to uref manager
 */
static inline void uref_mgr_vacuum(struct uref_mgr *mgr)
{
    mgr->uref_mgr_vacuum(mgr);
}

/** @This increments the reference count of a uref manager.
 *
 * @param mgr pointer to uref manager
 */
static inline void uref_mgr_use(struct uref_mgr *mgr)
{
    urefcount_use(&mgr->refcount);
}

/** @This decrements the reference count of a uref manager or frees it.
 *
 * @param mgr pointer to uref manager
 */
static inline void uref_mgr_release(struct uref_mgr *mgr)
{
    if (unlikely(urefcount_release(&mgr->refcount)))
        mgr->uref_mgr_free(mgr);
}

#endif
