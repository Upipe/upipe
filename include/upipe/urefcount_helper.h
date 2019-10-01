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

#ifndef _UPIPE_UREFCOUNT_HELPER_H_
/** @hidden */
#define _UPIPE_UREFCOUNT_HELPER_H_
#ifdef __cplusplus
extern "C" {
#endif

/** @This defines functions to deal with refcounted structures. */
#define UREFCOUNT_HELPER(STRUCT, UREFCOUNT, DEAD)                       \
UBASE_FROM_TO(STRUCT, urefcount, UREFCOUNT, UREFCOUNT)                  \
                                                                        \
static void DEAD(struct STRUCT *obj);                                   \
                                                                        \
static void STRUCT##_dead_##UREFCOUNT(struct urefcount *urefcount)      \
{                                                                       \
    struct STRUCT *obj = STRUCT##_from_##UREFCOUNT(urefcount);          \
    DEAD(obj);                                                          \
}                                                                       \
                                                                        \
static void STRUCT##_init_##UREFCOUNT(struct STRUCT *obj)               \
{                                                                       \
    urefcount_init(STRUCT##_to_##UREFCOUNT(obj),                        \
                   STRUCT##_dead_##UREFCOUNT);                          \
}                                                                       \
                                                                        \
static void STRUCT##_clean_##UREFCOUNT(struct STRUCT *obj)              \
{                                                                       \
    urefcount_clean(STRUCT##_to_##UREFCOUNT(obj));                      \
}                                                                       \
                                                                        \
static UBASE_UNUSED inline struct STRUCT *                              \
STRUCT##_use_##UREFCOUNT(struct STRUCT *obj)                            \
{                                                                       \
    return STRUCT##_from_##UREFCOUNT(                                   \
        urefcount_use(STRUCT##_to_##UREFCOUNT(obj)));                   \
}                                                                       \
                                                                        \
static UBASE_UNUSED inline void                                         \
STRUCT##_release_##UREFCOUNT(struct STRUCT *obj)                        \
{                                                                       \
    urefcount_release(STRUCT##_to_##UREFCOUNT(obj));                    \
}                                                                       \
                                                                        \
static UBASE_UNUSED inline bool                                         \
STRUCT##_single_##UREFCOUNT(struct STRUCT *obj)                         \
{                                                                       \
    return urefcount_single(STRUCT##_to_##UREFCOUNT(obj));              \
}

#ifdef __cplusplus
}
#endif
#endif
