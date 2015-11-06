/*
 * Copyright (C) 2013-2015 OpenHeadend S.A.R.L.
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
 * @short common declarations for event loop handlers
 */

#ifndef _UPIPE_UPUMP_BLOCKER_H_
/** @hidden */
#define _UPIPE_UPUMP_BLOCKER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/upump.h>

/** function called when a pump is triggered */
typedef void (*upump_blocker_cb)(struct upump_blocker *);

/** @This stores the parameters of a blocker.
 */
struct upump_blocker {
    /** structure for double-linked lists */
    struct uchain uchain;
    /** blocked pump */
    struct upump *upump;

    /** function to call back when the pump is released */
    upump_blocker_cb cb;
    /** opaque pointer for the callback */
    void *opaque;
    /** pointer to urefcount structure to increment during callback */
    struct urefcount *refcount;
};

/** @This returns the high-level upump_blocker structure.
 *
 * @param uchain pointer to the uchain structure wrapped into the upump_blocker
 * @return pointer to the upump_blocker structure
 */
static inline struct upump_blocker *
    upump_blocker_from_uchain(struct uchain *uchain)
{
    return container_of(uchain, struct upump_blocker, uchain);
}

/** @This returns the uchain structure used for FIFO, LIFO and lists.
 *
 * @param upump_blocker upump_blocker structure
 * @return pointer to the uchain structure
 */
static inline struct uchain *
    upump_blocker_to_uchain(struct upump_blocker *upump_blocker)
{
    return &upump_blocker->uchain;
}

/** @This allocates and initializes a blocker.
 *
 * @param upump blocked pump
 * @param cb function to call when the pump is released
 * @param opaque pointer to the module's internal structure
 * @param refcount pointer to urefcount structure to increment during callback,
 * or NULL
 * @return pointer to allocated blocker, or NULL in case of failure
 */
static inline struct upump_blocker *upump_blocker_alloc(struct upump *upump,
        upump_blocker_cb cb, void *opaque, struct urefcount *refcount)
{
    struct upump_blocker *upump_blocker =
        upump->mgr->upump_blocker_alloc(upump);
    if (unlikely(upump_blocker == NULL))
        return NULL;

    uchain_init(&upump_blocker->uchain);
    upump_blocker->upump = upump;
    upump_blocker->cb = cb;
    upump_blocker->opaque = opaque;
    upump_blocker->refcount = refcount;
    return upump_blocker;
}

/** @This releases a blocker, and if allowed restarts the pump.
 *
 * @param blocker description structure of the blocker
 */
static inline void upump_blocker_free(struct upump_blocker *blocker)
{
    blocker->upump->mgr->upump_blocker_free(blocker);
}

/** @This gets the opaque structure with a cast.
 */
#define upump_blocker_get_opaque(blocker, type) (type)(blocker)->opaque

/** @This sets the callback parameters of an existing blocker.
 *
 * @param upump_blocker description structure of the blocker
 * @param cb function to call when the pump is released
 * @param opaque pointer to the module's internal structure
 */
static inline void upump_blocker_set_cb(struct upump_blocker *upump_blocker,
                                        upump_blocker_cb cb, void *opaque)
{
    upump_blocker->cb = cb;
    upump_blocker->opaque = opaque;
}

/** @This finds in a ulist if a blocker already exists for the given pump.
 *
 * @param ulist list of blockers
 * @param upump pump to block
 * @return a pointer to the blocker, or NULL if not found
 */
static inline struct upump_blocker *upump_blocker_find(struct uchain *ulist,
                                                       struct upump *upump)
{
    struct uchain *uchain;
    ulist_foreach (ulist, uchain) {
        struct upump_blocker *blocker = upump_blocker_from_uchain(uchain);
        if (blocker->upump == upump)
            return blocker;
    }
    return NULL;
}

#ifdef __cplusplus
}
#endif
#endif
