/** @file
 * @short Upipe sdi_dec module
 */

#ifndef _UPIPE_MODULES_UPIPE_SDI_DEC_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_SDI_DEC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_SDI_DEC_SIGNATURE UBASE_FOURCC('s','d','i','d')
#define UPIPE_SDI_DEC_SUB_SIGNATURE UBASE_FOURCC('s','d','d','s')
/** @This returns the management structure for sdi_dec pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_sdi_dec_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
