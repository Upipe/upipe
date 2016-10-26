/** @file
 * @short Upipe hbrmt_enc module
 */

#ifndef _UPIPE_MODULES_UPIPE_HBRMT_ENC_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_HBRMT_ENC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_HBRMT_ENC_SIGNATURE UBASE_FOURCC('h','b','r','e')
/** @This returns the management structure for hbrmt_enc pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_hbrmt_enc_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
