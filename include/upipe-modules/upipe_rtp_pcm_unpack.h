#ifndef _UPIPE_MODULES_UPIPE_RTP_PCM_UNPACK_H_
# define _UPIPE_MODULES_UPIPE_RTP_PCM_UNPACK_H_

#ifdef __cplusplus
extern "C" {
#endif

#define UPIPE_RTP_PCM_UNPACK_SIGNATURE UBASE_FOURCC('r','t','p','u')

struct upipe_mgr *upipe_rtp_pcm_unpack_mgr_alloc(void);

#ifdef __cplusplus
}
#endif

#endif
