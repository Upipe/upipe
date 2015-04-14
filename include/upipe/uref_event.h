/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
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
 * @short Upipe event attributes for uref and control messages
 */

#ifndef _UPIPE_UREF_EVENT_H_
/** @hidden */
#define _UPIPE_UREF_EVENT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uref.h>
#include <upipe/uref_attr.h>

#include <stdint.h>
#include <stdbool.h>

UREF_ATTR_UNSIGNED_SH(event, events, UDICT_TYPE_EVENT_EVENTS, event number)
UREF_ATTR_UNSIGNED_VA(event, id, "e.id[%"PRIu64"]", event ID,
        uint64_t event, event)
UREF_ATTR_UNSIGNED_VA(event, start, "e.start[%"PRIu64"]", event start time,
        uint64_t event, event)
UREF_ATTR_UNSIGNED_VA(event, duration, "e.dur[%"PRIu64"]", event duration,
        uint64_t event, event)
UREF_ATTR_STRING_VA(event, language, "e.lang[%"PRIu64"]",
        event ISO-639 language, uint64_t event, event)
UREF_ATTR_STRING_VA(event, name, "e.name[%"PRIu64"]", event name,
        uint64_t event, event)
UREF_ATTR_STRING_VA(event, description, "e.desc[%"PRIu64"]", event description,
        uint64_t event, event)

#ifdef __cplusplus
}
#endif
#endif
