/** @file
 * @short Upipe netmap_enc module
 */

#ifndef _UPIPE_MODULES_UPIPE_NETMAP_SINK_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_NETMAP_SINK_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_NETMAP_SINK_SIGNATURE UBASE_FOURCC('n','t','m','k')
/** @This returns the management structure for netmap_sink pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_netmap_sink_mgr_alloc(void);


/** @This extends upipe_command with specific commands for netmap sink. */
enum upipe_netmap_sink_command {
    UPIPE_NETMAP_SINK_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the uclock (struct uclock *) **/
    UPIPE_NETMAP_SINK_GET_UCLOCK,
};

static inline int upipe_netmap_sink_get_uclock(struct upipe *upipe,
        struct uclock **uclock_p)
{
    return upipe_control(upipe, UPIPE_NETMAP_SINK_GET_UCLOCK,
            UPIPE_NETMAP_SINK_SIGNATURE, uclock_p);
}


#ifdef __cplusplus
}
#endif
#endif
