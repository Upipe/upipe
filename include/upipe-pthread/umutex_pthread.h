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

#ifndef _UPIPE_PTHREAD_UMUTEX_PTHREAD_H_
/** @hidden */
#define _UPIPE_PTHREAD_UMUTEX_PTHREAD_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/umutex.h"

#include <pthread.h>

/** @This allocates a new umutex structure using pthread primitives.
 *
 * @param mutexattr pthread mutex attributes, or NULL
 * @return pointer to umutex, or NULL in case of error
 */
struct umutex *umutex_pthread_alloc(const pthread_mutexattr_t *mutexattr);

#ifdef __cplusplus
}
#endif
#endif
