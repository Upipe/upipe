/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
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
