/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _UPIPE_UPIPE_HELPER_UREFCOUNT_REAL_H_
# define _UPIPE_UPIPE_HELPER_UREFCOUNT_REAL_H_
#ifdef __cplusplus
extern "C" {
#endif

/** @This declares functions dealing with public and private parts
 * of the allocated pipe structure.
 *
 * You must add the urefcount structure to your private pipe structure:
 * @code
 *  struct urefcount urefcount_real;
 * @end code
 *
 * You must also declare @ref #UPIPE_HELPER_UPIPE prior to using this macro,
 * and have a function to free the structure when the refcount goes down to 0:
 * @code
 *  void upipe_foo_free(struct upipe *upipe)
 * @end code
 *
 * Supposing the name of your structure is upipe_foo and the name of the
 * urefcount is urefcount_real, it declares:
 * @list
 * @item @code
 *  void upipe_foo_dead_urefcount_real(struct urefcount *urefcount)
 * @end code
 * Internal wrapper.
 *
 * @item @code
 *  void upipe_foo_release_urefcount_real(struct urefcount *urefcount)
 * @end code
 * Typically called in the main urefcount callback.
 *
 * @item @code
 *  void upipe_foo_init_urefcount_real(struct upipe *upipe)
 * @end code
 * Typically called in your upipe_foo_alloc() function.
 *
 * @item @code
 *  void upipe_foo_clean_urefcount_real(struct upipe *upipe)
 * @end code
 * Typically called from your upipe_foo_free() function.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure
 * @param UREFCOUNT name of the @tt{struct urefcount} field of
 * your private upipe structure
 * @param DEAD name of the function to free the structure
 */
#define UPIPE_HELPER_UREFCOUNT_REAL(STRUCTURE, UREFCOUNT, DEAD)             \
UBASE_FROM_TO(STRUCTURE, urefcount, UREFCOUNT, UREFCOUNT)                   \
/** @hidden */                                                              \
static void DEAD(struct upipe *upipe);                                      \
/** @internal @This is called when the refcount goes down to zero.          \
 *                                                                          \
 * @param urefcount pointer to the urefcount structure                      \
 */                                                                         \
static void STRUCTURE##_dead_##UREFCOUNT(struct urefcount *urefcount)       \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_##UREFCOUNT(urefcount);          \
    DEAD(STRUCTURE##_to_upipe(s));                                          \
}                                                                           \
/** @internal @This uses the refcount.                                      \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static UBASE_UNUSED inline struct upipe *                                   \
STRUCTURE##_use_##UREFCOUNT(struct upipe *upipe)                            \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    s = STRUCTURE##_from_##UREFCOUNT(urefcount_use(&s->UREFCOUNT));         \
    return STRUCTURE##_to_upipe(s);                                         \
}                                                                           \
/** @internal @This releases the refcount.                                  \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_release_##UREFCOUNT(struct upipe *upipe)            \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    urefcount_release(&s->UREFCOUNT);                                       \
}                                                                           \
/** @internal @This initializes the private members for this helper.        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_init_##UREFCOUNT(struct upipe *upipe)               \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    urefcount_init(&s->UREFCOUNT, STRUCTURE##_dead_##UREFCOUNT);            \
}                                                                           \
/** @internal @This cleans up the private members for this helper.          \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_clean_##UREFCOUNT(struct upipe *upipe)              \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    urefcount_clean(&s->UREFCOUNT);                                         \
}


#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_UPIPE_HELPER_UREFCOUNT_REAL_H_ */
