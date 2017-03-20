/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
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
 * @short Upipe umutex implementation using pthread
 */

#ifndef _UPIPE_PTHREAD_UMUTEX_PTHREAD_H_
/** @hidden */
#define _UPIPE_PTHREAD_UMUTEX_PTHREAD_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/umutex.h>

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
