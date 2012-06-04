/*****************************************************************************
 * uref_attr.h: upipe uref attributes handling
 *****************************************************************************
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
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
 *****************************************************************************/

#ifndef _UPIPE_UREF_ATTR_H_
/** @hidden */
#define _UPIPE_UREF_ATTR_H_

#include <stdint.h>
#include <stdarg.h>
#include <assert.h>

#include <upipe/ubase.h>
#include <upipe/uref.h>

/** @internal basic attribute types */
enum uref_attrtype {
    /** dummy type to mark the end of attributes */
    UREF_ATTRTYPE_END = 0,
    /** opaque attribute, implies size */
    UREF_ATTRTYPE_OPAQUE = 1,
    /** string attribute, implies size + NULL-terminated string */
    UREF_ATTRTYPE_STRING = 2,
    /** void attribute, just check the presence (no value) */
    UREF_ATTRTYPE_VOID = 3,
    /** bool attribute, stores 0 or 1 */
    UREF_ATTRTYPE_BOOL = 4,
    /** small unsigned attribute, stores an 8 bit unsigned integer */
    UREF_ATTRTYPE_SMALL_UNSIGNED = 5,
    /** small int attribute, stores an 8 bit signed integer */
    UREF_ATTRTYPE_SMALL_INT = 6,
    /** unsigned attribute, stores a 64 bit unsigned integer */
    UREF_ATTRTYPE_UNSIGNED = 7,
    /** int attribute, stores a 64 bit signed integer */
    UREF_ATTRTYPE_INT = 8,
    /** rational attribute, stores an struct urational */
    UREF_ATTRTYPE_RATIONAL = 9,
    /** float attribute, stores a double-precision floating point */
    UREF_ATTRTYPE_FLOAT = 10,

    /** short-hand types are above this limit */
    UREF_ATTRTYPE_SHORTHAND = 0x80,
};

/** rational type */
struct urational {
    /** numerator */
    int64_t num;
    /** denominator */
    uint64_t den;
};

/** @internal @This finds an attribute of the given name and type and returns
 * a pointer to the beginning of its value.
 *
 * @param uref pointer to the uref
 * @param name name of the attribute
 * @param type type of the attribute
 * @param size_p size of the value, written on execution
 * @return pointer to the value of the found attribute, or NULL
 */
static inline const uint8_t *uref_attr_get(struct uref *uref, const char *name,
                                           enum uref_attrtype type,
                                           size_t *size_p)
{
    return uref->mgr->uref_attr_get(uref, name, type, size_p);
}

/** @This returns the value of an opaque attribute.
 *
 * @param uref pointer to the uref
 * @param p pointer to the retrieved value (modified during execution)
 * @param size_p size of the value, written on execution
 * @param name name of the attribute
 * @return true if the attribute was found, otherwise p is not modified
 */
static inline bool uref_attr_get_opaque(struct uref *uref, const uint8_t **p,
                                        size_t *size_p, const char *name)
{
    const uint8_t *attr = uref_attr_get(uref, name, UREF_ATTRTYPE_OPAQUE,
                                        size_p);
    if (unlikely(attr == NULL)) return false;
    *p = attr;
    return true;
}

/** @This returns the value of an opaque attribute with printf-style name
 * generation.
 *
 * @param uref pointer to the uref
 * @param p pointer to the retrieved value (modified during execution)
 * @param size_p size of the value, written on execution
 * @param format printf-style format of the attribute, followed by a
 * variable list of arguments
 * @return true if the attribute was found, otherwise p is not modified
 */
static inline bool uref_attr_get_opaque_va(struct uref *uref, const uint8_t **p,
                                           size_t *size_p,
                                           const char *format, ...)
                   __attribute__ ((format(printf, 4, 5)));
/** @hidden */
static inline bool uref_attr_get_opaque_va(struct uref *uref, const uint8_t **p,
                                           size_t *size_p,
                                           const char *format, ...)
{
    UBASE_VARARG(uref_attr_get_opaque(uref, p, size_p, string))
}

/** @This returns the value of a string attribute.
 *
 * @param uref pointer to the uref
 * @param p pointer to the retrieved value (modified during execution)
 * @param name name of the attribute
 * @return true if the attribute was found, otherwise p is not modified
 */
