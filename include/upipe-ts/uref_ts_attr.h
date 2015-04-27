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
 * @short Upipe attributes macros for TS
 */

#ifndef _UPIPE_TS_UREF_TS_ATTR_H_
/** @hidden */
#define _UPIPE_TS_UREF_TS_ATTR_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uref.h>
#include <upipe/uref_attr.h>

#include <string.h>
#include <stdint.h>

/** @This declares a set of functions allowing to read or write descriptors
 * in a uref structure.
 *
 * @param group group of attributes
 * @param name name of the descriptors set
 */
#define UREF_TS_ATTR_DESCRIPTOR(group, name)                                \
/** @This registers a new name in the TS flow definition packet.            \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param desc descriptor                                                   \
 * @param desc_len size of name                                             \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_add_##name(struct uref *uref,              \
        const uint8_t *desc, size_t desc_len)                               \
{                                                                           \
    uint64_t descriptors = 0;                                               \
    uref_##group##_get_##name##s(uref, &descriptors);                       \
    UBASE_RETURN(uref_##group##_set_##name##s(uref, descriptors + 1))       \
    UBASE_RETURN(uref_##group##_set_##name(uref, desc, desc_len,            \
                                              descriptors))                 \
    return UBASE_ERR_NONE;                                                  \
}                                                                           \
/** @This gets the total size of name##s.                                   \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return the size of name##s                                              \
 */                                                                         \
static inline size_t uref_##group##_size_##name##s(struct uref *uref)       \
{                                                                           \
    uint64_t descriptors = 0;                                               \
    uref_##group##_get_##name##s(uref, &descriptors);                       \
    size_t descs_len = 0;                                                   \
    for (uint64_t j = 0; j < descriptors; j++) {                            \
        const uint8_t *desc;                                                \
        size_t desc_len;                                                    \
        if (ubase_check(uref_##group##_get_##name(uref, &desc, &desc_len,   \
                                                     j)))                   \
            descs_len += desc_len;                                          \
    }                                                                       \
    return descs_len;                                                       \
}                                                                           \
/** @This extracts all name##s.                                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param descs_p filled in with the name##s (size to be calculated with    \
 * @ref uref_##group##_size_##name##s)                                      \
 */                                                                         \
static inline void uref_##group##_extract_##name##s(struct uref *uref,      \
                                                    uint8_t *descs_p)       \
{                                                                           \
    uint64_t descriptors = 0;                                               \
    uref_##group##_get_##name##s(uref, &descriptors);                       \
    for (uint64_t j = 0; j < descriptors; j++) {                            \
        const uint8_t *desc;                                                \
        size_t desc_len;                                                    \
        if (ubase_check(uref_##group##_get_##name(uref, &desc, &desc_len,   \
                                                j))) {                      \
            memcpy(descs_p, desc, desc_len);                                \
            descs_p += desc_len;                                            \
        }                                                                   \
    }                                                                       \
}                                                                           \
/** @This compares all name##s in two urefs.                                \
 *                                                                          \
 * @param uref1 pointer to the first uref                                   \
 * @param uref2 pointer to the second uref                                  \
 * @return 0 if both are absent or identical                                \
 */                                                                         \
static inline int uref_##group##_compare_##name##s(struct uref *uref1,      \
                                                   struct uref *uref2)      \
{                                                                           \
    size_t size1 = uref_##group##_size_##name##s(uref1);                    \
    size_t size2 = uref_##group##_size_##name##s(uref2);                    \
    if (size1 != size2)                                                     \
        return size2 - size1;                                               \
    uint8_t descriptors1[size1];                                            \
    uref_##group##_extract_##name##s(uref1, descriptors1);                  \
    uint8_t descriptors2[size1];                                            \
    uref_##group##_extract_##name##s(uref2, descriptors1);                  \
    return memcmp(descriptors1, descriptors2, size1);                       \
}

/** @This declares a set of functions allowing to read or write descriptors
 * of a substructure in a uref structure.
 *
 * @param group group of the attributes
 * @param name name of the descriptors set
 * @param attr printf-style format of the attribute
 */
