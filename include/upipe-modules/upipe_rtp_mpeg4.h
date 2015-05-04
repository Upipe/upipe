#ifndef _UPIPE_MODULES_RTP_MPEG4_H_
# define UPIPE_MODULES_RTP_MPEG4_H_

#ifdef __cplusplus
extern "C" {
#endif

#define UPIPE_RTP_MPEG4_SIGNATURE UBASE_FOURCC('r','t','p','m')

struct upipe_mgr *upipe_rtp_mpeg4_mgr_alloc(void);

#ifdef __cplusplus
}
#endif

#endif
