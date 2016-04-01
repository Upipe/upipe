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
 * @short Upipe helper functions for public uprobe structure
 */

#ifndef _UPROBE_UPROBE_HELPER_UPROBE_H_
/** @hidden */
#define _UPROBE_UPROBE_HELPER_UPROBE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/uprobe.h>

/** @This declares two functions dealing with public and private parts
 * of the allocated probe structure.
 *
 * You must add the uprobe structure to your private probe structure:
 * @code
 *  struct uprobe uprobe;
 * @end code
 *
 * Supposing the name of your structure is uprobe_foo, it declares:
 * @list
 * @item @code
 *  struct uprobe *uprobe_foo_to_uprobe(struct uprobe_foo *s)
 * @end code
 * Returns a pointer to the public uprobe structure.
 *
 * @item @code
 *  struct uprobe_foo uprobe_foo_from_uprobe(struct uprobe *uprobe)
 * @end code
 * Returns a pointer to the private uprobe_foo structure.
 * @end list
 *
 * @param STRUCTURE name of your private uprobe structure 
 * @param UPROBE name of the @tt{struct uprobe} field of
 * your private uprobe structure
 */
#define UPROBE_HELPER_UPROBE(STRUCTURE, UPROBE)                             \
/** @internal @This returns the public uprobe structure.                    \
 *                                                                          \
 * @param STRUCTURE pointer to the private STRUCTURE structure              \
 * @return pointer to the public uprobe structure                           \
 */                                                                         \
static UBASE_UNUSED inline struct uprobe *                                  \
    STRUCTURE##_to_uprobe(struct STRUCTURE *s)                              \
{                                                                           \
    return s ? &s->UPROBE : NULL;                                           \
}                                                                           \
/** @internal @This returns the private STRUCTURE structure.                \
 *                                                                          \
 * @param uprobe public description structure of the probe                  \
 * @return pointer to the private STRUCTURE structure                       \
 */                                                                         \
static UBASE_UNUSED inline struct STRUCTURE *                               \
    STRUCTURE##_from_uprobe(struct uprobe *uprobe)                          \
{                                                                           \
    return uprobe ? container_of(uprobe, struct STRUCTURE, UPROBE) : NULL;  \
}

#ifdef __cplusplus
}
#endif
#endif
