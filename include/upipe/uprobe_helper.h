/*
 * Copyright (C) 2018 OpenHeadend S.A.R.L.
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

#ifndef _UPIPE_UPROBE_HELPER_H_
/** @hidden */
#define _UPIPE_UPROBE_HELPER_H_
#ifdef __cplusplus
extern "C" {
#endif

#define UPROBE_HELPER(STRUCT, UPROBE, THROW, UREFCOUNT)                     \
UBASE_FROM_TO(STRUCT, uprobe, UPROBE, UPROBE);                              \
                                                                            \
static int STRUCT##_catch_##UPROBE(struct uprobe *uprobe,                   \
                                   struct upipe *upipe,                     \
                                   int event, va_list args)                 \
{                                                                           \
    return THROW(STRUCT##_from_##UPROBE(uprobe), upipe, event, args);       \
}                                                                           \
                                                                            \
static void STRUCT##_init_##UPROBE(struct STRUCT *obj, struct uprobe *next) \
{                                                                           \
    struct uprobe *uprobe = STRUCT##_to_##UPROBE(obj);                      \
    uprobe_init(uprobe, STRUCT##_catch_##UPROBE, next);                     \
    uprobe->refcount = &obj->UREFCOUNT;                                     \
}                                                                           \
                                                                            \
static void STRUCT##_clean_##UPROBE(struct STRUCT *obj)                     \
{                                                                           \
    uprobe_clean(STRUCT##_to_##UPROBE(obj));                                \
}                                                                           \
                                                                            \
static inline int STRUCT##_throw_next_##UPROBE(struct STRUCT *obj,          \
                                               struct upipe *upipe,         \
                                               int event, va_list args)     \
{                                                                           \
    return uprobe_throw_next(STRUCT##_to_##UPROBE(obj), upipe,              \
                             event, args);                                  \
}

#ifdef __cplusplus
}
#endif
#endif
