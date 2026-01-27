/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _UPIPE_MODULES_UPIPE_RTP_H264_H_
# define _UPIPE_MODULES_UPIPE_RTP_H264_H_

#ifdef __cplusplus
extern "C" {
#endif

#define UPIPE_RTP_H264_SIGNATURE UBASE_FOURCC('r','t','p','h')

struct upipe_mgr *upipe_rtp_h264_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
