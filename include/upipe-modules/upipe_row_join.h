#ifndef _UPIPE_MODULES_ROW_JOIN_H_
# define _UPIPE_MODULES_ROW_JOIN_H_

#ifdef __cplusplus
extern "C" {
#endif

#define UPIPE_ROW_JOIN_SIGNATURE UBASE_FOURCC('r','w','j','n')

struct upipe_mgr *upipe_row_join_mgr_alloc(void);

#ifdef __cplusplus
}
#endif

#endif
