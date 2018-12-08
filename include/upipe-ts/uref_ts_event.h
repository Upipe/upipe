/*
 * Copyright (C) 2015-2017 OpenHeadend S.A.R.L.
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
 * @short Upipe event attributes for TS
 */

#ifndef _UPIPE_TS_UREF_TS_EVENT_H_
/** @hidden */
#define _UPIPE_TS_UREF_TS_EVENT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/uref_event.h>
#include <upipe-ts/uref_ts_attr.h>

#include <string.h>
#include <stdint.h>

UREF_ATTR_SMALL_UNSIGNED_VA(ts_event, running_status, "te.run[%" PRIu64"]",
        event running status, uint64_t event, event)
UREF_ATTR_VOID_VA(ts_event, scrambled, "te.ca[%" PRIu64"]", scrambled event,
        uint64_t event, event)
UREF_ATTR_UNSIGNED_VA(ts_event, descriptors, "te.descs[%" PRIu64"]",
        number of event descriptors, uint64_t event, event)
UREF_TS_ATTR_SUBDESCRIPTOR(ts_event, descriptor,
        "te.desc[%" PRIu64"][%" PRIu64"]")

/** @internal @This imports events into a flow def.
 *
 * @param uref1 output flow definition
 * @param uref2 uref containing events to import
 * @param event_p event number to start with
 */
static inline int uref_ts_event_import(struct uref *uref1, struct uref *uref2,
                                       uint64_t *event_p)
{
    uint64_t events;
    UBASE_RETURN(uref_event_get_events(uref2, &events))

    for (uint64_t event = 0; event < events; event++) {
        uint64_t tmp;
        if (ubase_check(uref_event_get_id(uref2, &tmp, event)))
            uref_event_set_id(uref1, tmp, *event_p);
        if (ubase_check(uref_event_get_start(uref2, &tmp, event)))
            uref_event_set_start(uref1, tmp, *event_p);
        if (ubase_check(uref_event_get_duration(uref2, &tmp, event)))
            uref_event_set_duration(uref1, tmp, *event_p);

        const char *str;
        if (ubase_check(uref_event_get_language(uref2, &str, event)))
            uref_event_set_language(uref1, str, *event_p);
        if (ubase_check(uref_event_get_name(uref2, &str, event)))
            uref_event_set_name(uref1, str, *event_p);
        if (ubase_check(uref_event_get_description(uref2, &str, event)))
            uref_event_set_description(uref1, str, *event_p);

        uint8_t small;
        if (ubase_check(uref_ts_event_get_running_status(uref2, &small, event)))
            uref_ts_event_set_running_status(uref1, small, *event_p);

        if (ubase_check(uref_ts_event_get_scrambled(uref2, event)))
            uref_ts_event_set_scrambled(uref1, *event_p);

        uint64_t descriptors = 0;
        if (ubase_check(uref_ts_event_get_descriptors(uref2, &descriptors, event)))
            uref_ts_event_set_descriptors(uref1, descriptors, *event_p);

        for (uint64_t descriptor = 0; descriptor < descriptors; descriptor++) {
            const uint8_t *p;
            size_t size;
            if (ubase_check(uref_ts_event_get_descriptor(uref2, &p, &size,
                                                         event, descriptor)))
                uref_ts_event_set_descriptor(uref1, p, size,
                                             *event_p, descriptor);
        }

        (*event_p)++;
    }

    UBASE_RETURN(uref_event_set_events(uref1, *event_p))
    return UBASE_ERR_NONE;
}

#ifdef __cplusplus
}
#endif
#endif
