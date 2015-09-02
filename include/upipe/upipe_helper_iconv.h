/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
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
 * @short Upipe helper functions handling iconv (required by biTStream)
 */

#ifndef _UPIPE_UPIPE_HELPER_ICONV_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_ICONV_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/upipe.h>

#include <iconv.h>

/** @This declares four functions handling iconv (required by biTStream).
 *
 * You must add two members to your private pipe structure, for instance:
 * @code
 *  const char *current_encoding;
 *  iconv_t iconv_handle;
 * @end code
 *
 * You must also declare @ref #UPIPE_HELPER_UPIPE prior to using this macro.
 *
 * Supposing the name of your structure is upipe_foo, it declares:
 * @list
 * @item @code
 *  void upipe_foo_init_iconv(struct upipe_foo *s)
 * @end code
 * Initializes the fields.
 *
 * @item @code
 *  char *upipe_foo_iconv_append_null(const char *string, size_t length)
 * @end code
 * Called internally in cases where no conversion is needed.
 *
 * @item @code
 *  char *upipe_foo_iconv_wrapper(void *s, const char *encoding, char *string,
 *                                size_t length)
 * @end code
 * Wraps around iconv in biTStream calls. The returned string must be freed
 * by the user.
 *
 * @item @code
 *  void upipe_foo_clean_iconv(struct upipe_foo *s)
 * @end code
 * Releases the iconv handle.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure 
 * @param NATIVE_ENCODING native encoding to convert to ("UTF-8")
 * @param CURRENT_ENCODING name of the @tt{const char *} field of
 * your private upipe structure
 * @param ICONV_HANDLE name of the @tt{iconv_t} field of
 * your private upipe structure
 */
#define UPIPE_HELPER_ICONV(STRUCTURE, NATIVE_ENCODING, CURRENT_ENCODING,    \
                           ICONV_HANDLE)                                    \
/** @internal @This initializes the private members for this helper.        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_init_iconv(struct upipe *upipe)                     \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    s->CURRENT_ENCODING = "";                                               \
    s->ICONV_HANDLE = (iconv_t)-1;                                          \
}                                                                           \
/** @internal @This wraps around iconv calls in cases where no conversion   \
 * is needed.                                                               \
 *                                                                          \
 * @param string string to convert (unterminated)                           \
 * @param length length of the string                                       \
 * @return allocated converted string (must be freed)                       \
 */                                                                         \
static char *STRUCTURE##_iconv_append_null(const char *string,              \
                                           size_t length)                   \
{                                                                           \
    char *output = malloc(length + 1);                                      \
    if (unlikely(output == NULL))                                           \
        return NULL;                                                        \
    memcpy(output, string, length);                                         \
    output[length] = '\0';                                                  \
    return output;                                                          \
}                                                                           \
/** @internal @This wraps around iconv in biTStream calls.                  \
 *                                                                          \
 * @param _upipe description structure of the pipe                          \
 * @param encoding name of encoding in iconv (must be persistent)           \
 * @param string string to convert (unterminated)                           \
 * @param length length of the string                                       \
 * @return allocated converted string (must be freed)                       \
 */                                                                         \
static char *STRUCTURE##_iconv_wrapper(void *_upipe, const char *encoding,  \
                                       char *string, size_t length)         \
{                                                                           \
    struct upipe *upipe = (struct upipe *)_upipe;                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    char *output, *p;                                                       \
    size_t out_length;                                                      \
                                                                            \
    if (!strcmp(encoding, NATIVE_ENCODING))                                 \
        return STRUCTURE##_iconv_append_null(string, length);               \
                                                                            \
    if (s->ICONV_HANDLE != (iconv_t)-1 &&                                   \
        strcmp(encoding, s->CURRENT_ENCODING)) {                            \
        iconv_close(s->ICONV_HANDLE);                                       \
        s->ICONV_HANDLE = (iconv_t)-1;                                      \
    }                                                                       \
                                                                            \
    if (s->ICONV_HANDLE == (iconv_t)-1)                                     \
        s->ICONV_HANDLE = iconv_open(NATIVE_ENCODING, encoding);            \
    if (s->ICONV_HANDLE == (iconv_t)-1) {                                   \
        upipe_warn_va(upipe, "couldn't convert from %s to %s (%m)",         \
                      encoding, NATIVE_ENCODING);                           \
        return STRUCTURE##_iconv_append_null(string, length);               \
    }                                                                       \
    s->CURRENT_ENCODING = encoding;                                         \
                                                                            \
    /* converted strings can be up to six times larger */                   \
    out_length = length * 6;                                                \
    p = output = malloc(out_length);                                        \
    if (unlikely(p == NULL)) {                                              \
        upipe_err(upipe, "couldn't allocate");                              \
        return STRUCTURE##_iconv_append_null(string, length);               \
    }                                                                       \
    if (iconv(s->ICONV_HANDLE, &string, &length, &p, &out_length) == -1) {  \
        upipe_warn_va(upipe, "couldn't convert from %s to %s (%m)",         \
                      encoding, NATIVE_ENCODING);                           \
        free(output);                                                       \
        return STRUCTURE##_iconv_append_null(string, length);               \
    }                                                                       \
    if (length)                                                             \
        upipe_warn_va(upipe, "partial conversion from %s to %s",            \
                      encoding, NATIVE_ENCODING);                           \
                                                                            \
    *p = '\0';                                                              \
    return output;                                                          \
}                                                                           \
/** @internal @This cleans up the private members for this helper.          \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_clean_iconv(struct upipe *upipe)                    \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    if (s->ICONV_HANDLE != (iconv_t)-1)                                     \
        iconv_close(s->ICONV_HANDLE);                                       \
}

#ifdef __cplusplus
}
#endif
#endif
