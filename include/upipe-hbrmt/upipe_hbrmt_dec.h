/** @file
 * @short Upipe hbrmt_dec module
 */

#ifndef _UPIPE_MODULES_UPIPE_HBRMT_DEC_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_HBRMT_DEC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_HBRMT_DEC_SIGNATURE UBASE_FOURCC('h','b','r','d')
/** @This returns the management structure for hbrmt_dec pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_hbrmt_dec_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
