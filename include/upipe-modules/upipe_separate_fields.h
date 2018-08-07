#ifndef _UPIPE_MODULES_SEPARATE_FIELDS_H_
# define _UPIPE_MODULES_SEPARATE_FIELDS_H_

#ifdef __cplusplus
extern "C" {
#endif

#define UPIPE_SEPARATE_FIELDS_SIGNATURE UBASE_FOURCC('s','e','p','f')

struct upipe_mgr *upipe_separate_fields_mgr_alloc(void);

#ifdef __cplusplus
}
#endif

#endif
