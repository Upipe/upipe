#ifndef _UPIPE_MODULES_UPIPE_RTCP_H_
# define _UPIPE_MODULES_UPIPE_RTCP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>

#define UPIPE_RTCP_SIGNATURE UBASE_FOURCC('r','t','c','p')

struct upipe_mgr *upipe_rtcp_mgr_alloc(void);

enum upipe_rtcp_command {
    UPIPE_RTCP_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** set rtcp clockrate (uint32_t) */
    UPIPE_RTCP_SET_CLOCKRATE,
    /** set rtcp clockrate (uint32_t*) */
    UPIPE_RTCP_GET_CLOCKRATE,

    /** set rtcp sr send rate (uint64_t) */
    UPIPE_RTCP_SET_RATE,
    /** get rtcp sr send rate (uint64_t) */
    UPIPE_RTCP_GET_RATE,
};

static inline int upipe_rtcp_get_clockrate(struct upipe *upipe,
                                      uint32_t *clockrate_p)
{
    return upipe_control(upipe, UPIPE_RTCP_GET_CLOCKRATE,
                         UPIPE_RTCP_SIGNATURE, clockrate_p);
}

static inline int upipe_rtcp_set_clockrate(struct upipe *upipe,
                                           uint32_t clockrate)
{
    return upipe_control(upipe, UPIPE_RTCP_SET_CLOCKRATE,
                         UPIPE_RTCP_SIGNATURE, clockrate);
}

static inline int upipe_rtcp_get_rate(struct upipe *upipe, uint64_t *rate_p)
{
    return upipe_control(upipe, UPIPE_RTCP_GET_RATE,
                         UPIPE_RTCP_SIGNATURE, rate_p);
}

static inline int upipe_rtcp_set_rate(struct upipe *upipe, uint64_t rate)
{
    return upipe_control(upipe, UPIPE_RTCP_SET_RATE,
                         UPIPE_RTCP_SIGNATURE, rate);
}

#ifdef __cplusplus
}
#endif
#endif
