/*
 * Copyright (C) 2018-2019 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe NIC PTP implementation of uclock
 */

#ifndef _UPIPE_UCLOCK_PTP_H_
/** @hidden */
#define _UPIPE_UCLOCK_PTP_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uclock.h"
#include "upipe/uprobe.h"

/** @This allocates a new uclock structure.
 *
 * @param uprobe probe catching log events for error reporting
 * @param interface NIC names, or NULL
 * @return pointer to uclock, or NULL in case of error
 */
struct uclock *uclock_ptp_alloc(struct uprobe *uprobe, const char *interface[2]);

#ifdef __cplusplus
}
#endif
#endif