static inline bool uref_attr_get_string(struct uref *uref, const char **p,
                                        const char *name)
{
    size_t size;
    const uint8_t *attr = uref_attr_get(uref, name, UREF_ATTRTYPE_STRING,
                                        &size);
    if (unlikely(attr == NULL)) return false;
    *p = (const char *)attr;
    assert(size > strlen(*p));
    return true;
}

/** @This checks for the presence of a void attribute.
 *
 * @param uref pointer to the uref
 * @param p actually unused, but kept for API consistency (should be NULL)
 * @param name name of the attribute
 * @return true if the attribute was found
 */
static inline bool uref_attr_get_void(struct uref *uref, void *p,
                                      const char *name)
{
    const uint8_t *attr = uref_attr_get(uref, name, UREF_ATTRTYPE_VOID, NULL);
    return (attr != NULL);
}

/** @This returns the value of a bool attribute.
 *
 * @param uref pointer to the uref
 * @param p pointer to the retrieved value (modified during execution)
 * @param name name of the attribute
 * @return true if the attribute was found, otherwise p is not modified
 */
static inline bool uref_attr_get_bool(struct uref *uref, bool *p,
                                      const char *name)
{
    size_t size;
    const uint8_t *attr = uref_attr_get(uref, name, UREF_ATTRTYPE_BOOL, &size);
    if (unlikely(attr == NULL)) return false;
    assert(size == 1);
    *p = !!*attr;
    return true;
}

/** @This returns the value of a small unsigned attribute.
 *
 * @param uref pointer to the uref
 * @param p pointer to the retrieved value (modified during execution)
 * @param name name of the attribute
 * @return true if the attribute was found, otherwise p is not modified
 */
static inline bool uref_attr_get_small_unsigned(struct uref *uref, uint8_t *p,
                                                const char *name)
{
    size_t size;
    const uint8_t *attr = uref_attr_get(uref, name,
                                        UREF_ATTRTYPE_SMALL_UNSIGNED, &size);
    if (unlikely(attr == NULL)) return false;
    assert(size == 1);
    *p = *attr;
    return true;
}

/** @This returns the value of a small int attribute.
 *
 * @param uref pointer to the uref
 * @param p pointer to the retrieved value (modified during execution)
 * @param name name of the attribute
 * @return true if the attribute was found, otherwise p is not modified
 */
static inline bool uref_attr_get_small_int(struct uref *uref, int8_t *p,
                                           const char *name)
{
    size_t size;
    const uint8_t *attr = uref_attr_get(uref, name, UREF_ATTRTYPE_SMALL_INT,
                                        &size);
    if (unlikely(attr == NULL)) return false;
    assert(size == 1);
    *p = *(int8_t *)attr;
    return true;
}

/** @internal @This unserializes a 64 bit unsigned integer.
 *
 * @param attr pointer to the start of serialized data
 * @return value of the unsigned integer
 */
static inline uint64_t uref_attr_get_uint64(const uint8_t *attr)
{
    return ((uint64_t)attr[0] << 56) | ((uint64_t)attr[1] << 48) |
           ((uint64_t)attr[2] << 40) | ((uint64_t)attr[3] << 32) |
           ((uint64_t)attr[4] << 24) | ((uint64_t)attr[5] << 16) |
           ((uint64_t)attr[6] <<  8) | (uint64_t)attr[7];
}

/** @internal @This unserializes a 64 bit signed integer.
 * FIXME: this is probably suboptimal
 *
 * @param attr pointer to the start of serialized data
 * @return value of the signed integer
 */
static inline int64_t uref_attr_get_int64(const uint8_t *attr)
{
    if (attr[0] & 0x80)
        return (-1) *
            (((uint64_t)(attr[0] & ~0x80) << 56) | ((uint64_t)attr[1] << 48) |
            ((uint64_t)attr[2] << 40) | ((uint64_t)attr[3] << 32) |
            ((uint64_t)attr[4] << 24) | ((uint64_t)attr[5] << 16) |
            ((uint64_t)attr[6] <<  8) | (uint64_t)attr[7]);
    return uref_attr_get_uint64(attr);
}

/** @This returns the value of an unsigned attribute.
 *
 * @param uref pointer to the uref
 * @param p pointer to the retrieved value (modified during execution)
 * @param name name of the attribute
 * @return true if the attribute was found, otherwise p is not modified
 */
