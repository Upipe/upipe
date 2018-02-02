/** @file
 * @short Upipe netmap source module
 */

#ifndef _UPIPE_NETMAP_UPIPE_NETMAP_SOURCE_H_
/** @hidden */
#define _UPIPE_NETMAP_UPIPE_NETMAP_SOURCE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_NETMAP_SOURCE_SIGNATURE UBASE_FOURCC('n','t','m','s')
/** @This returns the management structure for netmap_source pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_netmap_source_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
