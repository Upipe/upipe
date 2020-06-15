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
 * @short Upipe uref attributes for TS SCTE 35
 */

#ifndef _UPIPE_TS_UREF_TS_SCTE35_H_
/** @hidden */
#define _UPIPE_TS_UREF_TS_SCTE35_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe-ts/uref_ts_attr.h>

#include <string.h>
#include <stdint.h>

UREF_ATTR_SMALL_UNSIGNED(ts_scte35, command_type, "scte35.type", command type)
UREF_ATTR_UNSIGNED(ts_scte35, event_id, "scte35.id", event ID)
UREF_ATTR_VOID(ts_scte35, cancel, "scte35.cancel", cancel indicator)
UREF_ATTR_VOID(ts_scte35, out_of_network, "scte35.oon",
        out of network indicator)
UREF_ATTR_VOID(ts_scte35, auto_return, "scte35.autoreturn",
        auto return indicator)
UREF_ATTR_UNSIGNED(ts_scte35, unique_program_id, "scte35.programid",
        unique program ID)
UREF_ATTR_SMALL_UNSIGNED_VA(ts_scte35, desc_tag, "scte35.desc_tag[%u]",
                            descriptor tag, unsigned desc, desc)
UREF_ATTR_UNSIGNED_VA(ts_scte35, desc_identifier, "scte35.desc_id[%u]",
                      descriptor identifier, unsigned desc, desc)

#ifdef __cplusplus
}
#endif
#endif