static inline bool uref_attr_get_unsigned(struct uref *uref, uint64_t *p,
                                          const char *name)
{
    size_t size;
    const uint8_t *attr = uref_attr_get(uref, name, UREF_ATTRTYPE_UNSIGNED,
                                        &size);
    if (unlikely(attr == NULL)) return false;
    assert(size == 8);
    *p = uref_attr_get_uint64(attr);
    return true;
}

/** @This returns the value of an int attribute.
 *
 * @param uref pointer to the uref
 * @param p pointer to the retrieved value (modified during execution)
 * @param name name of the attribute
 * @return true if the attribute was found, otherwise p is not modified
 */
static inline bool uref_attr_get_int(struct uref *uref, int64_t *p,
                                     const char *name)
{
    size_t size;
    const uint8_t *attr = uref_attr_get(uref, name, UREF_ATTRTYPE_INT, &size);
    if (unlikely(attr == NULL)) return false;
    assert(size == 8);
    *p = uref_attr_get_int64(attr);
    return true;
}

/** @This returns the value of a float attribute.
 *
 * @param uref pointer to the uref
 * @param p pointer to the retrieved value (modified during execution)
 * @param name name of the attribute
 * @return true if the attribute was found, otherwise p is not modified
 */
static inline bool uref_attr_get_float(struct uref *uref, double *p,
                                       const char *name)
{
    /* FIXME: this is probably not portable */
    union {
        double f;
        uint64_t i;
    } u;
    size_t size;
    const uint8_t *attr = uref_attr_get(uref, name, UREF_ATTRTYPE_FLOAT, &size);
    if (unlikely(attr == NULL)) return false;
    assert(size == 8);
    u.i = uref_attr_get_uint64(attr);
    *p = u.f;
    return true;
}

/** @This returns the value of a rational attribute.
 *
 * @param uref pointer to the uref
 * @param p pointer to the retrieved value (modified during execution)
 * @param name name of the attribute
 * @return true if the attribute was found, otherwise p is not modified
 */
static inline bool uref_attr_get_rational(struct uref *uref,
                                          struct urational *p, const char *name)
{
    size_t size;
    const uint8_t *attr = uref_attr_get(uref, name, UREF_ATTRTYPE_RATIONAL,
                                        &size);
    if (unlikely(attr == NULL)) return false;
    assert(size == 16);
    p->num = uref_attr_get_int64(attr);
    p->den = uref_attr_get_uint64(attr + 8);
    return true;
}

/** @internal @This adds or changes an attribute (excluding the value itself).
 *
 * @param uref_p pointer to the pointer to the uref (possibly modified)
 * @param name name of the attribute
 * @param type type of the attribute
 * @param attr_size size needed to store the value of the attribute
 * @return pointer to the value of the attribute
 */
static inline uint8_t *uref_attr_set(struct uref **uref_p, const char *name,
                                     enum uref_attrtype type, size_t attr_size)
{
    return (*uref_p)->mgr->uref_attr_set(uref_p, name, type, attr_size);
}

/** @This sets the value of a opaque attribute, optionally creating it
 *
 * @param uref_p pointer to the pointer to the uref (possibly modified)
 * @param value value to set
 * @param attr_size size of the opaque value
 * @param name name of the attribute
 * @return true if no allocation failure occurred
 */
static inline bool uref_attr_set_opaque(struct uref **uref_p,
                                        const uint8_t *value, size_t attr_size,
                                        const char *name)
{
    uint8_t *attr = uref_attr_set(uref_p, name, UREF_ATTRTYPE_OPAQUE,
                                  attr_size);
    if (unlikely(attr == NULL)) return false;
    memcpy(attr, value, attr_size);
    return true;
}

/** @This sets the value of an opaque attribute, optionally creating it, with
 * printf-style name generation.
 *
 * @param uref_p pointer to the pointer to the uref (possibly modified)
 * @param value value to set
 * @param attr_size size of the opaque value
 * @param format printf-style format of the attribute, followed by a
 * variable list of arguments
 * @return true if no allocation failure occurred
 */
static inline bool uref_attr_set_opaque_va(struct uref **uref_p,
                                           const uint8_t *value,
                                           size_t attr_size,
                                           const char *format, ...)
                   __attribute__ ((format(printf, 4, 5)));
