/*
 * Copyright (C) 2023 EasyTools
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _UPIPE_MODULES_UPIPE_ID3V2_DECAPS_H_
# define _UPIPE_MODULES_UPIPE_ID3V2_DECAPS_H_
# ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uprobe.h"

#define UPIPE_ID3V2D_SIGNATURE   UBASE_FOURCC('i','d','3','d')

/** @This extends uprobe_event with specific events for id3v2_decaps. */
enum uprobe_id3v2d_event {
    UPROBE_ID3V2D_SENTINEL = UPROBE_LOCAL,

    /** an ID3v2 tag was found (struct uref *) */
    UPROBE_ID3V2D_TAG,
};

/** @This defines a helper to check id3v2d extended events. */
#define uprobe_id3v2d_check_extended(event, args, expected_event) \
    uprobe_check_extended(event, args, expected_event, UPIPE_ID3V2D_SIGNATURE)

/** @This checks if an event is the extended tag event.
 *
 * @param event event triggered by the pipe
 * @param args arguments of the event
 * @param uref_p filled with the tag if the event match
 * @return true if the event is the expected extended event, false otherwise
 */
static inline bool uprobe_id3v2d_check_tag(int event, va_list args,
                                           struct uref **uref_p)
{
    if (uprobe_id3v2d_check_extended(event, args, UPROBE_ID3V2D_TAG)) {
        struct uref *uref = va_arg(args, struct uref *);
        if (uref_p)
            *uref_p = uref;
        return true;
    }
    return false;
}

/** @This returns the id3v2 pipe manager. */
struct upipe_mgr *upipe_id3v2d_mgr_alloc(void);

# ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_MODULES_UPIPE_ID3V2_DECAPS_H_ */
