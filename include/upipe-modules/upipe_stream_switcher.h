#ifndef _UPIPE_MODULS_UPIPE_STREAM_SWITCHER_H_
# define _UPIPE_MODULS_UPIPE_STREAM_SWITCHER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <upipe/upipe.h>

#define UPIPE_STREAM_SWITCHER_SIGNATURE UBASE_FOURCC('s','t','s','w')
#define UPIPE_STREAM_SWITCHER_SUB_SIGNATURE UBASE_FOURCC('s','t','s','s')

struct upipe_mgr *upipe_stream_switcher_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
