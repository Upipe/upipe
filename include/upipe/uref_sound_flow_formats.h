/*
 * Copyright (C) 2019 OpenHeadend S.A.R.L.
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe sound flow format definitions and helpers.
 */

#ifndef _UPIPE_UREF_SOUND_FLOW_FORMATS_H_
/** @hidden */
#define _UPIPE_UREF_SOUND_FLOW_FORMATS_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/** @This describes a sound format. */
struct uref_sound_flow_format {
    /** name */
    const char *name;
    /** sample size */
    uint8_t sample_size;
    /** format is planar? */
    bool planar;
};

#define UREF_SOUND_FLOW_FORMAT_DEFINE(NAME, SAMPLE_SIZE)    \
static const struct uref_sound_flow_format                  \
uref_sound_flow_format_##NAME = {                           \
    .name = "sound." #NAME ".",                             \
    .sample_size = SAMPLE_SIZE,                             \
    .planar = false                                         \
};                                                          \
static const struct uref_sound_flow_format                  \
uref_sound_flow_format_##NAME##_planar = {                  \
    .name = "sound." #NAME ".",                             \
    .sample_size = SAMPLE_SIZE,                             \
    .planar = true                                          \
}

UREF_SOUND_FLOW_FORMAT_DEFINE(u8, 1);
UREF_SOUND_FLOW_FORMAT_DEFINE(s16, 2);
UREF_SOUND_FLOW_FORMAT_DEFINE(s32, 4);
UREF_SOUND_FLOW_FORMAT_DEFINE(s64, 8);
UREF_SOUND_FLOW_FORMAT_DEFINE(f32, 4);
UREF_SOUND_FLOW_FORMAT_DEFINE(f64, 8);

UREF_SOUND_FLOW_FORMAT_DEFINE(u8be, 1);
UREF_SOUND_FLOW_FORMAT_DEFINE(s16be, 2);
UREF_SOUND_FLOW_FORMAT_DEFINE(s32be, 4);
UREF_SOUND_FLOW_FORMAT_DEFINE(s64be, 8);
UREF_SOUND_FLOW_FORMAT_DEFINE(f32be, 4);
UREF_SOUND_FLOW_FORMAT_DEFINE(f64be, 8);

UREF_SOUND_FLOW_FORMAT_DEFINE(u8le, 1);
UREF_SOUND_FLOW_FORMAT_DEFINE(s16le, 2);
UREF_SOUND_FLOW_FORMAT_DEFINE(s32le, 4);
UREF_SOUND_FLOW_FORMAT_DEFINE(s64le, 8);
UREF_SOUND_FLOW_FORMAT_DEFINE(f32le, 4);
UREF_SOUND_FLOW_FORMAT_DEFINE(f64le, 8);

#ifdef __cplusplus
}
#endif

#endif
