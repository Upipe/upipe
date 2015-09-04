#ifndef _UPIPE_MODULES_RTP_OPUS_H_
# define _UPIPE_MODULES_RTP_OPUS_H_

#ifdef __cplusplus
extern "C" {
#endif

#define UPIPE_RTP_OPUS_SIGNATURE UBASE_FOURCC('r','t','p','o')

struct upipe_mgr *upipe_rtp_opus_mgr_alloc(void);

#ifdef __cplusplus
}
#endif

#endif
