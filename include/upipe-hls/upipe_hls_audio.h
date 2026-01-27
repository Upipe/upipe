/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _UPIPE_HLS_UPIPE_HLS_AUDIO_H_
# define _UPIPE_HLS_UPIPE_HLS_AUDIO_H_
#ifdef __cplusplus
extern "C" {
#endif

#define UPIPE_HLS_AUDIO_SIGNATURE UBASE_FOURCC('h','l','s','a')

struct upipe_mgr *upipe_hls_audio_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_HLS_UPIPE_HLS_AUDIO_H_ */
