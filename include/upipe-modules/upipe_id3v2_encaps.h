/*
 * Copyright (C) 2023 EasyTools
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

#ifndef _UPIPE_MODULES_UPIPE_ID3V2_ENCAPS_H_
# define _UPIPE_MODULES_UPIPE_ID3V2_ENCAPS_H_
# ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uprobe.h"

#define UPIPE_ID3V2E_SUB_SIGNATURE  UBASE_FOURCC('i','d','3','e')
#define UPIPE_ID3V2E_SIGNATURE      UBASE_FOURCC('i','d','3','E')

/** @This returns the id3v2 pipe manager. */
struct upipe_mgr *upipe_id3v2e_mgr_alloc(void);

# ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_MODULES_UPIPE_ID3V2_ENCAPS_H_ */
