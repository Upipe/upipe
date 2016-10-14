#ifndef _UPIPE_MODULES_RTP_PCM_PACK_H_
# define _UPIPE_MODULES_RTP_PCM_PACK_H_

#ifdef __cplusplus
extern "C" {
#endif

#define UPIPE_RTP_PCM_PACK_SIGNATURE UBASE_FOURCC('r','t','p','c')

struct upipe_mgr *upipe_rtp_pcm_pack_mgr_alloc(void);

#ifdef __cplusplus
}
#endif

#endif
