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
#include <upipe-ts/uref_ts_attr.h>

#include <string.h>
#include <stdint.h>

UREF_ATTR_SMALL_UNSIGNED_VA(ts_event, running_status, "te.run[%"PRIu64"]",
        event running status, uint64_t event, event)
UREF_ATTR_VOID_VA(ts_event, scrambled, "te.ca[%"PRIu64"]", scrambled event,
        uint64_t event, event)
UREF_ATTR_UNSIGNED_VA(ts_event, descriptors, "te.descs[%"PRIu64"]",
        number of event descriptors, uint64_t event, event)
UREF_TS_ATTR_SUBDESCRIPTOR(ts_event, descriptor,
        "te.desc[%"PRIu64"][%"PRIu64"]")

#ifdef __cplusplus
}
#endif
#endif
