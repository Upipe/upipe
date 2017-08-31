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

/** @This extends upipe_command with specific commands for avformat source. */
enum upipe_sdi_enc_sink_command {
    UPIPE_SDI_ENC_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the subpic subpipe (struct upipe **) */
    UPIPE_SDI_ENC_GET_SUBPIC_SUB,
};

/** @This returns the subpic subpipe. The refcount is not incremented so you
 * have to use it if you want to keep the pointer.
 *
 * @param upipe description structure of the super pipe
 * @param upipe_p filled in with a pointer to the subpic subpipe
 * @return an error code
 */
static inline int upipe_sdi_enc_get_subpic_sub(struct upipe *upipe,
        struct upipe **upipe_p)
{
    return upipe_control(upipe, UPIPE_SDI_ENC_GET_SUBPIC_SUB,
            UPIPE_SDI_ENC_SIGNATURE, upipe_p);
}

/** @This allocates and initializes a sdi enc pipe.
 *
 * @param mgr management structure for sdi enc type
 * @param uprobe structure used to raise events for the super pipe
 * @param uprobe_pic structure used to raise events for the pic subpipe
 * @param uprobe_subpic structure used to raise events for the subpic subpipe
 * @return pointer to allocated pipe, or NULL in case of failure
 */
static inline struct upipe *upipe_sdi_enc_alloc(struct upipe_mgr *mgr,
                                                    struct uprobe *uprobe,
                                                    struct uprobe *uprobe_subpic)
{
    return upipe_alloc(mgr, uprobe, UPIPE_SDI_ENC_SIGNATURE,
                        uprobe_subpic);
}

/** @This returns the management structure for sdi_enc pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_sdi_enc_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
