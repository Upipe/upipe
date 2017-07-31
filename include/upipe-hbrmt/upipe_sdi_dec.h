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
#define UPIPE_SDI_DEC_SUB_SIGNATURE UBASE_FOURCC('s','d','i','s')

/** @This extends upipe_command with specific commands for sdi_dec pipes. */
enum upipe_sdi_dec_command {
    UPIPE_SDI_DEC_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the vanc subpipe (struct upipe **) */
    UPIPE_SDI_DEC_GET_VANC_SUB,

    /** returns the vbi subpipe (struct upipe **) */
    UPIPE_SDI_DEC_GET_VBI_SUB,

    /** returns the audio subpipe (struct upipe **) */
    UPIPE_SDI_DEC_GET_AUDIO_SUB,
};

/** @This returns the audio subpipe. The refcount is not incremented so you
 * have to use it if you want to keep the pointer.
 *
 * @param upipe description structure of the super pipe
 * @param upipe_p filled in with a pointer to the audio subpipe
 * @return an error code
 */
static inline int upipe_sdi_dec_get_audio_sub(struct upipe *upipe,
                                         struct upipe **upipe_p)
{
    return upipe_control(upipe, UPIPE_SDI_DEC_GET_AUDIO_SUB,
                         UPIPE_SDI_DEC_SIGNATURE, upipe_p);
}

/** @This returns the vbi subpipe. The refcount is not incremented so you
 * have to use it if you want to keep the pointer.
 *
 * @param upipe description structure of the super pipe
 * @param upipe_p filled in with a pointer to the vbi subpipe
 * @return an error code
 */
static inline int upipe_sdi_dec_get_vbi_sub(struct upipe *upipe,
                                         struct upipe **upipe_p)
{
    return upipe_control(upipe, UPIPE_SDI_DEC_GET_VBI_SUB,
                         UPIPE_SDI_DEC_SIGNATURE, upipe_p);
}

/** @This returns the vanc subpipe. The refcount is not incremented so you
 * have to use it if you want to keep the pointer.
 *
 * @param upipe description structure of the super pipe
 * @param upipe_p filled in with a pointer to the vanc subpipe
 * @return an error code
 */
static inline int upipe_sdi_dec_get_vanc_sub(struct upipe *upipe,
                                         struct upipe **upipe_p)
{
    return upipe_control(upipe, UPIPE_SDI_DEC_GET_VANC_SUB,
                         UPIPE_SDI_DEC_SIGNATURE, upipe_p);
}

/** @This returns the management structure for sdi_dec pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_sdi_dec_mgr_alloc(void);

/** @hidden */
#define ARGS_DECL , struct uprobe *uprobe_vanc, struct uprobe *uprobe_vbi, struct uprobe *uprobe_audio, \
    struct uref *flow_def
/** @hidden */
#define ARGS , uprobe_vanc, uprobe_vbi, uprobe_audio, flow_def
UPIPE_HELPER_ALLOC(sdi_dec, UPIPE_SDI_DEC_SIGNATURE)
#undef ARGS
#undef ARGS_DECL

#ifdef __cplusplus
}
#endif
#endif
