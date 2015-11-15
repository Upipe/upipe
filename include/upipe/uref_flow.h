/*
 * Copyright (C) 2012-2015 OpenHeadend S.A.R.L.
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
 * @short Upipe flow attributes for uref and control messages
 */

#ifndef _UPIPE_UREF_FLOW_H_
/** @hidden */
#define _UPIPE_UREF_FLOW_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uref.h>
#include <upipe/uref_attr.h>

#include <stdint.h>
#include <stdbool.h>

UREF_ATTR_VOID_UREF(flow, end, UREF_FLAG_FLOW_END,
        end flag meaning that the writer was disconnected)
UREF_ATTR_VOID_UREF(flow, discontinuity, UREF_FLAG_FLOW_DISC,
        discontinuity flag that may be present in any uref carrying data)
UREF_ATTR_VOID_SH(flow, random, UDICT_TYPE_FLOW_RANDOM,
        random access flag that may be present in any uref carrying data)
UREF_ATTR_VOID_SH(flow, error, UDICT_TYPE_FLOW_ERROR,
        error flag that may be present in any uref carrying data)
UREF_ATTR_STRING_SH(flow, def, UDICT_TYPE_FLOW_DEF, flow definition)
UREF_ATTR_UNSIGNED_SH(flow, id, UDICT_TYPE_FLOW_ID,
        flow ID from the last split pipe)
UREF_ATTR_STRING_SH(flow, raw_def, UDICT_TYPE_FLOW_RAWDEF, raw flow definition)
UREF_ATTR_SMALL_UNSIGNED_SH(flow, languages, UDICT_TYPE_FLOW_LANGUAGES,
        number of flow languages)
UREF_ATTR_STRING_VA(flow, language, "f.lang[%"PRIu8"]", flow language,
        uint8_t nb, nb)
UREF_ATTR_VOID_VA(flow, hearing_impaired, "f.himp[%"PRIu8"]",
        flow for hearing impaired, uint8_t nb, nb)
UREF_ATTR_VOID_VA(flow, visual_impaired, "f.vimp[%"PRIu8"]",
        flow for visual impaired, uint8_t nb, nb)
UREF_ATTR_VOID_VA(flow, audio_clean, "f.clean[%"PRIu8"]",
        clean effects audio, uint8_t nb, nb)
UREF_ATTR_VOID(flow, lowdelay, "f.lowdelay", low delay mode)
UREF_ATTR_VOID(flow, copyright, "f.copyright", copyrighted content)
UREF_ATTR_VOID(flow, original, "f.original", original or copy)
UREF_ATTR_VOID(flow, global, "f.global", global headers present or required)
UREF_ATTR_OPAQUE(flow, headers, "f.headers", global headers)
UREF_ATTR_STRING(flow, name, "f.name", flow name)

/** @This sets the flow definition attribute of a uref, with printf-style
 * generation.
 *
 * @param uref uref structure
 * @param format printf-style format of the flow definition, followed by a
 * variable list of arguments
 * @return an error code
 */
static inline int uref_flow_set_def_va(struct uref *uref,
                                       const char *format, ...)
{
    UBASE_VARARG(uref_flow_set_def(uref, string))
}

#ifdef __cplusplus
}
#endif
#endif
