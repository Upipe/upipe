/*
 * Copyright (C) 2015-2017 OpenHeadend S.A.R.L.
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
 * @short Upipe helper functions writing DVB strings using iconv
 */

#ifndef _UPIPE_UPIPE_HELPER_DVB_STRING_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_DVB_STRING_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/upipe.h>

#include <iconv.h>
#include <ctype.h>

/** @This declares three functions writing DVB strings using iconv.
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
 *  void upipe_foo_init_dvb_string(struct upipe *upipe)
 * @end code
 * Initializes the fields.
 *
 * @item @code
 *  uint8_t *upipe_foo_alloc_dvb_string(struct upipe *upipe,
        const char *string, const char *encoding, size_t *out_length_p)
 * @end code
 * Allocates a buffer and stores a DVB string with the given encoding.
 *
 * @item @code
 *  void upipe_foo_clean_dvb_string(struct upipe *upipe)
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
#define UPIPE_HELPER_DVB_STRING(STRUCTURE, NATIVE_ENCODING,                 \
                                CURRENT_ENCODING, ICONV_HANDLE)             \
/** @internal @This initializes the private members for this helper.        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_init_dvb_string(struct upipe *upipe)                \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    s->CURRENT_ENCODING = "";                                               \
    s->ICONV_HANDLE = (iconv_t)-1;                                          \
}                                                                           \
/** @internal @This allocates a buffer and stores a DVB string with the     \
 * given encoding.                                                          \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param string input string in NATIVE_ENCODING                            \
 * @param encoding name of encoding in iconv (must be persistent)           \
 * @param filled in with the length of the allocated DVB string             \
 * @return an error code                                                    \
 */                                                                         \
static uint8_t *STRUCTURE##_alloc_dvb_string(struct upipe *upipe,           \
        const char *string, const char *encoding, size_t *out_length_p)     \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    size_t length = strlen(string);                                         \
    /* do not convert ASCII strings */                                      \
    const char *c = string;                                                 \
    while (*c)                                                              \
        if (!isascii(*c++))                                                 \
            break;                                                          \
    if (!*c)                                                                \
        return dvb_string_set((const uint8_t *)string, length, "ISO6937",   \
                              out_length_p);                                \
                                                                            \
    if (!strcmp(encoding, NATIVE_ENCODING))                                 \
        return dvb_string_set((const uint8_t *)string, length, encoding,    \
                              out_length_p);                                \
                                                                            \
    if (s->ICONV_HANDLE != (iconv_t)-1 &&                                   \
        strcmp(encoding, s->CURRENT_ENCODING)) {                            \
        iconv_close(s->ICONV_HANDLE);                                       \
        s->ICONV_HANDLE = (iconv_t)-1;                                      \
    }                                                                       \
                                                                            \
    if (s->ICONV_HANDLE == (iconv_t)-1)                                     \
        s->ICONV_HANDLE = iconv_open(encoding, NATIVE_ENCODING);            \
    if (s->ICONV_HANDLE == (iconv_t)-1) {                                   \
        upipe_warn_va(upipe, "couldn't convert from %s to %s (%m)",         \
                      NATIVE_ENCODING, encoding);                           \
        *out_length_p = 0;                                                  \
        return NULL;                                                        \
    }                                                                       \
    s->CURRENT_ENCODING = encoding;                                         \
                                                                            \
    /* converted strings can be up to six times larger */                   \
    size_t tmp_length = length * 6;                                         \
    char tmp[tmp_length];                                                   \
    size_t tmp_available = tmp_length;                                      \
    char *p = tmp;                                                          \
    if (iconv(s->ICONV_HANDLE, (char **)&string, &length,                   \
              &p, &tmp_available) == -1 || length) {                        \
        upipe_warn_va(upipe, "couldn't convert from %s to %s (%m)",         \
                      NATIVE_ENCODING, encoding);                           \
        *out_length_p = 0;                                                  \
        return NULL;                                                        \
    }                                                                       \
                                                                            \
    return dvb_string_set((const uint8_t *)tmp, tmp_length - tmp_available, \
                          encoding, out_length_p);                          \
}                                                                           \
/** @internal @This cleans up the private members for this helper.          \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_clean_dvb_string(struct upipe *upipe)               \
{                                                                           \
    struct STRUCTURE *s = STRUCTURE##_from_upipe(upipe);                    \
    if (s->ICONV_HANDLE != (iconv_t)-1)                                     \
        iconv_close(s->ICONV_HANDLE);                                       \
}

#ifdef __cplusplus
}
#endif
#endif
