/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _UPIPE_HLS_UPIPE_HLS_MASTER_H_
# define _UPIPE_HLS_UPIPE_HLS_MASTER_H_
#ifdef __cplusplus
extern "C" {
#endif

#define UPIPE_HLS_MASTER_SIGNATURE      UBASE_FOURCC('h','l','s','M')
#define UPIPE_HLS_MASTER_SUB_SIGNATURE  UBASE_FOURCC('h','l','s','m')

/** @This allocates a hls master pipe manager.
 *
 * @return the pipe manager.
 */
struct upipe_mgr *upipe_hls_master_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
