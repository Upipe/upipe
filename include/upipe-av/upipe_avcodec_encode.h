/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *          Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe avcodec encode wrapper module
 */

#ifndef _UPIPE_AV_UPIPE_AVCODEC_ENCODE_H_
/** @hidden */
#define _UPIPE_AV_UPIPE_AVCODEC_ENCODE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"
#include "upipe/uref_attr.h"

#define UPIPE_AVCENC_SIGNATURE UBASE_FOURCC('a', 'v', 'c', 'e')

UREF_ATTR_STRING(avcenc, codec_name, "avcenc.name", avcenc codec name)

/** @This extends upipe_command with specific commands for avcenc. */
enum upipe_avcenc_command {
    UPIPE_AVCENC_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** set slice type enforcement mode (int) */
    UPIPE_AVCENC_SET_SLICE_TYPE_ENFORCE
};

/** @This sets the slice type enforcement mode (true or false).
 *
 * @param upipe description structure of the pipe
 * @param enforce true if the incoming slice types must be enforced
 * @return an error code
 */
static inline int upipe_avcenc_set_slice_type_enforce(struct upipe *upipe,
                                                      bool enforce)
{
    return upipe_control(upipe, UPIPE_AVCENC_SET_SLICE_TYPE_ENFORCE,
                         UPIPE_AVCENC_SIGNATURE, enforce ? 1 : 0);
}

/** @This returns the management structure for avcodec encoders.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_avcenc_mgr_alloc(void);

/** @This extends upipe_mgr_command with specific commands for avcenc. */
enum upipe_avcenc_mgr_command {
    UPIPE_AVCENC_MGR_SENTINEL = UPIPE_MGR_CONTROL_LOCAL,

    /** sets the flow definition from codec name (struct uref *, const char *) */
    UPIPE_AVCENC_MGR_SET_FLOW_DEF_FROM_NAME
};

/** @This configures the given flow definition to be able to encode to the
 * av codec described by name.
 *
 * @param mgr pointer to manager
 * @param flow_def flow definition packet
 * @param name codec name
 * @return an error code
 */
static inline int upipe_avcenc_mgr_set_flow_def_from_name(
                                            struct upipe_mgr *mgr,
                                            struct uref *flow_def,
                                            const char *name)
{
    return upipe_mgr_control(mgr, UPIPE_AVCENC_MGR_SET_FLOW_DEF_FROM_NAME,
                             UPIPE_AVCENC_SIGNATURE, flow_def, name);
}

#ifdef __cplusplus
}
#endif
#endif
