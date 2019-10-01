#ifndef _UPIPE_MODULES_ROW_SPLIT_H_
# define _UPIPE_MODULES_ROW_SPLIT_H_

#ifdef __cplusplus
extern "C" {
#endif

#define UPIPE_ROW_SPLIT_SIGNATURE UBASE_FOURCC('r','w','s','p')

struct upipe_mgr *upipe_row_split_mgr_alloc(void);

#ifdef __cplusplus
}
#endif

#endif
