/*****************************************************************************
 * uref.h: upipe uref structure handling
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

#ifndef _UPIPE_UREF_H_
/** @hidden */
#define _UPIPE_UREF_H_

#include <stdint.h>
#include <string.h>
#include <assert.h>

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/ubuf.h>

/** @hidden */
struct uref_mgr;
/** @hidden */
enum uref_attrtype;

/** @This stores a reference to a ubuf with related attributes.
 *
 * The structure is not refcounted and shouldn't be used by more than one
 * module at once.
 */
struct uref {
    /** structure for double-linked lists */
    struct uchain uchain;
    /** pointer to the entity responsible for the management */
    struct uref_mgr *mgr;

    /** pointer to (potentially shared) buffer */
    struct ubuf *ubuf;
};

/** @This stores common management parameters for a uref pool.
 */
struct uref_mgr {
    /** refcount management structure */
    urefcount refcount;

    /** minimum size of a control struct uref */
    size_t control_attr_size;

    /** function to allocate a uref with a given attr_size,
     * returns NULL on error */
    struct uref *(*uref_alloc)(struct uref_mgr *, size_t);
    /** function to duplicate a ubuf */
    struct uref *(*uref_dup)(struct uref_mgr *, struct uref *);
    /** function to free a uref */
    void (*uref_free)(struct uref *);

    /** function to get an attribute */
    const uint8_t *(*uref_attr_get)(struct uref *, const char *,
                                    enum uref_attrtype, size_t *);
    /** function to set an attribute */
    uint8_t *(*uref_attr_set)(struct uref **, const char *,
                              enum uref_attrtype, size_t);
    /** function to delete an attribute */
    bool (*uref_attr_delete)(struct uref *, const char *, enum uref_attrtype);

    /** function to free the uref_mgr structure */
    void (*uref_mgr_free)(struct uref_mgr *);
};

/** @This allocates and initializes a new uref_t.
 *
 * @param mgr management structure for this buffer pool
 * @return allocated uref or NULL in case of allocation failure
 */
static inline struct uref *uref_alloc(struct uref_mgr *mgr)
{
    return mgr->uref_alloc(mgr, 0);
}

/** @This returns a new uref (without a ubuf) with extra attributes space.
 * This is typically useful for control messages.
 *
 * @param mgr management structure for this uref pool
 * @return allocated uref or NULL in case of allocation failure
 */
static inline struct uref *uref_alloc_control(struct uref_mgr *mgr)
{
    return mgr->uref_alloc(mgr, mgr->control_attr_size);
}

/** @This duplicates a uref.
 *
 * @param uref source structure to duplicate
 * @return duplicated uref or NULL in case of allocation failure
 */
static inline struct uref *uref_dup(struct uref_mgr *mgr, struct uref *uref)
{
    return uref->mgr->uref_dup(mgr, uref);
}

/** @This frees a uref and decrements the refcount of ubuf.
 *
 * @param uref structure to free
 */
static inline void uref_release(struct uref *uref)
{
    if (uref->ubuf != NULL)
        ubuf_release(uref->ubuf);
    uref->mgr->uref_free(uref);
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

/** @internal @This returns a new uref pointing to a new ubuf.
 *
 * @param uref_mgr management structure for this uref pool
 * @param ubuf_mgr management structure for this ubuf pool
 * @param alloc_type sentinel defining the type of buffer to allocate,
 * followed by optional arguments to the ubuf manager
 * @return pointer to struct uref or NULL in case of failure
 */
static inline struct uref *uref_ubuf_alloc(struct uref_mgr *uref_mgr,
                                           struct ubuf_mgr *ubuf_mgr,
                                           enum ubuf_alloc_type alloc_type, ...)
{
    va_list args;
    struct uref *uref = uref_alloc(uref_mgr);
    if (unlikely(uref == NULL)) return NULL;

    va_start(args, alloc_type);
    uref->ubuf = ubuf_alloc_va(ubuf_mgr, alloc_type, args);
    va_end(args);
    if (unlikely(uref->ubuf == NULL)) {
        uref_release(uref);
        return NULL;
    }
    return uref;
}

/** @This makes the ubuf pointed to by the uref writable.
 *
 * @param uref struct uref structure
 * @param ubuf_mgr management structure in case the allocation of a new ubuf
 * is necessary (it must store data in the same manner as the original manager)
 * @return false in case of allocation error
 */
static inline bool uref_ubuf_writable(struct uref *uref,
                                      struct ubuf_mgr *ubuf_mgr)
{
    assert(uref->ubuf != NULL);
    return ubuf_writable(ubuf_mgr, &uref->ubuf);
}

/** @This increments the reference count of a uref_mgr.
 *
 * @param mgr pointer to uref_mgr
 */
static inline void uref_mgr_use(struct uref_mgr *mgr)
{
    urefcount_use(&mgr->refcount);
}

/** @This decrements the reference count of a uref_mgr, and frees it when it
 * gets down to 0.
 *
 * @param mgr pointer to uref_mgr
 */
static inline void uref_mgr_release(struct uref_mgr *mgr)
{
    if (unlikely(urefcount_release(&mgr->refcount)))
        mgr->uref_mgr_free(mgr);
}

#endif