#define UREF_TS_ATTR_SUBDESCRIPTOR(group, name, attr)                       \
/** @This returns the sub name attribute of a uref.                         \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param p pointer to the retrieved value (modified during execution)      \
 * @param sub number of the substructure                                    \
 * @param nb number of the name                                             \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_get_##name(struct uref *uref,              \
        const uint8_t **p, size_t *size_p, uint64_t sub, uint64_t nb)       \
{                                                                           \
    struct udict_opaque opaque;                                             \
    int err = uref_attr_get_opaque_va(uref, &opaque, UDICT_TYPE_OPAQUE,     \
                                      attr, sub, nb);                       \
    if (ubase_check(err)) {                                                 \
        *p = opaque.v;                                                      \
        *size_p = opaque.size;                                              \
    }                                                                       \
    return err;                                                             \
}                                                                           \
/** @This sets the sub name attribute of a uref.                            \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @param sub number of the sub                                             \
 * @param nb number of the name                                             \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_set_##name(struct uref *uref,              \
        const uint8_t *v, size_t size, uint64_t sub, uint64_t nb)           \
{                                                                           \
    struct udict_opaque opaque;                                             \
    opaque.v = v;                                                           \
    opaque.size = size;                                                     \
    return uref_attr_set_opaque_va(uref, opaque, UDICT_TYPE_OPAQUE,         \
                                   attr, sub, nb);                          \
}                                                                           \
/** @This deletes the sub name attribute of a uref.                         \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param sub number of the sub                                             \
 * @param nb number of the name                                             \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_delete_##name(struct uref *uref,           \
        uint64_t sub, uint64_t nb)                                          \
{                                                                           \
    return uref_attr_delete_va(uref, UDICT_TYPE_OPAQUE, attr, sub, nb);     \
}                                                                           \
/** @This registers a new sub name in the TS flow definition packet.        \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param desc name                                                         \
 * @param desc_len size of name                                             \
 * @param sub number of the sub                                             \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_add_##name(struct uref *uref,              \
        const uint8_t *desc, size_t desc_len, uint64_t sub)                 \
{                                                                           \
    uint64_t descriptors = 0;                                               \
    uref_##group##_get_##name##s(uref, &descriptors, sub);                  \
    UBASE_RETURN(uref_##group##_set_##name##s(uref, descriptors + 1,        \
                sub))                                                       \
    UBASE_RETURN(uref_##group##_set_##name(uref, desc, desc_len,            \
                sub, descriptors))                                          \
    return UBASE_ERR_NONE;                                                  \
}                                                                           \
/** @This gets the total size of name##s.                                   \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param sub number of the sub                                             \
 * @return the size of name##s                                              \
 */                                                                         \
static inline size_t uref_##group##_size_##name##s(struct uref *uref,       \
                                                   uint64_t sub)            \
{                                                                           \
    uint64_t descriptors = 0;                                               \
    uref_##group##_get_##name##s(uref, &descriptors, sub);                  \
    size_t descs_len = 0;                                                   \
    for (uint64_t j = 0; j < descriptors; j++) {                            \
        const uint8_t *desc;                                                \
        size_t desc_len;                                                    \
        if (ubase_check(uref_##group##_get_##name(uref, &desc, &desc_len,   \
                        sub, j)))                                           \
            descs_len += desc_len;                                          \
    }                                                                       \
    return descs_len;                                                       \
}                                                                           \
/** @This extracts all name##s.                                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param descs_p filled in with the name##s (size to be calculated with    \
 * @ref uref_##group##_size_##name##s)                                      \
 * @param sub number of the sub                                             \
 */                                                                         \
static inline void uref_##group##_extract_##name##s(struct uref *uref,      \
        uint8_t *descs_p, uint64_t sub)                                     \
{                                                                           \
    uint64_t descriptors = 0;                                               \
    uref_##group##_get_##name##s(uref, &descriptors, sub);                  \
    for (uint64_t j = 0; j < descriptors; j++) {                            \
        const uint8_t *desc;                                                \
        size_t desc_len;                                                    \
        if (ubase_check(uref_##group##_get_##name(uref, &desc, &desc_len,   \
                        sub, j))) {                                         \
            memcpy(descs_p, desc, desc_len);                                \
            descs_p += desc_len;                                            \
        }                                                                   \
    }                                                                       \
}                                                                           \
/** @This compares all name##s in two urefs.                                \
 *                                                                          \
 * @param uref1 pointer to the first uref                                   \
 * @param uref2 pointer to the second uref                                  \
 * @param sub number of the sub                                             \
 * @return 0 if both are absent or identical                                \
 */                                                                         \
static inline int uref_##group##_compare_##name##s(struct uref *uref1,      \
                                                   struct uref *uref2,      \
                                                   uint64_t sub)            \
{                                                                           \
    size_t size1 = uref_##group##_size_##name##s(uref1, sub);               \
    size_t size2 = uref_##group##_size_##name##s(uref2, sub);               \
    if (size1 != size2)                                                     \
        return size2 - size1;                                               \
    uint8_t descriptors1[size1];                                            \
    uref_##group##_extract_##name##s(uref1, descriptors1, sub);             \
    uint8_t descriptors2[size1];                                            \
    uref_##group##_extract_##name##s(uref2, descriptors1, sub);             \
    return memcmp(descriptors1, descriptors2, size1);                       \
}

#ifdef __cplusplus
}
#endif
#endif