/** @hidden */
static inline bool uref_attr_set_opaque_va(struct uref **uref_p,
                                           const uint8_t *value,
                                           size_t attr_size,
                                           const char *format, ...)
{
    UBASE_VARARG(uref_attr_set_opaque(uref_p, value, attr_size, string))
}

/** @This sets the value of a string attribute, optionally creating it
 *
 * @param uref_p pointer to the pointer to the uref (possibly modified)
 * @param value value to set
 * @param name name of the attribute
 * @return true if no allocation failure occurred
 */
static inline bool uref_attr_set_string(struct uref **uref_p, const char *value,
                                        const char *name)
{
    size_t attr_size = strlen(value) + 1;
    uint8_t *attr = uref_attr_set(uref_p, name, UREF_ATTRTYPE_STRING,
                                  attr_size);
    if (unlikely(attr == NULL)) return false;
    memcpy(attr, value, attr_size);
    return true;
}

/** @This sets the value of a void attribute, optionally creating it
 *
 * @param uref_p pointer to the pointer to the uref (possibly modified)
 * @param value actually unused, but kept for API consistency (should be NULL)
 * @param name name of the attribute
 * @return true if no allocation failure occurred
 */
static inline bool uref_attr_set_void(struct uref **uref_p, void *value,
                                      const char *name)
{
    uint8_t *attr = uref_attr_set(uref_p, name, UREF_ATTRTYPE_VOID, 0);
    return (attr != NULL);
}

/** @This sets the value of a bool attribute, optionally creating it
 *
 * @param uref_p pointer to the pointer to the uref (possibly modified)
 * @param value value to set
 * @param name name of the attribute
 * @return true if no allocation failure occurred
 */
static inline bool uref_attr_set_bool(struct uref **uref_p, bool value,
                                      const char *name)
{
    uint8_t *attr = uref_attr_set(uref_p, name, UREF_ATTRTYPE_BOOL, 1);
    if (unlikely(attr == NULL)) return false;
    *attr = value ? 1 : 0;
    return true;
}

/** @This sets the value of a small unsigned attribute, optionally creating it
 *
 * @param uref_p pointer to the pointer to the uref (possibly modified)
 * @param value value to set
 * @param name name of the attribute
 * @return true if no allocation failure occurred
 */
static inline bool uref_attr_set_small_unsigned(struct uref **uref_p,
                                                uint8_t value, const char *name)
{
    uint8_t *attr = uref_attr_set(uref_p, name, UREF_ATTRTYPE_SMALL_UNSIGNED,
                                  1);
    if (unlikely(attr == NULL)) return false;
    *attr = value;
    return true;
}

/** @This sets the value of a small int attribute, optionally creating it
 *
 * @param uref_p pointer to the pointer to the uref (possibly modified)
 * @param value value to set
 * @param name name of the attribute
 * @return true if no allocation failure occurred
 */
static inline bool uref_attr_set_small_int(struct uref **uref_p, int8_t value,
                                           const char *name)
{
    uint8_t *attr = uref_attr_set(uref_p, name, UREF_ATTRTYPE_SMALL_INT, 1);
    if (unlikely(attr == NULL)) return false;
    int8_t *value_p = (int8_t *)attr;
    *value_p = value;
    return true;
}

/** @internal @This serializes a 64 bit unsigned integer.
 *
 * @param attr pointer to the start of serialized data
 * @param value value of the unsigned integer
 */
static inline void uref_attr_set_uint64(uint8_t *attr, uint64_t value)
{
    attr[0] = value >> 56;
    attr[1] = (value >> 48) & 0xff;
    attr[2] = (value >> 40) & 0xff;
    attr[3] = (value >> 32) & 0xff;
    attr[4] = (value >> 24) & 0xff;
    attr[5] = (value >> 16) & 0xff;
    attr[6] = (value >> 8) & 0xff;
    attr[7] = value & 0xff;
}

/** @internal @This serializes a 64 bit signed integer.
 *
 * @param attr pointer to the start of serialized data
 * @param value value of the signed integer
 */
