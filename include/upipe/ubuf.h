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
 * @short Upipe buffer handling
 * This file defines the API to access buffers and buffer managers.
 */

#ifndef _UPIPE_UBUF_H_
/** @hidden */
#define _UPIPE_UBUF_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/urefcount.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <assert.h>

/** @hidden */
struct ubuf_mgr;

/** @This is allocated by a manager and eventually points to a buffer
 * containing data. */
struct ubuf {
    /** structure for double-linked lists */
    struct uchain uchain;
    /** pointer to the entity responsible for the management */
    struct ubuf_mgr *mgr;
};

UBASE_FROM_TO(ubuf, uchain, uchain, uchain)

/** @This is a simple signature to make sure the ubuf_alloc internal API
 * is used properly. */
enum ubuf_alloc_type {
    /** block (int) */
    UBUF_ALLOC_BLOCK,
    /** picture (int, int) */
    UBUF_ALLOC_PICTURE,

    /** non-standard ubuf allocators can start from there */
    UBUF_ALLOC_LOCAL = 0x8000
};

/** @This defines standard commands which ubuf managers may implement. */
enum ubuf_command {
    /*
     * Main commands
     */
    /** duplicate a given ubuf (struct ubuf **) */
    UBUF_DUP,
    /** returns true if the memory area's refcount is 1 (void) */
    UBUF_SINGLE,

    /*
     * Size commands (do not map data)
     */
    /** size picture ubuf (size_t *, size_t *, uint8_t *) */
    UBUF_SIZE_PICTURE,
    /** size a plane of a picture ubuf (const char *, size_t *,
     * uint8_t *, uint8_t *, uint8_t *) */
    UBUF_SIZE_PICTURE_PLANE,

    /*
     * Map commands
     */
    /** map block ubuf (const uint8_t **) */
    UBUF_MAP_BLOCK,
    /** read a plane of a picture ubuf (const char *, int, int, int, int,
     * const uint8_t **) */
    UBUF_READ_PICTURE_PLANE,
    /** write a plane of a picture ubuf (const char *, int, int, int, int,
     * uint8_t **) */
    UBUF_WRITE_PICTURE_PLANE,

    /*
     * Unmap commands
     */
    /** unmap block ubuf (void) */
    UBUF_UNMAP_BLOCK,
    /** unmap a plane of a picture ubuf (const char *, int, int, int, int) */
    UBUF_UNMAP_PICTURE_PLANE,

    /*
     * Resize commands
     */
    /** duplicates and resize block ubuf (struct ubuf **, int) */
    UBUF_SPLICE_BLOCK,
    /** resize picture ubuf (int, int, int, int) */
    UBUF_RESIZE_PICTURE,

    /*
     * Other standard commands
     */
    /** iterate on picture plane chroma (const char **) */
    UBUF_ITERATE_PICTURE_PLANE,

    /** non-standard commands implemented by a ubuf manager can start from
     * there */
    UBUF_CONTROL_LOCAL = 0x8000
};

/** @This defines standard manger commands which ubuf managers may implement. */
enum ubuf_mgr_command {
    /** release all buffers kept in pools (void) */
    UBUF_MGR_VACUUM,

    /** non-standard commands implemented by a ubuf manager can start from
     * there */
    UBUF_MGR_CONTROL_LOCAL = 0x8000
};

/** @This stores common management parameters for a ubuf pool.
 */
struct ubuf_mgr {
    /** pointer to refcount management structure */
    struct urefcount *refcount;
    /** type of allocator */
    enum ubuf_alloc_type type;

    /** function to allocate a new ubuf, with optional arguments depending
     * on the ubuf manager */
    struct ubuf *(*ubuf_alloc)(struct ubuf_mgr *, enum ubuf_alloc_type,
                               va_list);
    /** control function for standard or local commands */
    bool (*ubuf_control)(struct ubuf *, enum ubuf_command, va_list);
    /** function to free a ubuf */
    void (*ubuf_free)(struct ubuf *);

    /** manager control function for standard or local commands */
    bool (*ubuf_mgr_control)(struct ubuf_mgr *, enum ubuf_mgr_command, va_list);
};

/** @internal @This returns a new ubuf. Optional ubuf manager
 * arguments can be passed at the end.
 *
 * @param mgr management structure for this ubuf type
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

/** @internal @This sends a control command to the ubuf manager.
 *
 * @param ubuf pointer to ubuf
 * @param command control command to send, followed by optional read or write
 * parameters
 * @return false in case of error
 */
static inline bool ubuf_control(struct ubuf *ubuf,
                                enum ubuf_command command, ...)
{
    bool ret;
    va_list args;
    va_start(args, command);
    ret = ubuf->mgr->ubuf_control(ubuf, command, args);
    va_end(args);
    return ret;
}

/** @This duplicates a given ubuf (it is very likely that the manager
 * doesn't actually duplicate data but just create references and increment
 * reference counts).
 *
 * @param ubuf pointer to ubuf
 * @return duplicated ubuf
 */
static inline struct ubuf *ubuf_dup(struct ubuf *ubuf)
{
    struct ubuf *dup_ubuf;
    if (unlikely(!ubuf_control(ubuf, UBUF_DUP, &dup_ubuf)))
        return NULL;
    return dup_ubuf;
}

/** @This frees a ubuf.
 *
 * @param ubuf pointer to ubuf
 */
static inline void ubuf_free(struct ubuf *ubuf)
{
    ubuf->mgr->ubuf_free(ubuf);
}

/** @internal @This sends a manager control command to the ubuf manager.
 *
 * @param mgr pointer to ubuf manager
 * @param command control command to send, followed by optional read or write
 * parameters
 * @return false in case of error
 */
static inline bool ubuf_mgr_control(struct ubuf_mgr *mgr,
                                    enum ubuf_command command, ...)
{
    bool ret;
    va_list args;
    va_start(args, command);
    ret = mgr->ubuf_mgr_control(mgr, command, args);
    va_end(args);
    return ret;
}

/** @This instructs an existing ubuf manager to release all structures currently
 * kept in pools. It is inteded as a debug tool only.
 *
 * @param mgr pointer to ubuf manager
 */
static inline void ubuf_mgr_vacuum(struct ubuf_mgr *mgr)
{
    ubuf_mgr_control(mgr, UBUF_MGR_VACUUM);
}

/** @This increments the reference count of a ubuf manager.
 *
 * @param mgr pointer to ubuf manager
 * @return same pointer to ubuf manager
 */
static inline struct ubuf_mgr *ubuf_mgr_use(struct ubuf_mgr *mgr)
{
    assert(mgr != NULL);
    urefcount_use(mgr->refcount);
    return mgr;
}

/** @This decrements the reference count of a ubuf manager or frees it.
 *
 * @param mgr pointer to ubuf manager
 */
static inline void ubuf_mgr_release(struct ubuf_mgr *mgr)
{
    if (mgr != NULL)
        urefcount_release(mgr->refcount);
}

#ifdef __cplusplus
}
#endif
#endif
