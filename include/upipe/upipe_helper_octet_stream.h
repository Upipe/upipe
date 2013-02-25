/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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
 * @short Upipe helper functions to work on input as an octet stream
 */

#ifndef _UPIPE_UPIPE_HELPER_OCTET_STREAM_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_OCTET_STREAM_H_

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/ubuf.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/upipe.h>

#include <assert.h>

/** @This declares four functions allowing to process input urefs as an
 * octet stream.
 *
 * You must add three members to your private pipe structure, for instance:
 * @code
 *  struct uref *next_uref;
 *  size_t next_uref_size;
 *  struct ulist urefs;
 * @end code
 *
 * You must also declare @ref #UPIPE_HELPER_UPIPE prior to using this macro.
 *
 * Supposing the name of your structure is upipe_foo, it declares:
 * @list
 * @item @code
 *  void upipe_foo_init_octet_stream(struct upipe_foo *s)
 * @end code
 * Initializes the fields.
 *
 * @item @code
 *  void upipe_foo_append_octet_stream(struct upipe_foo *s, struct uref *uref)
 * @end code
 * Appends the given uref to the octet stream.
 *
 * @item @code
 *  void upipe_foo_consume_octet_stream(struct upipe_foo *s, size_t consumed)
 * @end code
 * Consumes the given number of octets from the octet stream.
 *
 * @item @code
 *  void upipe_foo_clean_octet_stream(struct upipe_foo *s)
 * @end code
 * Releases all buffers.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure 
 * @param NEXT_UREF name of the @tt{struct uref} field of
 * your private upipe structure
 * @param NEXT_UREF_SIZE name of the @tt{size_t} field of
 * your private upipe structure
 * @param UREFS name of the @tt{struct ulist} field of
 * your private upipe structure
 * @param APPEND_CB function @tt{(struct upipe *)} that will be called when
 * a new uref starts being consumed (may be NULL)
 */
#define UPIPE_HELPER_OCTET_STREAM(STRUCTURE, NEXT_UREF, NEXT_UREF_SIZE,     \
                                  UREFS, APPEND_CB)                         \
/** @internal @This initializes the private members for this helper.        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_init_octet_stream(struct upipe *upipe)              \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    STRUCTURE->NEXT_UREF = NULL;                                            \
    ulist_init(&STRUCTURE->UREFS);                                          \
}                                                                           \
/** @internal @This appends a new uref to the list of received urefs, and   \
 * also appends it to the uref of the octet stream.                         \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param uref uref structure to append                                     \
 */                                                                         \
static void STRUCTURE##_append_octet_stream(struct upipe *upipe,            \
                                            struct uref *uref)              \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    if (STRUCTURE->NEXT_UREF == NULL) {                                     \
        STRUCTURE->NEXT_UREF = uref;                                        \
        uref_block_size(STRUCTURE->NEXT_UREF, &STRUCTURE->NEXT_UREF_SIZE);  \
        void (*cb)(struct upipe *) = APPEND_CB;                             \
        if (cb != NULL)                                                     \
            cb(upipe);                                                      \
        return;                                                             \
    }                                                                       \
    struct ubuf *ubuf = ubuf_dup(uref->ubuf);                               \
    if (unlikely(ubuf == NULL ||                                            \
                 !uref_block_append(STRUCTURE->NEXT_UREF, ubuf))) {         \
        upipe_throw_aerror(upipe);                                          \
        uref_free(uref);                                                    \
        if (ubuf != NULL)                                                   \
            ubuf_free(ubuf);                                                \
    } else                                                                  \
        ulist_add(&STRUCTURE->UREFS, uref_to_uchain(uref));                 \
}                                                                           \
/** @internal @This consumes the given number of octets from the octet      \
 * stream, and rotates the buffers accordingly.                             \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param consumed number of octets consumed from the octet stream          \
 */                                                                         \
