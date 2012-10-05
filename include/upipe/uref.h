/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
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
#include <upipe/ubuf.h>
#include <upipe/udict.h>
#include <upipe/upump.h>

/** @hidden */
struct uref_mgr;

/** @This stores references to a ubuf, a udict and a upump.
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
    /** pointer to upump */
    struct upump *upump;
};

/** @This stores common management parameters for a uref pool.
 */
struct uref_mgr {
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
    /** function to increment the refcount of the uref manager */
    void (*uref_mgr_use)(struct uref_mgr *);
    /** function to decrement the refcount of the uref manager or free it */
    void (*uref_mgr_release)(struct uref_mgr *);
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
    /* FIXME upump */
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
    uref->upump = NULL;
    uref->udict = udict_alloc(mgr->udict_mgr, 0);
    if (unlikely(uref->udict == NULL)) {
        uref_free(uref);
        return NULL;
    }

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
    uref->upump = NULL;
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
    new_uref->upump = NULL;
    new_uref->udict = udict_dup(uref->udict);
    if (unlikely(new_uref->udict == NULL)) {
        uref_free(new_uref);
        return NULL;
    }

    if (uref->ubuf != NULL) {
        new_uref->ubuf = ubuf_dup(uref->ubuf);
        if (unlikely(new_uref->ubuf != NULL)) {
            uref_free(new_uref);
            return NULL;
        }
    }

    /* FIXME */
    new_uref->upump = uref->upump;
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

/** @This attaches a upump to a given uref. The upump pointer may no longer be
 * used by the module afterwards.
 *
 * @param uref pointer to uref structure
 * @param upump pointer to upump structure to attach to uref
 */
static inline void uref_attach_upump(struct uref *uref, struct upump *upump)
{
/*    if (uref->upump != NULL)
        FIXME */

    uref->upump = upump;
}

/** @This detaches a upump from a uref. The returned upump must be freed
 * or re-attached at some point, otherwise it will leak.
 *
 * @param uref pointer to uref structure
 * @return pointer to detached upump structure
 */
static inline struct upump *uref_detach_upump(struct uref *uref)
{
    struct upump *upump = uref->upump;
    uref->upump = NULL;
    return upump;
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
 * @param uref struct uref structure
 * @return pointer to the uchain structure
 */
static inline struct uchain *uref_to_uchain(struct uref *uref)
{
    return &uref->uchain;
}

/** @This instructs an existing uref manager to release all structures currently
 * kept in pools. It is inteded as a debug tool only.
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
    if (likely(mgr->uref_mgr_use != NULL))
        mgr->uref_mgr_use(mgr);
}

/** @This decrements the reference count of a uref manager or frees it.
 *
 * @param mgr pointer to uref manager
 */
static inline void uref_mgr_release(struct uref_mgr *mgr)
{
    if (likely(mgr->uref_mgr_release != NULL))
        mgr->uref_mgr_release(mgr);
}

#endif
