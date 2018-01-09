#ifndef _UPIPE_MODULES_UPIPE_SUBPIC_SCHEDULE_H_
# define _UPIPE_MODULES_UPIPE_SUBPIC_SCHEDULE_H_
#ifdef __cplusplus
extern "C" {
#endif

#define UPIPE_SUBPIC_SCHEDULE_SIGNATURE     UBASE_FOURCC('s','p','s','c')
#define UPIPE_SUBPIC_SCHEDULE_SUB_SIGNATURE UBASE_FOURCC('s','p','s','s')

struct upipe_mgr *upipe_subpic_schedule_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_MODULES_UPIPE_SUBPIC_SCHEDULE_H_ */