static void STRUCTURE##_consume_octet_stream(struct upipe *upipe,           \
                                             size_t consumed)               \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    assert(STRUCTURE->NEXT_UREF != NULL);                                   \
    if (consumed < STRUCTURE->NEXT_UREF_SIZE) {                             \
        uref_block_resize(STRUCTURE->NEXT_UREF, consumed, -1);              \
        STRUCTURE->NEXT_UREF_SIZE -= consumed;                              \
        return;                                                             \
    }                                                                       \
    while (consumed) {                                                      \
        assert(STRUCTURE->NEXT_UREF != NULL);                               \
        if (consumed < STRUCTURE->NEXT_UREF_SIZE) {                         \
            uref_block_resize(STRUCTURE->NEXT_UREF, consumed, -1);          \
            STRUCTURE->NEXT_UREF_SIZE -= consumed;                          \
            break;                                                          \
        }                                                                   \
        consumed -= STRUCTURE->NEXT_UREF_SIZE;                              \
        uref_free(STRUCTURE->NEXT_UREF);                                    \
        STRUCTURE->NEXT_UREF = NULL;                                        \
        struct uchain *uchain = ulist_pop(&STRUCTURE->UREFS);               \
        if (uchain != NULL)                                                 \
            STRUCTURE##_append_octet_stream(upipe,                          \
                                            uref_from_uchain(uchain));      \
    }                                                                       \
    struct ulist urefs = STRUCTURE->UREFS;                                  \
    ulist_init(&STRUCTURE->UREFS);                                          \
    struct uchain *uchain;                                                  \
    while ((uchain = ulist_pop(&urefs)) != NULL)                            \
        STRUCTURE##_append_octet_stream(upipe,                              \
                                        uref_from_uchain(uchain));          \
}                                                                           \
/** @internal @This extracts the given number of octets from the octet      \
 * stream, and rotates the buffers accordingly.                             \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param extracted number of octets to extract from the octet stream       \
 * @return uref containing extracted octets                                 \
 */                                                                         \
static struct uref *STRUCTURE##_extract_octet_stream(struct upipe *upipe,   \
                                                     size_t extracted)      \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    assert(STRUCTURE->NEXT_UREF != NULL);                                   \
    if (extracted < STRUCTURE->NEXT_UREF_SIZE) {                            \
        struct uref *uref = uref_block_splice(STRUCTURE->NEXT_UREF, 0,      \
                                              extracted);                   \
        uref_block_resize(STRUCTURE->NEXT_UREF, extracted, -1);             \
        STRUCTURE->NEXT_UREF_SIZE -= extracted;                             \
        return uref;                                                        \
    }                                                                       \
    struct uref *uref = STRUCTURE->NEXT_UREF;                               \
    uref_block_resize(uref, 0, extracted);                                  \
    STRUCTURE->NEXT_UREF = NULL;                                            \
    extracted -= STRUCTURE->NEXT_UREF_SIZE;                                 \
    while (extracted) {                                                     \
        struct uchain *uchain = ulist_pop(&STRUCTURE->UREFS);               \
        if (uchain != NULL)                                                 \
            STRUCTURE##_append_octet_stream(upipe,                          \
                                            uref_from_uchain(uchain));      \
        if (extracted < STRUCTURE->NEXT_UREF_SIZE) {                        \
            uref_block_resize(STRUCTURE->NEXT_UREF, extracted, -1);         \
            STRUCTURE->NEXT_UREF_SIZE -= extracted;                         \
            break;                                                          \
        }                                                                   \
        extracted -= STRUCTURE->NEXT_UREF_SIZE;                             \
        uref_free(STRUCTURE->NEXT_UREF);                                    \
        STRUCTURE->NEXT_UREF = NULL;                                        \
    }                                                                       \
    struct ulist urefs = STRUCTURE->UREFS;                                  \
    ulist_init(&STRUCTURE->UREFS);                                          \
    struct uchain *uchain;                                                  \
    while ((uchain = ulist_pop(&urefs)) != NULL)                            \
        STRUCTURE##_append_octet_stream(upipe,                              \
                                        uref_from_uchain(uchain));          \
    return uref;                                                            \
}                                                                           \
/** @internal @This cleans up the private members for this helper.          \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_clean_octet_stream(struct upipe *upipe)             \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    if (STRUCTURE->NEXT_UREF != NULL) {                                     \
        uref_free(STRUCTURE->NEXT_UREF);                                    \
        struct uchain *uchain;                                              \
        while ((uchain = ulist_pop(&STRUCTURE->UREFS)) != NULL)             \
            uref_free(uref_from_uchain(uchain));                            \
    }                                                                       \
}

#endif
