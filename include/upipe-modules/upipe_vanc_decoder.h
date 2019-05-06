#ifndef _UPIPE_MODULES_UPIPE_VANC_DECODER_H_
# define _UPIPE_MODULES_UPIPE_VANC_DECODER_H_
#ifdef __cplusplus
extern "C" {
#endif

#define UPIPE_VANC_DECODER_SIGNATURE     UBASE_FOURCC('a','n','c','d')

struct upipe_mgr *upipe_vancd_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_MODULES_UPIPE_VANC_DECODER_H_ */
