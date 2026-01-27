/*
 * Copyright (C) 2018 OpenHeadend S.A.R.L.
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
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
    int (*throw_cb)(struct STRUCT *, struct upipe *, int, va_list) = THROW; \
    if (throw_cb == NULL)                                                   \
        return uprobe_throw_next(uprobe, upipe, event, args);               \
    return throw_cb(STRUCT##_from_##UPROBE(uprobe), upipe, event, args);    \
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
