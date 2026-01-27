/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe event attributes for uref and control messages
 */

#ifndef _UPIPE_UREF_EVENT_H_
/** @hidden */
#define _UPIPE_UREF_EVENT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uref.h"
#include "upipe/uref_attr.h"

#include <stdint.h>
#include <stdbool.h>

UREF_ATTR_UNSIGNED_SH(event, events, UDICT_TYPE_EVENT_EVENTS, event number)
UREF_ATTR_UNSIGNED_VA(event, id, "e.id[%" PRIu64"]", event ID,
        uint64_t event, event)
UREF_ATTR_UNSIGNED_VA(event, start, "e.start[%" PRIu64"]", event start time,
        uint64_t event, event)
UREF_ATTR_UNSIGNED_VA(event, duration, "e.dur[%" PRIu64"]", event duration,
        uint64_t event, event)
UREF_ATTR_STRING_VA(event, language, "e.lang[%" PRIu64"]",
        event ISO-639 language, uint64_t event, event)
UREF_ATTR_STRING_VA(event, name, "e.name[%" PRIu64"]", event name,
        uint64_t event, event)
UREF_ATTR_STRING_VA(event, description, "e.desc[%" PRIu64"]", event description,
        uint64_t event, event)

#ifdef __cplusplus
}
#endif
#endif
