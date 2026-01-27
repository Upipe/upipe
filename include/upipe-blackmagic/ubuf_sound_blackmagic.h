/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe ubuf manager for sound formats with blackmagic storage
 */

#ifndef _UPIPE_BLACKMAGIC_UBUF_SOUND_BLACKMAGIC_H_
/** @hidden */
#define _UPIPE_BLACKMAGIC_UBUF_SOUND_BLACKMAGIC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/ubase.h"
#include "upipe/ubuf.h"
#include "upipe/ubuf_sound.h"

#include <stdint.h>
#include <stdbool.h>

/** @This is a simple signature to make sure the ubuf_alloc internal API
 * is used properly. */
#define UBUF_BMD_ALLOC_SOUND UBASE_FOURCC('b','m','d','s')

/** @This returns a new ubuf from a blackmagic sound allocator.
 *
 * @param mgr management structure for this ubuf type
 * @param AudioFrame pointer to IDeckLinkAudioInputPacket
 * @return pointer to ubuf or NULL in case of failure
 */
static inline struct ubuf *ubuf_sound_bmd_alloc(struct ubuf_mgr *mgr,
                                                void *AudioFrame)
{
    return ubuf_alloc(mgr, UBUF_BMD_ALLOC_SOUND, AudioFrame);
}

/** @This allocates a new instance of the ubuf manager for sound formats
 * using blackmagic.
 *
 * @param ubuf_pool_depth maximum number of ubuf structures in the pool
 * @param SampleType blackmagic sample type
 * @param nb_channels number of channels
 * @param channel channel type (see channel reference)
 * @return pointer to manager, or NULL in case of error
 */
struct ubuf_mgr *ubuf_sound_bmd_mgr_alloc(uint16_t ubuf_pool_depth,
                                          uint32_t SampleType,
                                          uint8_t nb_channels,
                                          const char *channel);

#ifdef __cplusplus
}
#endif
#endif
