/** @file
 * @short Upipe sdi_dec module
 */

#ifndef _UPIPE_MODULES_UPIPE_UNPACK_RFC4175_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_UNPACK_RFC4175_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_UNPACK_RFC4175_SIGNATURE UBASE_FOURCC('4','7','5', 'u')
/** @This returns the management structure for unpack_rfc4175 pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_unpack_rfc4175_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
