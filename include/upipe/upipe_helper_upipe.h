/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
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
 * @short Upipe helper functions for public upipe structure
 */

#ifndef _UPIPE_UPIPE_HELPER_UPIPE_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_UPIPE_H_

#include <upipe/ubase.h>
#include <upipe/upipe.h>

/** @This declares two functions dealing with public and private parts
 * of the allocated pipe structure.
 *
 * You must add the upipe structure to your private pipe structure:
 * @code
 *  struct upipe upipe;
 * @end code
 *
 * Supposing the name of your structure is upipe_foo, it declares:
 * @list
 * @item @code
 *  struct upipe *upipe_foo_to_upipe(struct upipe_foo *s)
 * @end code
 * Returns a pointer to the public upipe structure.
 *
 * @item @code
 *  struct upipe_foo upipe_foo_from_upipe(struct upipe *upipe)
 * @end code
 * Returns a pointer to the private upipe_foo structure.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure 
 * @param UPIPE name of the @tt{struct upipe} field of
 * your private upipe structure
 */
#define UPIPE_HELPER_UPIPE(STRUCTURE, UPIPE)                                \
/** @internal @This returns the public upipe structure.                     \
 *                                                                          \
 * @param STRUCTURE pointer to the private STRUCTURE structure              \
 * @return pointer to the public upipe structure                            \
 */                                                                         \
static inline struct upipe *STRUCTURE##_to_upipe(struct STRUCTURE *s)       \
{                                                                           \
    return &s->UPIPE;                                                       \
}                                                                           \
/** @internal @This returns the private STRUCTURE structure.                \
 *                                                                          \
 * @param upipe public description structure of the pipe                    \
 * @return pointer to the private STRUCTURE structure                       \
 */                                                                         \
static inline struct STRUCTURE *STRUCTURE##_from_upipe(struct upipe *upipe) \
{                                                                           \
    return container_of(upipe, struct STRUCTURE, UPIPE);                    \
}

#endif
