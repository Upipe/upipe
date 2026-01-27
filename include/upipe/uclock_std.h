/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe standard implementation of uclock
 */

#ifndef _UPIPE_UCLOCK_STD_H_
/** @hidden */
#define _UPIPE_UCLOCK_STD_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uclock.h"

/** flags for the creation of a uclock structure */
enum uclock_std_flags {
    /** force using a real-time clock even if a monotonic clock is available */
    UCLOCK_FLAG_REALTIME = 0x1
};

/** @This allocates a new uclock structure.
 *
 * @param flags flags for the creation of a uclock structure
 * @return pointer to uclock, or NULL in case of error
 */
struct uclock *uclock_std_alloc(enum uclock_std_flags flags);

#ifdef __cplusplus
}
#endif
#endif
