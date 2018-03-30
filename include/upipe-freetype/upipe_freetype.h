#ifndef _UPIPE_FREETYPE_UPIPE_FREETYPE_H_
# define _UPIPE_FREETYPE_UPIPE_FREETYPE_H_
#ifdef __cplusplus
extern "C" {
#endif

#define UPIPE_FREETYPE_SIGNATURE UBASE_FOURCC('f','r','t','2')

/** @This returns the freetype pipes manager.
 *
 * @return a pointer to the freetype pipes manager
 */
struct upipe_mgr *upipe_freetype_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_FREETYPE_UPIPE_FREETYPE_H_ */
