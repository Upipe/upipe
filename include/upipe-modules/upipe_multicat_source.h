/*
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
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
 * @short Upipe module - multicat file source
 */

#ifndef _UPIPE_MODULES_UPIPE_MULTICAT_SOURCE_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_MULTICAT_SOURCE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <upipe/ubase.h>
#include <upipe/upipe.h>
#include <upipe/uref_attr.h>

UREF_ATTR_STRING(msrc_flow, path, "msrc.path", directory path)
UREF_ATTR_STRING(msrc_flow, data, "msrc.data", data suffix)
UREF_ATTR_STRING(msrc_flow, aux, "msrc.aux", aux suffix)
UREF_ATTR_UNSIGNED(msrc_flow, rotate, "msrc.rotate", rotate interval)
UREF_ATTR_UNSIGNED(msrc_flow, offset, "msrc.offset", rotate offset)

#define UPIPE_MSRC_SIGNATURE UBASE_FOURCC('m','s','r','c')
#define UPIPE_MSRC_DEF_ROTATE UINT64_C(97200000000)
#define UPIPE_MSRC_DEF_OFFSET UINT64_C(0)

/** @This returns the management structure for msrc pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_msrc_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