static inline void uref_attr_set_int64(uint8_t *attr, int64_t value)
{
    if (value > 0)
        return uref_attr_set_uint64(attr, value);

    assert(value != INT64_MIN);
    value *= -1;
    uref_attr_set_uint64(attr, value);
    attr[0] |= 0x80;
}

/** @This sets the value of an unsigned attribute, optionally creating it
 *
 * @param uref_p pointer to the pointer to the uref (possibly modified)
 * @param value value to set
 * @param name name of the attribute
 * @return true if no allocation failure occurred
 */
static inline bool uref_attr_set_unsigned(struct uref **uref_p, uint64_t value,
                                          const char *name)
{
    uint8_t *attr = uref_attr_set(uref_p, name, UREF_ATTRTYPE_UNSIGNED, 8);
    if (unlikely(attr == NULL)) return false;
    uref_attr_set_uint64(attr, value);
    return true;
}

/** @This sets the value of an int attribute, optionally creating it
 *
 * @param uref_p pointer to the pointer to the uref (possibly modified)
 * @param value value to set
 * @param name name of the attribute
 * @return true if no allocation failure occurred
 */
static inline bool uref_attr_set_int(struct uref **uref_p, uint64_t value,
                                     const char *name)
{
    uint8_t *attr = uref_attr_set(uref_p, name, UREF_ATTRTYPE_INT, 8);
    if (unlikely(attr == NULL)) return false;
    uref_attr_set_int64(attr, value);
    return true;
}

/** @This sets the value of a float attribute, optionally creating it
 *
 * @param uref_p pointer to the pointer to the uref (possibly modified)
 * @param value value to set
 * @param name name of the attribute
 * @return true if no allocation failure occurred
 */
static inline bool uref_attr_set_float(struct uref **uref_p, double value,
                                       const char *name)
{
    /* FIXME: this is probably not portable */
    union {
        double f;
        uint64_t i;
    } u;
    uint8_t *attr = uref_attr_set(uref_p, name, UREF_ATTRTYPE_FLOAT, 8);
    if (unlikely(attr == NULL)) return false;
    u.f = value;
    uref_attr_set_uint64(attr, u.i);
    return true;
}

/** @This sets the value of a rational attribute, optionally creating it
 *
 * @param uref_p pointer to the pointer to the uref (possibly modified)
 * @param value value to set
 * @param name name of the attribute
 * @return true if no allocation failure occurred
 */
static inline bool uref_attr_set_rational(struct uref **uref_p,
                                          struct urational value,
                                          const char *name)
{
    uint8_t *attr = uref_attr_set(uref_p, name, UREF_ATTRTYPE_RATIONAL, 16);
    if (unlikely(attr == NULL)) return false;
    uref_attr_set_int64(attr, value.num);
    uref_attr_set_uint64(attr + 8, value.den);
    return true;
}

/** @internal @This deletes an attribute.
 *
 * @param uref pointer to the uref
 * @param name name of the attribute
 * @param type type of the attribute
 * @return true if the attribute existed before
 */
static inline bool uref_attr_delete(struct uref *uref, const char *name,
                                    enum uref_attrtype type)
{
    return uref->mgr->uref_attr_delete(uref, name, type);
}
/* type-specific deletion primitives are simple and auto-generated below */

/** @internal This template allows to quickly define printf-stype get and
 * set functions, except for opaque type.
 *
 * @param type upipe attribute type name
 * @param ctype type of the attribute value in C
 */
#define UREF_ATTR_TEMPLATE_TYPE1(type, ctype)                               \
/** @This returns the value of a type attribute with printf-style name      \
 * generation.                                                              \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param p pointer to the retrieved value (modified during execution)      \
 * @param format printf-style format of the attribute, followed by a        \
 * variable list of arguments                                               \
 * @return true if the attribute was found, otherwise p is not modified     \
 */                                                                         \
static inline bool uref_attr_get_##type##_va(struct uref *uref, ctype *p,   \
                                             const char *format, ...)       \
                   __attribute__ ((format(printf, 3, 4)));                  \
