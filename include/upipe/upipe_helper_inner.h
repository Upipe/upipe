/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
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
 * @short Upipe helper functions for inner pipes
 */

#ifndef _UPIPE_UPIPE_HELPER_INNER_H_
# define _UPIPE_UPIPE_HELPER_INNER_H_
#ifdef __cplusplus
extern "C" {
#endif

/** @This declares functions dealing with inner pipes,
 * which internally implement an inner pipeline to handle a given task.
 *
 * You must add four members to your private upipe structure, for instance:
 * @code
 *  struct upipe *inner;
 * @end code
 *
 * You must also declare @ref #UPIPE_HELPER_UPIPE prior to using this macro.
 *
 * Supposing the name of your structure is upipe_foo and the name of your
 * member is inner, it declares:
 * @list
 * @item @code
 *  void upipe_foo_init_inner(struct upipe *upipe)
 * @end code
 * Typically called in your upipe_foo_alloc() function.
 *
 * @item @code
 *  void upipe_foo_store_inner(struct upipe *upipe, struct upipe *inner)
 * @end code
 * Called whenever you change the inner pipe of this pipe.
 *
 * @item @code
 *  int upipe_foo_control_inner(struct upipe *upipe,
 *                              enum upipe_command command, va_list args)
 * @end code
 * Typically called from your upipe_foo_control() handler.
 * It acts as a proxy for commands.
 *
 * @item @code
 *  void upipe_foo_clean_inner(struct upipe *upipe)
 * @end code
 * Typically called from your upipe_foo_free() function.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure
 * @param INNER name of the @tt{struct upipe *} field of
 * your private upipe structure, pointing to the inner pipe
 */
#define UPIPE_HELPER_INNER(STRUCTURE, INNER)                            \
/** @internal @This initializes the private members for this helper.    \
 *                                                                      \
 * @param upipe description structure of the pipe                       \
 */                                                                     \
static void STRUCTURE##_init_##INNER(struct upipe *upipe)               \
{                                                                       \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                \
    s->INNER = NULL;                                                    \
}                                                                       \
/** @internal @This stores the inner pipe, while releasing the          \
 * previous one.                                                        \
 *                                                                      \
 * @param upipe description structure of the pipe                       \
 * @param inner inner pipe (belongs to the callee)                      \
 */                                                                     \
static UBASE_UNUSED void STRUCTURE##_store_##INNER(struct upipe *upipe, \
                                                   struct upipe *inner) \
{                                                                       \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                \
    upipe_release(s->INNER);                                            \
    s->INNER = inner;                                                   \
}                                                                       \
/** @internal @This cleans up the private members for this helper.      \
 *                                                                      \
 * @param upipe description structure of the pipe                       \
 */                                                                     \
static void STRUCTURE##_clean_##INNER(struct upipe *upipe)              \
{                                                                       \
    STRUCTURE##_store_##INNER(upipe, NULL);                             \
}                                                                       \
/** @internal @This handles the control commands.                       \
 *                                                                      \
 * @param upipe description structure of the pipe                       \
 * @param command control command                                       \
 * @param args optional control command arguments                       \
 * @return an error code                                                \
 */                                                                     \
static UBASE_UNUSED int STRUCTURE##_control_##INNER(struct upipe *upipe,\
                                                    int command,        \
                                                    va_list args)       \
{                                                                       \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                \
    if (s->INNER == NULL)                                               \
        return UBASE_ERR_UNHANDLED;                                     \
    va_list args_copy;                                                  \
    va_copy(args_copy, args);                                           \
    int ret = upipe_control_va(s->INNER, command, args_copy);           \
    va_end(args_copy);                                                  \
    return ret;                                                         \
}

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_UPIPE_HELPER_INNER_H_ */
