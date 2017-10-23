/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
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

#ifndef _UPIPE_DVBCSA_UPIPE_DVBCSA_ENCRYPT_H_
#define _UPIPE_DVBCSA_UPIPE_DVBCSA_ENCRYPT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>

/** @This is the dvbcsa encryption pipe signature */
#define UPIPE_DVBCSA_ENC_SIGNATURE  UBASE_FOURCC('d','v','b','e')

/** @This returns the dvbcsa encryption pipe management structure.
 *
 * @return a pointer to the management structure
 */
struct upipe_mgr *upipe_dvbcsa_enc_mgr_alloc(void);

#ifdef _cplusplus
}
#endif
#endif