/** @hidden */                                                              \
static inline bool uref_attr_get_##type##_va(struct uref *uref, ctype *p,   \
                                             const char *format, ...)       \
{                                                                           \
    UBASE_VARARG(uref_attr_get_##type(uref, p, string))                     \
}                                                                           \
/** @This sets the value of a type attribute, optionally creating it, with  \
 * printf-style name generation.                                            \
 *                                                                          \
 * @param uref_p pointer to the pointer to the uref (possibly modified)     \
 * @param value value to set                                                \
 * @param format printf-style format of the attribute, followed by a        \
 * variable list of arguments                                               \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_attr_set_##type##_va(struct uref **uref_p,          \
                                             ctype value,                   \
                                             const char *format, ...)       \
                   __attribute__ ((format(printf, 3, 4)));                  \
/** @hidden */                                                              \
static inline bool uref_attr_set_##type##_va(struct uref **uref_p,          \
                                             ctype value,                   \
                                             const char *format, ...)       \
{                                                                           \
    UBASE_VARARG(uref_attr_set_##type(uref_p, value, string))               \
}

UREF_ATTR_TEMPLATE_TYPE1(string, const char *)
UREF_ATTR_TEMPLATE_TYPE1(void, void *)
UREF_ATTR_TEMPLATE_TYPE1(bool, bool)
UREF_ATTR_TEMPLATE_TYPE1(small_unsigned, uint8_t)
UREF_ATTR_TEMPLATE_TYPE1(small_int, int8_t)
UREF_ATTR_TEMPLATE_TYPE1(unsigned, uint64_t)
UREF_ATTR_TEMPLATE_TYPE1(int, int64_t)
UREF_ATTR_TEMPLATE_TYPE1(float, double)
UREF_ATTR_TEMPLATE_TYPE1(rational, struct urational)
#undef UREF_ATTR_TEMPLATE_TYPE1

/** @internal This template allows to quickly define type-specific delete
 * functions
 *
 * @param type upipe attribute type name
 * @param ctype type of the attribute value in C
 * @param attrtype upipe attribute type enum
 */
#define UREF_ATTR_TEMPLATE_TYPE2(type, ctype, attrtype)                     \
/** @This deletes a type attribute.                                         \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param name name of the attribute                                        \
 * @return true if the attribute existed before                             \
 */                                                                         \
static inline bool uref_attr_delete_##type(struct uref *uref, const char *name)  \
{                                                                           \
    return uref_attr_delete(uref, name, attrtype);                          \
}                                                                           \
/** @This deletes a type attribute with printf-style name generation.       \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param format printf-style format of the attribute, followed by a        \
 * variable list of arguments                                               \
 * @return true if the attribute existed before                             \
 */                                                                         \
static inline bool uref_attr_delete_##type##_va(struct uref *uref,          \
                                                const char *format, ...)    \
                   __attribute__ ((format(printf, 2, 3)));                  \
