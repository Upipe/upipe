/*****************************************************************************
 * ubuf.h: upipe ubuf structure handling
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

#ifndef _UPIPE_UBUF_H_
/** @hidden */
#define _UPIPE_UBUF_H_

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#include <upipe/ubase.h>
#include <upipe/urefcount.h>

/** @This stores a plane of data. */
struct ubuf_plane {
    /** if the plane is an array of data, stride between array elements
     * (lines of a picture, samples of an audio frame) */
    size_t stride;
    /** data */
    uint8_t *buffer;
};

/** @hidden */
struct ubuf_mgr;

/** @This stores an array of struct ubuf_plane.
 *
 * All buffers are supposed to describe the same temporal information
 * (like planes of a picture or channels of audio).
 */
struct ubuf {
    /** structure for double-linked lists */
    struct uchain uchain;
    /** pointer to the entity responsible for the management */
    struct ubuf_mgr *mgr;
    /** refcount management structure */
    urefcount refcount;

    /** planes descriptors */
    struct ubuf_plane planes[];
};

/** simple signature to make sure the API is used properly */
enum ubuf_alloc_type {
    UBUF_ALLOC_TYPE_OTHER,
    UBUF_ALLOC_TYPE_BLOCK,
    UBUF_ALLOC_TYPE_PICTURE,
    UBUF_ALLOC_TYPE_SOUND
};

/** @This stores common management parameters for a ubuf pool.
 */
struct ubuf_mgr {
    /** refcount management structure */
    urefcount refcount;

    /** function to allocate a new ubuf, with optional arguments depending
     * on the ubuf manager */
    struct ubuf *(*ubuf_alloc)(struct ubuf_mgr *, enum ubuf_alloc_type,
                               va_list);
    /** function to duplicate a ubuf */
    struct ubuf *(*ubuf_dup)(struct ubuf_mgr *, struct ubuf *);
    /** function to free a ubuf */
    void (*ubuf_free)(struct ubuf *);
    /** function to resize ubuf, with optional arguments depending
     * on the ubuf manager */
    bool (*ubuf_resize)(struct ubuf_mgr *, struct ubuf **, enum ubuf_alloc_type,
                        va_list);

    /** function to free the ubuf manager structure */
    void (*ubuf_mgr_free)(struct ubuf_mgr *);
};

/** @internal @This returns a new ubuf (vararg version). Optional ubuf
 * manager arguments can be passed at the end.
 *
 * @param mgr management structure for this ubuf pool
 * @param alloc_type sentinel defining the type of buffer to allocate,
 * followed by optional arguments to the ubuf manager
 * @return pointer to ubuf or NULL in case of failure
 */
static inline struct ubuf *ubuf_alloc_va(struct ubuf_mgr *mgr,
                                         enum ubuf_alloc_type alloc_type,
                                         va_list args)
{
    return mgr->ubuf_alloc(mgr, alloc_type, args);
}

/** @internal @This returns a new ubuf. Optional ubuf manager
 * arguments can be passed at the end.
 *
 * @param mgr management structure for this ubuf pool
 * @param alloc_type sentinel defining the type of buffer to allocate,
 * followed by optional arguments to the ubuf manager
 * @return pointer to ubuf or NULL in case of failure
 */
static inline struct ubuf *ubuf_alloc(struct ubuf_mgr *mgr,
                                      enum ubuf_alloc_type alloc_type, ...)
{
    struct ubuf *ubuf;
    va_list args;
    va_start(args, alloc_type);
    ubuf = mgr->ubuf_alloc(mgr, alloc_type, args);
    va_end(args);
    return ubuf;
}

/** @This increments the reference count of a ubuf.
 *
 * @param ubuf pointer to ubuf
 */
static inline void ubuf_use(struct ubuf *ubuf)
{
    urefcount_use(&ubuf->refcount);
}

/** @This decrements the reference count of a ubuf, and frees it when it
 * gets down to 0.
 *
 * @param ubuf pointer to ubuf
 */
static inline void ubuf_release(struct ubuf *ubuf)
{
    if (likely(urefcount_release(&ubuf->refcount)))
        ubuf->mgr->ubuf_free(ubuf);
}

/** @This checks if we are the single owner of the ubuf.
 *
 * @param ubuf pointer to ubuf
 * @return false if other references share this buffer
 */
static inline bool ubuf_single(struct ubuf *ubuf)
{
    return urefcount_single(&ubuf->refcount);
}

/** @This checks if the reference count of the ubuf is greater than 1,
 * and makes a writable copy it if it is the case.
 *
 * @param mgr management structure used to create the new ubuf (must store
 * data in the same manner as the original manager)
 * @param ubuf_p reference to a pointer to ubuf (possibly modified)
 * @return false in case of allocation error
 */
static inline bool ubuf_writable(struct ubuf_mgr *mgr, struct ubuf **ubuf_p)
{
    if (unlikely(!ubuf_single(*ubuf_p))) {
        struct ubuf *new_ubuf = (*ubuf_p)->mgr->ubuf_dup(mgr, *ubuf_p);
        if (new_ubuf == NULL) return false;
        ubuf_release(*ubuf_p);
        *ubuf_p = new_ubuf;
    }
    return true;
}

/** @This returns the high-level ubuf structure.
 *
 * @param uchain pointer to the uchain structure wrapped into the ubuf
 * @return pointer to the ubuf structure
 */
static inline struct ubuf *ubuf_from_uchain(struct uchain *uchain)
{
    return container_of(uchain, struct ubuf, uchain);
}

/** @This returns the uchain structure used for FIFO, LIFO and lists.
 *
 * @param ubuf struct ubuf structure
 * @return pointer to the uchain structure
 */
static inline struct uchain *ubuf_to_uchain(struct ubuf *ubuf)
{
    return &ubuf->uchain;
}

/** @internal @This resizes a ubuf. Optional ubuf manager
 * arguments can be passed at the end.
 *
 * @param mgr management structure used to create a new buffer, if needed
 * (can be NULL if ubuf_single(ubuf))
 * @param ubuf_p reference to a pointer to ubuf (possibly modified)
 * @param alloc_type magic number that allows to catch cast errors
 * @return pointer to ubuf or NULL in case of failure
 */
static inline bool ubuf_resize(struct ubuf_mgr *mgr, struct ubuf **ubuf_p,
                               enum ubuf_alloc_type alloc_type, ...)
{
    bool ret;
    va_list args;
    va_start(args, alloc_type);
    ret = (*ubuf_p)->mgr->ubuf_resize(mgr, ubuf_p, alloc_type, args);
    va_end(args);
    return ret;
}

/** @This increments the reference count of a ubuf manager.
 *
 * @param mgr pointer to ubuf manager
 */
static inline void ubuf_mgr_use(struct ubuf_mgr *mgr)
{
    urefcount_use(&mgr->refcount);
}

/** @This decrements the reference count of a ubuf manager, and frees it when it
 * gets down to 0.
 *
 * @param mgr pointer to ubuf manager
 */
static inline void ubuf_mgr_release(struct ubuf_mgr *mgr)
{
    if (unlikely(urefcount_release(&mgr->refcount)))
        mgr->ubuf_mgr_free(mgr);
}

#endif
