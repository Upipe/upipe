/*
 * Copyright (C) 2017 Open Broadcast Systems Ltd
 *
 * Authors: Judah Rand
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe block_to_sound module - converts incoming block urefs to outgoing sound urefs
 */

#ifndef _UPIPE_MODULES_UPIPE_BLOCK_TO_SOUND_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_BLOCK_TO_SOUND_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

/** @This is the signature of the block to sound pipe. */
#define UPIPE_BLOCK_TO_SOUND_SIGNATURE UBASE_FOURCC('b', 't', 'o', 's')

/** @This extends upipe_command with specific commands for null */
enum upipe_block_to_sound_command {
    UPIPE_BLOCK_TO_SOUND_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** ask enable or disable dumping dicts (int) */
    UPIPE_BLOCK_TO_SOUND_DUMP_DICT
};

/** @This enables or disables dumping of uref->udict
 *
 * @param upipe description structure of the pipe
 * @param enable enable or disable
 * @return an error code
 */
static inline int upipe_block_to_sound_dump_dict(struct upipe *upipe, bool enable)
{
    return upipe_control(upipe, UPIPE_BLOCK_TO_SOUND_DUMP_DICT, UPIPE_BLOCK_TO_SOUND_SIGNATURE, (enable ? 1 : 0));
}

/** @This returns the management structure for null pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_block_to_sound_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
