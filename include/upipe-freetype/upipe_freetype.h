#ifndef _UPIPE_MODULES_UPIPE_FREETYPE_H_
# define _UPIPE_MODULES_UPIPE_FREETYPE_H_
#ifdef __cplusplus
extern "C" {
#endif

#define UPIPE_FREETYPE_SIGNATURE UBASE_FOURCC('f','r','t','2')

struct upipe_mgr *upipe_freetype_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_MODULES_UPIPE_FREETYPE_H_ */
