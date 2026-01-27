/*
 * Copyright (C) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _UPIPE_MODULES_UPIPE_RTP_MPEG4_H_
/** @hidden */
# define _UPIPE_MODULES_UPIPE_RTP_MPEG4_H_

#ifdef __cplusplus
extern "C" {
#endif

#define UPIPE_RTP_MPEG4_SIGNATURE UBASE_FOURCC('r','t','p','m')

struct upipe_mgr *upipe_rtp_mpeg4_mgr_alloc(void);

#ifdef __cplusplus
}
#endif

#endif
