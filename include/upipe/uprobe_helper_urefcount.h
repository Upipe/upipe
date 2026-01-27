/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _UPIPE_UPROBE_HELPER_UREFCOUNT_H_
# define _UPIPE_UPROBE_HELPER_UREFCOUNT_H_
#ifdef __cplusplus
extern "C" {
#endif

/** @file
 * @short uprobe helper functions to refcount user defined probes
 */

/** @This declares functions to increment and decrement reference count on
 * user defined probes.
 *
 * Supposing the name of your structure is @em uprobe_foo, it declares:
 * @list
 * @item @code C
 * static inline struct uprobe_foo *uprobe_foo_use(struct uprobe_foo *)
 * @end code
 * A wrapper to @ref uprobe_use which increments the reference count of the
 * probe.
 * @item @code C
 * static inline void uprobe_foo_release(struct uprobe_foo *)
 * @end code
 * A wrapper to @ref uprobe_release which decrements the reference count or
 * frees the probe.
 * @end list
 * You @b must define @ref #UPROBE_HELPER_UPROBE for @em uprobe_foo prior
 * to this.
 */
#define UPROBE_HELPER_UREFCOUNT(STRUCTURE)                                  \
/** @This increments reference count of a STRUCTURE.                        \
 *                                                                          \
 * @param s pointer to STRUCTURE                                            \
 * @return same pointer to STRUCTURE                                        \
 */                                                                         \
static inline struct STRUCTURE *STRUCTURE##_use(struct STRUCTURE *s)        \
{                                                                           \
    return STRUCTURE##_from_uprobe(uprobe_use(STRUCTURE##_to_uprobe(s)));   \
}                                                                           \
/** @This decrements the reference count of a STRUCTURE or frees it.        \
 *                                                                          \
 * @param s pointer to STRUCTURE                                            \
 */                                                                         \
static inline void STRUCTURE##_release(struct STRUCTURE *s)                 \
{                                                                           \
    uprobe_release(STRUCTURE##_to_uprobe(s));                               \
}

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_UPROBE_HELPER_UREFCOUNT_H_ */
