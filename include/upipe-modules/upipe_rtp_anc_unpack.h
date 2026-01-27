/*
 * Copyright (C) 2025 Open Broadcast Systems Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _UPIPE_MODULES_UPIPE_RTP_ANC_UNPACK_H_
# define _UPIPE_MODULES_UPIPE_RTP_ANC_UNPACK_H_

#ifdef __cplusplus
extern "C" {
#endif

#define UPIPE_RTP_ANC_UNPACK_SIGNATURE UBASE_FOURCC('r','t','p','a')

struct upipe_mgr *upipe_rtp_anc_unpack_mgr_alloc(void);

#ifdef __cplusplus
}
#endif

#endif
