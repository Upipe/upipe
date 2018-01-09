#ifndef _UPIPE_MODULES_UPIPE_AES_DECRYPT_H_
# define _UPIPE_MODULES_UPIPE_AES_DECRYPT_H_
#ifdef __cplusplus
extern "C" {
#endif

#define UPIPE_AES_DECRYPT_SIGNATURE     UBASE_FOURCC('a','e','s','d')

struct upipe_mgr *upipe_aes_decrypt_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_MODULES_UPIPE_AES_DECRYPT_H_ */
