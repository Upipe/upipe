/** @file
 * @short Upipe pciesdi source framer
 */

#ifndef _UPIPE_HBRMT_UPIPE_PCIESDI_SOURCE_FRAMER_H_
/** @hidden */
#define _UPIPE_HBRMT_UPIPE_PCIESDI_SOURCE_FRAMER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_PCIESDI_SOURCE_FRAMER_SIGNATURE UBASE_FOURCC('p','c','s','f')

/** @This returns the management structure for sdi_dec pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_pciesdi_source_framer_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

