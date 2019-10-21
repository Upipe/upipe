/*
 * Copyright (C) 2019 OpenHeadend S.A.R.L.
 *
 * Authors: Arnaud de Turckheim
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/** @file
 * @short Upipe sound flow format definitions and helpers.
 */

#ifndef _UPIPE_UREF_SOUND_FLOW_FORMAT_H_
/** @hidden */
#define _UPIPE_UREF_SOUND_FLOW_FORMAT_H_
#ifdef __cplusplus
extern "C" {
#endif

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
