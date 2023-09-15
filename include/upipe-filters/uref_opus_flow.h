/*
 * Copyright (C) 2023 EasyTools S.A.S.
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
 * @short Upipe opus flow definition attributes for uref
 */

#ifndef _UPIPE_FILTERS_UREF_OPUS_FLOW_H_
/** @hidden */
#define _UPIPE_FILTERS_UREF_OPUS_FLOW_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uref.h"
#include "upipe/uref_attr.h"
#include "upipe/uref_flow.h"

/** @This defines encapsulation types for Opus. */
enum uref_opus_encaps {
    /** No encapsulation */
    UREF_OPUS_ENCAPS_RAW = 0,
    /** TS encapsulation */
    UREF_OPUS_ENCAPS_TS,
};

UREF_ATTR_SMALL_UNSIGNED(opus_flow, encaps, "opus.encaps",
                         Opus encapsulation type)

#ifdef __cplusplus
}
#endif
#endif
