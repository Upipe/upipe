/*
 * Copyright (C) 2018 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
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
 * @short Upipe PCIE SDI sink
 */

#ifndef _UPIPE_MODULES_UPIPE_PCIESDI_SINK_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_PCIESDI_SINK_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_PCIESDI_SINK_SIGNATURE UBASE_FOURCC('o', 'b', 'f', 'h')

/** @This extends upipe_command with specific commands. */
enum upipe_pciesdi_sink_command {
    UPIPE_PCIESDI_SINK_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the uclock (struct uclock *) **/
    UPIPE_PCIESDI_SINK_GET_UCLOCK,

    HACK_CONTROL_SET_CLOCK_SD,
    HACK_CONTROL_SET_CLOCK_HD_NTSC,
    HACK_CONTROL_SET_CLOCK_HD_PAL,
    HACK_CONTROL_SET_CLOCK_3G_NTSC,
    HACK_CONTROL_SET_CLOCK_3G_PAL,
};

/** @This returns the pciesdi uclock.
 *
 * @param upipe description structure of the super pipe
 * @param uclock_p filled in with a pointer to the uclock
 * @return an error code
 */
static inline int upipe_pciesdi_sink_get_uclock(struct upipe *upipe,
        struct uclock **uclock_p)
{
    return upipe_control(upipe, UPIPE_PCIESDI_SINK_GET_UCLOCK,
            UPIPE_PCIESDI_SINK_SIGNATURE, uclock_p);
}

#define HACK_CONTROL_SET_CLOCK_TEMPLATE(name, NAME, desc)                     \
/** @This instructs the pipe to configure for transmission of desc.           \
 *                                                                            \
 * @param upipe description structure of the pipe                             \
 * @return an error code                                                      \
 */                                                                           \
static inline int hack_control_set_clock_##name(struct upipe *upipe)          \
{                                                                             \
    return upipe_control(upipe, HACK_CONTROL_SET_CLOCK_##NAME,                \
            UPIPE_PCIESDI_SINK_SIGNATURE);                                    \
}

HACK_CONTROL_SET_CLOCK_TEMPLATE(sd, SD, Standard Definition)
HACK_CONTROL_SET_CLOCK_TEMPLATE(hd_ntsc, HD_NTSC, High Definition at NTSC frame rates)
HACK_CONTROL_SET_CLOCK_TEMPLATE(hd_pal,   HD_PAL, High Definition at PAL frame rates)
HACK_CONTROL_SET_CLOCK_TEMPLATE(3g_ntsc, 3G_NTSC, High Definition at NTSC frame rates greater than 30 fps)
HACK_CONTROL_SET_CLOCK_TEMPLATE(3g_pal,   3G_PAL, High Definition at PAL frame rates greater than 30 fps)

/** @This enumarates the private events for upipe_pciesdi_sink pipes. */
enum uprobe_pciesdi_sink_event {
    /** sentinel */
    UPROBE_PCIESDI_SINK_SENTINEL = UPROBE_LOCAL,

    /** genlock type (uint32_t) */
    UPROBE_PCIESDI_SINK_GENLOCK_TYPE,
};

enum uprobe_pciesdi_sink_genlock {
    UPROBE_PCIESDI_SINK_GENLOCK_NOT_CONFIGURED,
    UPROBE_PCIESDI_SINK_GENLOCK_CONFIGURED,
    UPROBE_PCIESDI_SINK_GENLOCK_IN_USE,
};

/** @This returns the management structure for pciesdi_sink pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_pciesdi_sink_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
