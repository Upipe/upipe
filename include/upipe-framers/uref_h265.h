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
 * @short Upipe h265 attributes for uref
 */

#ifndef _UPIPE_FRAMERS_UREF_H265_H_
/** @hidden */
#define _UPIPE_FRAMERS_UREF_H265_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uref.h"
#include "upipe/uref_attr.h"

UREF_ATTR_SMALL_UNSIGNED(h265, type, "h265.type", slice coding type)

#ifdef __cplusplus
}
#endif
#endif
