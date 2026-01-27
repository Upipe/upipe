/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe umutex implementation using pthread
 */

#include "upipe/ubase.h"
#include "upipe/urefcount.h"
#include "upipe/umutex.h"
#include "upipe-pthread/umutex_pthread.h"

#include <pthread.h>
#include <errno.h>

/** super-set of the umutex structure with additional local members */
struct umutex_pthread {
    /** refcount management structure */
    struct urefcount urefcount;

    /** pthread mutex */
    pthread_mutex_t mutex;

    /** structure exported to modules */
    struct umutex umutex;
};

UBASE_FROM_TO(umutex_pthread, umutex, umutex, umutex)
UBASE_FROM_TO(umutex_pthread, urefcount, urefcount, urefcount)

/** @This locks a mutex.
 *
 * @param umutex pointer to a umutex structure
 * @return an error code
 */
static inline int umutex_pthread_lock(struct umutex *umutex)
{
    struct umutex_pthread *umutex_pthread = umutex_pthread_from_umutex(umutex);
    int err = pthread_mutex_lock(&umutex_pthread->mutex);

    switch (err) {
        case 0: return UBASE_ERR_NONE;
        case EDEADLK: return UBASE_ERR_BUSY;
        default:
        case EINVAL: return UBASE_ERR_INVALID;
    }
}

/** @This unlocks a mutex.
 *
 * @param umutex pointer to a umutex structure
 * @return an error code
 */
static inline int umutex_pthread_unlock(struct umutex *umutex)
{
    struct umutex_pthread *umutex_pthread = umutex_pthread_from_umutex(umutex);
    int err = pthread_mutex_unlock(&umutex_pthread->mutex);

    switch (err) {
        case 0: return UBASE_ERR_NONE;
        case EPERM: return UBASE_ERR_BUSY;
        default:
        case EINVAL: return UBASE_ERR_INVALID;
    }
}

/** @This frees a umutex.
 *
 * @param urefcount pointer to urefcount
 */
static void umutex_pthread_free(struct urefcount *urefcount)
{
    struct umutex_pthread *umutex_pthread =
        umutex_pthread_from_urefcount(urefcount);
    pthread_mutex_destroy(&umutex_pthread->mutex);
    urefcount_clean(urefcount);
    free(umutex_pthread);
}

/** @This allocates a new umutex structure using pthread primitives.
 *
 * @param mutexattr pthread mutex attributes, or NULL
 * @return pointer to umutex, or NULL in case of error
 */
struct umutex *umutex_pthread_alloc(const pthread_mutexattr_t *mutexattr)
{
    struct umutex_pthread *umutex_pthread =
        malloc(sizeof(struct umutex_pthread));
    if (unlikely(umutex_pthread == NULL))
        return NULL;

    urefcount_init(umutex_pthread_to_urefcount(umutex_pthread),
                   umutex_pthread_free);
    umutex_pthread->umutex.refcount =
        umutex_pthread_to_urefcount(umutex_pthread);
    umutex_pthread->umutex.umutex_lock = umutex_pthread_lock;
    umutex_pthread->umutex.umutex_unlock = umutex_pthread_unlock;
    pthread_mutex_init(&umutex_pthread->mutex, mutexattr);
    return umutex_pthread_to_umutex(umutex_pthread);
}
