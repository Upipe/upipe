/*
 * Copyright (C) 2018 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module decoding the entitlement management message table
 */

#ifndef _UPIPE_TS_UPIPE_TS_EMM_DECODER_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_EMM_DECODER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"
#include "upipe-ts/upipe_ts_demux.h"

#define UPIPE_TS_EMMD_SIGNATURE UBASE_FOURCC('t','s','e','m')
#define UPIPE_TS_EMMD_ECM_SIGNATURE UBASE_FOURCC('t','s','e','c')

/** @This extends uprobe_event with specific events for ECM. */
enum uprobe_ts_emmd_ecm_event {
    UPROBE_TS_EMMD_ECM_SENTINEL = UPROBE_LOCAL,

    /** most recent even and odd keys (uint8_t[16], uint8_t[16]) */
    UPROBE_TS_EMMD_ECM_KEY_UPDATE,
};

/** @This extends upipe_command with specific commands for EMM . */
enum upipe_ts_emmd_command {
    UPIPE_TS_EMM_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** sets the private key file (const char *) */
    UPIPE_TS_EMM_SET_PRIVATE_KEY,
};

/** @This sets the BISS-CA private key.
 *
 * @param upipe description structure of the pipe
 * @param private_key the private_key file
 * @return an error code
 */
static inline int upipe_ts_emmd_set_private_key(struct upipe *upipe,
        const char *private_key)
{
    return upipe_control(upipe, UPIPE_TS_EMM_SET_PRIVATE_KEY,
            UPIPE_TS_EMMD_SIGNATURE, private_key);
}

/** @This returns the management structure for all ts_emmd pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_emmd_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
