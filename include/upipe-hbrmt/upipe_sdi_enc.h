/** @file
 * @short Upipe sdi_enc module
 */

#ifndef _UPIPE_MODULES_UPIPE_SDI_ENC_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_SDI_ENC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_SDI_ENC_SIGNATURE UBASE_FOURCC('s','d','i','e')
#define UPIPE_SDI_ENC_SUB_SIGNATURE UBASE_FOURCC('s','d','i','s')
/** @This returns the management structure for sdi_enc pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_sdi_enc_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