/** @hidden */                                                              \
static inline bool uref_attr_delete_##type##_va(struct uref *uref,          \
                                                const char *format, ...)    \
{                                                                           \
    UBASE_VARARG(uref_attr_delete_##type(uref, string))                     \
}

UREF_ATTR_TEMPLATE_TYPE2(opaque, uint8_t *, UREF_ATTRTYPE_OPAQUE)
UREF_ATTR_TEMPLATE_TYPE2(string, const char *, UREF_ATTRTYPE_STRING)
UREF_ATTR_TEMPLATE_TYPE2(void, void *, UREF_ATTRTYPE_VOID)
UREF_ATTR_TEMPLATE_TYPE2(bool, bool, UREF_ATTRTYPE_BOOL)
UREF_ATTR_TEMPLATE_TYPE2(small_unsigned, uint8_t, UREF_ATTRTYPE_SMALL_UNSIGNED)
UREF_ATTR_TEMPLATE_TYPE2(small_int, int8_t, UREF_ATTRTYPE_SMALL_INT)
UREF_ATTR_TEMPLATE_TYPE2(unsigned, uint64_t, UREF_ATTRTYPE_UNSIGNED)
UREF_ATTR_TEMPLATE_TYPE2(int, int64_t, UREF_ATTRTYPE_INT)
UREF_ATTR_TEMPLATE_TYPE2(float, double, UREF_ATTRTYPE_FLOAT)
UREF_ATTR_TEMPLATE_TYPE2(rational, struct urational, UREF_ATTRTYPE_RATIONAL)
#undef UREF_ATTR_TEMPLATE_TYPE2

/* @This allows to define accessors for a standard attribute.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param name string defining the attribute
 * @param type upipe attribute type name
 * @param ctype type of the attribute value in C
 * @param desc description of the attribute
 */
#define UREF_ATTR_TEMPLATE(group, attr, name, type, ctype, desc)            \
/** @This returns the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param p pointer to the retrieved value (modified during execution)      \
 * @return true if the attribute was found, otherwise p is not modified     \
 */                                                                         \
static inline bool uref_##group##_get_##attr(struct uref *uref, ctype *p)   \
{                                                                           \
    return uref_attr_get_##type(uref, p, name);                             \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref_p reference to the pointer to the uref (possibly modified)   \
 * @param value value to set                                                \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_set_##attr(struct uref **uref_p,          \
                                             ctype value)                   \
{                                                                           \
    return uref_attr_set_##type(uref_p, value, name);                       \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_delete_##attr(struct uref *uref)          \
{                                                                           \
    return uref_attr_delete_##type(uref, name);                             \
}

/* @This allows to define accessors for a standard void attribute.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param name string defining the attribute
 * @param desc description of the attribute
 */
#define UREF_ATTR_TEMPLATE_VOID(group, attr, name, desc)                    \
/** @This returns the presence of a desc attribute in a uref.               \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return true if the attribute was found, otherwise p is not modified     \
 */                                                                         \
static inline bool uref_##group##_get_##attr(struct uref *uref)             \
{                                                                           \
    return uref_attr_get_void(uref, NULL, name);                            \
}                                                                           \
/** @This sets a desc attribute in a uref.                                  \
 *                                                                          \
 * @param uref_p reference to the pointer to the uref (possibly modified)   \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_set_##attr(struct uref **uref_p)          \
{                                                                           \
    return uref_attr_set_void(uref_p, NULL, name);                          \
}                                                                           \
/** @This deletes a desc attribute from a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_delete_##attr(struct uref *uref)          \
{                                                                           \
    return uref_attr_delete_void(uref, name);                               \
}

/* @This allows to define accessors for a standard attribute, with a name
 * depending on printf arguments.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param format printf-style format of the attribute
 * @param type upipe attribute type name
 * @param ctype type of the attribute value in C
 * @param desc description of the attribute
 */
#define UREF_ATTR_TEMPLATE_VA(group, attr, format, type, ctype, desc,       \
                              args_decl, args)                              \
/** @This returns the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param p pointer to the retrieved value (modified during execution)      \
 * @return true if the attribute was found, otherwise p is not modified     \
 */                                                                         \
static inline bool uref_##group##_get_##attr(struct uref *uref, ctype *p,   \
                                             args_decl)                     \
{                                                                           \
    return uref_attr_get_##type##_va(uref, p, format, args);                \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref_p reference to the pointer to the uref (possibly modified)   \
 * @param value value to set                                                \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_set_##attr(struct uref **uref_p,          \
                                             ctype value, args_decl)        \
{                                                                           \
    return uref_attr_set_##type##_va(uref_p, value, format, args);          \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_delete_##attr(struct uref *uref,          \
                                                args_decl)                  \
{                                                                           \
    return uref_attr_delete_##type##_va(uref, format, args);                \
}

/* @This allows to define accessors for a standard void attribute, with a name
 * depending on printf arguments.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param format printf-style format of the attribute
 * @param desc description of the attribute
 */
#define UREF_ATTR_TEMPLATE_VOID_VA(group, attr, name, desc, args_decl, args)\
/** @This returns the presence of a desc attribute in a uref.               \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return true if the attribute was found, otherwise p is not modified     \
 */                                                                         \
static inline bool uref_##group##_get_##attr(struct uref *uref, args_decl)  \
{                                                                           \
    return uref_attr_get_void_va(uref, NULL, format, args);                 \
}                                                                           \
/** @This sets a desc attribute in a uref.                                  \
 *                                                                          \
 * @param uref_p reference to the pointer to the uref (possibly modified)   \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_set_##attr(struct uref **uref_p,          \
                                             args_decl)                     \
{                                                                           \
    return uref_attr_set_void_va(uref_p, NULL, format, args);               \
}                                                                           \
/** @This deletes a desc attribute from a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_delete_##attr(struct uref *uref,          \
                                                args_decl)                  \
{                                                                           \
    return uref_attr_delete_void_va(uref, format, args);                    \
}

#endif
