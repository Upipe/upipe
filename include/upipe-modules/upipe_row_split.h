/*
 * Copyright (C) 2017-2018 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module to split pictures into a series of rows
 */

#ifndef _UPIPE_MODULES_UPIPE_ROW_SPLIT_H_
# define _UPIPE_MODULES_UPIPE_ROW_SPLIT_H_

#ifdef __cplusplus
extern "C" {
#endif

#define UPIPE_ROW_SPLIT_SIGNATURE UBASE_FOURCC('r','w','s','p')

struct upipe_mgr *upipe_row_split_mgr_alloc(void);

#ifdef __cplusplus
}
#endif

#endif
