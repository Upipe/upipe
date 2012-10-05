/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
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
 * @short Upipe dictionary of attributes
 */

#ifndef _UPIPE_UDICT_H_
/** @hidden */
#define _UPIPE_UDICT_H_

#include <upipe/ubase.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/** @internal @This defines basic attribute types. */
enum udict_type {
    /** dummy type to mark the end of attributes */
    UDICT_TYPE_END = 0,
    /** opaque attribute, implies size */
    UDICT_TYPE_OPAQUE = 1,
    /** string attribute, implies size + NULL-terminated string */
    UDICT_TYPE_STRING = 2,
    /** void attribute, just check the presence (no value) */
    UDICT_TYPE_VOID = 3,
    /** bool attribute, stores 0 or 1 */
    UDICT_TYPE_BOOL = 4,
    /** small unsigned attribute, stores an 8 bit unsigned integer */
    UDICT_TYPE_SMALL_UNSIGNED = 5,
    /** small int attribute, stores an 8 bit signed integer */
    UDICT_TYPE_SMALL_INT = 6,
    /** unsigned attribute, stores a 64 bit unsigned integer */
    UDICT_TYPE_UNSIGNED = 7,
    /** int attribute, stores a 64 bit signed integer */
    UDICT_TYPE_INT = 8,
    /** rational attribute, stores an urational */
    UDICT_TYPE_RATIONAL = 9,
    /** float attribute, stores a double-precision floating point */
    UDICT_TYPE_FLOAT = 10,

    /** short-hand types are above this limit */
    UDICT_TYPE_SHORTHAND = 0x80,
};

/** @This defines the rational type. */
struct urational {
    /** numerator */
    int64_t num;
    /** denominator */
    uint64_t den;
};

/** @This defines standard commands which udict modules may implement. */
enum udict_command {
    /** duplicate a given udict (struct udict **) */
    UDICT_DUP,
    /** get the name and type of the next attribute (const char **,
     * enum udict_type *) */
    UDICT_ITERATE,
    /** get an attribute (const char *, enum udict_type, size_t *,
     * const uint8_t **) */
    UDICT_GET,
    /** set an attribute (const char *, enum udict_type, size_t, uint8_t **) */
    UDICT_SET,
    /** delete an attribute (const char *, enum udict_type) */
    UDICT_DELETE,

    /** non-standard commands implemented by a module type can start from
     * there (first arg = signature) */
    UDICT_CONTROL_LOCAL = 0x8000
};

/** @hidden */
struct udict_mgr;

/** @This stores a dictionary of attributes.
 *
 * The structure is not refcounted and shouldn't be used by more than one
 * module at once.
 */
struct udict {
    /** pointer to the entity responsible for the management */
    struct udict_mgr *mgr;
};

/** @This stores common management parameters for a udict pool.
 */
struct udict_mgr {
    /** function to allocate a udict with a given initial size */
    struct udict *(*udict_alloc)(struct udict_mgr *, size_t);
    /** control function for standard or local commands */
    bool (*udict_control)(struct udict *, enum udict_command, va_list);
    /** function to free a udict */
    void (*udict_free)(struct udict *);

    /** function to release all buffers kept in pools */
    void (*udict_mgr_vacuum)(struct udict_mgr *);
    /** function to increment the refcount of the udict manager */
    void (*udict_mgr_use)(struct udict_mgr *);
    /** function to decrement the refcount of the udict manager or free it */
    void (*udict_mgr_release)(struct udict_mgr *);
};

/** @This allocates and initializes a new udict.
 *
 * @param mgr management structure for this buffer pool
 * @param size initial size of the attribute space
 * @return allocated udict or NULL in case of allocation failure
 */
static inline struct udict *udict_alloc(struct udict_mgr *mgr, size_t size)
{
    return mgr->udict_alloc(mgr, size);
}

/** @internal @This sends a control command to the udict.
 *
 * @param upipe description structure of the pipe
 * @param command control command to send, followed by optional read or write
 * parameters
 * @return false in case of error
 */
static inline bool udict_control(struct udict *udict,
                                 enum udict_command command, ...)
{
    bool ret;
    va_list args;
    va_start(args, command);
    ret = udict->mgr->udict_control(udict, command, args);
    va_end(args);
    return ret;
}

/** @This duplicates a given udict.
 *
 * @param udict pointer to udict
 * @return duplicated udict
 */
static inline struct udict *udict_dup(struct udict *udict)
{
    struct udict *dup_udict;
    if (unlikely(!udict_control(udict, UDICT_DUP, &dup_udict)))
        return NULL;
    return dup_udict;
}

/** @This finds an attribute of the given name and type and returns
 * the name and type of the next attribute.
 *
 * @param udict pointer to the udict
 * @param name_p reference to the name of the attribute to find, changed during
 * execution to the name of the next attribute, or NULL if it was the last
 * attribute; if it was NULL, it is changed to the name of the first attribute
 * @param type_p reference to the type of the attribute, if the name is valid
 * @return false in case of error
 */
static inline bool udict_iterate(struct udict *udict, const char **name_p,
                                 enum udict_type *type_p)
{
    return udict_control(udict, UDICT_ITERATE, name_p, type_p);
}

/** @internal @This finds an attribute of the given name and type and returns
 * a pointer to the beginning of its value.
 *
 * @param udict pointer to the udict
 * @param name name of the attribute
 * @param type type of the attribute
 * @param size_p size of the value, written on execution
 * @return pointer to the value of the found attribute, or NULL
 */
static inline const uint8_t *udict_get(struct udict *udict, const char *name,
                                       enum udict_type type, size_t *size_p)
{
    const uint8_t *p;
    if (unlikely(!udict_control(udict, UDICT_GET, name, type, size_p, &p)))
        return NULL;
    return p;
}

/** @This returns the value of an opaque attribute.
 *
 * @param udict pointer to the udict
 * @param p pointer to the retrieved value (modified during execution)
 * @param size_p size of the value, written on execution
 * @param name name of the attribute
 * @return true if the attribute was found, otherwise p is not modified
 */
static inline bool udict_get_opaque(struct udict *udict, const uint8_t **p,
                                    size_t *size_p, const char *name)
{
    const uint8_t *attr = udict_get(udict, name, UDICT_TYPE_OPAQUE, size_p);
    if (unlikely(attr == NULL))
        return false;
    *p = attr;
    return true;
}

/** @This returns the value of an opaque attribute with printf-style name
 * generation.
 *
 * @param udict pointer to the udict
 * @param p pointer to the retrieved value (modified during execution)
 * @param size_p size of the value, written on execution
 * @param format printf-style format of the attribute, followed by a
 * variable list of arguments
 * @return true if the attribute was found, otherwise p is not modified
 */
static inline bool udict_get_opaque_va(struct udict *udict, const uint8_t **p,
                                       size_t *size_p, const char *format, ...)
                   __attribute__ ((format(printf, 4, 5)));
/** @hidden */
static inline bool udict_get_opaque_va(struct udict *udict, const uint8_t **p,
                                       size_t *size_p, const char *format, ...)
{
    UBASE_VARARG(udict_get_opaque(udict, p, size_p, string))
}

/** @This returns the value of a string attribute.
 *
 * @param udict pointer to the udict
 * @param p pointer to the retrieved value (modified during execution)
 * @param name name of the attribute
 * @return true if the attribute was found, otherwise p is not modified
 */
static inline bool udict_get_string(struct udict *udict, const char **p,
                                    const char *name)
{
    size_t size;
    const uint8_t *attr = udict_get(udict, name, UDICT_TYPE_STRING, &size);
    if (unlikely(attr == NULL))
        return false;
    *p = (const char *)attr;
    assert(size > strlen(*p));
    return true;
}

/** @This checks for the presence of a void attribute.
 *
 * @param udict pointer to the udict
 * @param p actually unused, but kept for API consistency (should be NULL)
 * @param name name of the attribute
 * @return true if the attribute was found
 */
static inline bool udict_get_void(struct udict *udict, void *p,
                                  const char *name)
{
    const uint8_t *attr = udict_get(udict, name, UDICT_TYPE_VOID, NULL);
    return (attr != NULL);
}

/** @This returns the value of a bool attribute.
 *
 * @param udict pointer to the udict
 * @param p pointer to the retrieved value (modified during execution)
 * @param name name of the attribute
 * @return true if the attribute was found, otherwise p is not modified
 */
static inline bool udict_get_bool(struct udict *udict, bool *p,
                                  const char *name)
{
    size_t size;
    const uint8_t *attr = udict_get(udict, name, UDICT_TYPE_BOOL, &size);
    if (unlikely(attr == NULL))
        return false;
    assert(size == 1);
    *p = !!*attr;
    return true;
}

/** @This returns the value of a small unsigned attribute.
 *
 * @param udict pointer to the udict
 * @param p pointer to the retrieved value (modified during execution)
 * @param name name of the attribute
 * @return true if the attribute was found, otherwise p is not modified
 */
static inline bool udict_get_small_unsigned(struct udict *udict, uint8_t *p,
                                            const char *name)
{
    size_t size;
    const uint8_t *attr = udict_get(udict, name,
                                    UDICT_TYPE_SMALL_UNSIGNED, &size);
    if (unlikely(attr == NULL))
        return false;
    assert(size == 1);
    *p = *attr;
    return true;
}

/** @This returns the value of a small int attribute.
 *
 * @param udict pointer to the udict
 * @param p pointer to the retrieved value (modified during execution)
 * @param name name of the attribute
 * @return true if the attribute was found, otherwise p is not modified
 */
static inline bool udict_get_small_int(struct udict *udict, int8_t *p,
                                       const char *name)
{
    size_t size;
    const uint8_t *attr = udict_get(udict, name, UDICT_TYPE_SMALL_INT, &size);
    if (unlikely(attr == NULL))
        return false;
    assert(size == 1);
    *p = *(int8_t *)attr;
    return true;
}

/** @internal @This unserializes a 64 bit unsigned integer.
 *
 * @param attr pointer to the start of serialized data
 * @return value of the unsigned integer
 */
static inline uint64_t udict_get_uint64(const uint8_t *attr)
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
static inline int64_t udict_get_int64(const uint8_t *attr)
{
    if (attr[0] & 0x80)
        return (-1) *
            (((uint64_t)(attr[0] & ~0x80) << 56) | ((uint64_t)attr[1] << 48) |
            ((uint64_t)attr[2] << 40) | ((uint64_t)attr[3] << 32) |
            ((uint64_t)attr[4] << 24) | ((uint64_t)attr[5] << 16) |
            ((uint64_t)attr[6] <<  8) | (uint64_t)attr[7]);
    return udict_get_uint64(attr);
}

/** @This returns the value of an unsigned attribute.
 *
 * @param udict pointer to the udict
 * @param p pointer to the retrieved value (modified during execution)
 * @param name name of the attribute
 * @return true if the attribute was found, otherwise p is not modified
 */
static inline bool udict_get_unsigned(struct udict *udict, uint64_t *p,
                                      const char *name)
{
    size_t size;
    const uint8_t *attr = udict_get(udict, name, UDICT_TYPE_UNSIGNED,
                                        &size);
    if (unlikely(attr == NULL))
        return false;
    assert(size == 8);
    *p = udict_get_uint64(attr);
    return true;
}

/** @This returns the value of an int attribute.
 *
 * @param udict pointer to the udict
 * @param p pointer to the retrieved value (modified during execution)
 * @param name name of the attribute
 * @return true if the attribute was found, otherwise p is not modified
 */
static inline bool udict_get_int(struct udict *udict, int64_t *p,
                                 const char *name)
{
    size_t size;
    const uint8_t *attr = udict_get(udict, name, UDICT_TYPE_INT, &size);
    if (unlikely(attr == NULL))
        return false;
    assert(size == 8);
    *p = udict_get_int64(attr);
    return true;
}

/** @This returns the value of a float attribute.
 *
 * @param udict pointer to the udict
 * @param p pointer to the retrieved value (modified during execution)
 * @param name name of the attribute
 * @return true if the attribute was found, otherwise p is not modified
 */
static inline bool udict_get_float(struct udict *udict, double *p,
                                   const char *name)
{
    /* FIXME: this is probably not portable */
    union {
        double f;
        uint64_t i;
    } u;
    size_t size;
    const uint8_t *attr = udict_get(udict, name, UDICT_TYPE_FLOAT, &size);
    if (unlikely(attr == NULL))
        return false;
    assert(size == 8);
    u.i = udict_get_uint64(attr);
    *p = u.f;
    return true;
}

/** @This returns the value of a rational attribute.
 *
 * @param udict pointer to the udict
 * @param p pointer to the retrieved value (modified during execution)
 * @param name name of the attribute
 * @return true if the attribute was found, otherwise p is not modified
 */
static inline bool udict_get_rational(struct udict *udict, struct urational *p,
                                      const char *name)
{
    size_t size;
    const uint8_t *attr = udict_get(udict, name, UDICT_TYPE_RATIONAL, &size);
    if (unlikely(attr == NULL))
        return false;
    assert(size == 16);
    p->num = udict_get_int64(attr);
    p->den = udict_get_uint64(attr + 8);
    return true;
}

/** @internal @This adds or changes an attribute (excluding the value itself).
 *
 * @param udict the pointer to the udict
 * @param name name of the attribute
 * @param type type of the attribute
 * @param attr_size size needed to store the value of the attribute
 * @return pointer to the value of the attribute, or NULL in case of error
 */
static inline uint8_t *udict_set(struct udict *udict, const char *name,
                                 enum udict_type type, size_t attr_size)
{
    uint8_t *p;
    if (unlikely(!udict_control(udict, UDICT_SET, name, type, attr_size, &p)))
        return NULL;
    return p;
}

/** @This sets the value of a opaque attribute, optionally creating it
 *
 * @param udict the pointer to the udict
 * @param value value to set
 * @param attr_size size of the opaque value
 * @param name name of the attribute
 * @return true if no allocation failure occurred
 */
static inline bool udict_set_opaque(struct udict *udict,
                                    const uint8_t *value, size_t attr_size,
                                    const char *name)
{
    uint8_t *attr = udict_set(udict, name, UDICT_TYPE_OPAQUE, attr_size);
    if (unlikely(attr == NULL))
        return false;
    memcpy(attr, value, attr_size);
    return true;
}

/** @This sets the value of an opaque attribute, optionally creating it, with
 * printf-style name generation.
 
 * @param udict the pointer to the udict
 * @param value value to set
 * @param attr_size size of the opaque value
 * @param format printf-style format of the attribute, followed by a
 * variable list of arguments
 * @return true if no allocation failure occurred
 */
static inline bool udict_set_opaque_va(struct udict *udict,
                                       const uint8_t *value,
                                       size_t attr_size,
                                       const char *format, ...)
                   __attribute__ ((format(printf, 4, 5)));
/** @hidden */
static inline bool udict_set_opaque_va(struct udict *udict,
                                       const uint8_t *value,
                                       size_t attr_size,
                                       const char *format, ...)
{
    UBASE_VARARG(udict_set_opaque(udict, value, attr_size, string))
}

/** @This sets the value of a string attribute, optionally creating it.
 *
 * @param udict the pointer to the udict
 * @param value value to set
 * @param name name of the attribute
 * @return true if no allocation failure occurred
 */
static inline bool udict_set_string(struct udict *udict, const char *value,
                                    const char *name)
{
    size_t attr_size = strlen(value) + 1;
    uint8_t *attr = udict_set(udict, name, UDICT_TYPE_STRING, attr_size);
    if (unlikely(attr == NULL))
        return false;
    memcpy(attr, value, attr_size);
    return true;
}

/** @This sets the value of a void attribute, optionally creating it.
 *
 * @param udict the pointer to the udict
 * @param value actually unused, but kept for API consistency (should be NULL)
 * @param name name of the attribute
 * @return true if no allocation failure occurred
 */
static inline bool udict_set_void(struct udict *udict, void *value,
                                  const char *name)
{
    uint8_t *attr = udict_set(udict, name, UDICT_TYPE_VOID, 0);
    return (attr != NULL);
}

/** @This sets the value of a bool attribute, optionally creating it.
 *
 * @param udict the pointer to the udict
 * @param value value to set
 * @param name name of the attribute
 * @return true if no allocation failure occurred
 */
static inline bool udict_set_bool(struct udict *udict, bool value,
                                  const char *name)
{
    uint8_t *attr = udict_set(udict, name, UDICT_TYPE_BOOL, 1);
    if (unlikely(attr == NULL))
        return false;
    *attr = value ? 1 : 0;
    return true;
}

/** @This sets the value of a small unsigned attribute, optionally creating it.
 *
 * @param udict the pointer to the udict
 * @param value value to set
 * @param name name of the attribute
 * @return true if no allocation failure occurred
 */
static inline bool udict_set_small_unsigned(struct udict *udict,
                                            uint8_t value, const char *name)
{
    uint8_t *attr = udict_set(udict, name, UDICT_TYPE_SMALL_UNSIGNED, 1);
    if (unlikely(attr == NULL))
        return false;
    *attr = value;
    return true;
}

/** @This sets the value of a small int attribute, optionally creating it.
 *
 * @param udict the pointer to the udict
 * @param value value to set
 * @param name name of the attribute
 * @return true if no allocation failure occurred
 */
static inline bool udict_set_small_int(struct udict *udict, int8_t value,
                                       const char *name)
{
    uint8_t *attr = udict_set(udict, name, UDICT_TYPE_SMALL_INT, 1);
    if (unlikely(attr == NULL))
        return false;
    int8_t *value_p = (int8_t *)attr;
    *value_p = value;
    return true;
}

/** @internal @This serializes a 64 bit unsigned integer.
 *
 * @param attr pointer to the start of serialized data
 * @param value value of the unsigned integer
 */
static inline void udict_set_uint64(uint8_t *attr, uint64_t value)
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
static inline void udict_set_int64(uint8_t *attr, int64_t value)
{
    if (value > 0)
        return udict_set_uint64(attr, value);

    assert(value != INT64_MIN);
    value *= -1;
    udict_set_uint64(attr, value);
    attr[0] |= 0x80;
}

/** @This sets the value of an unsigned attribute, optionally creating it.
 *
 * @param udict the pointer to the udict
 * @param value value to set
 * @param name name of the attribute
 * @return true if no allocation failure occurred
 */
static inline bool udict_set_unsigned(struct udict *udict, uint64_t value,
                                      const char *name)
{
    uint8_t *attr = udict_set(udict, name, UDICT_TYPE_UNSIGNED, 8);
    if (unlikely(attr == NULL))
        return false;
    udict_set_uint64(attr, value);
    return true;
}

/** @This sets the value of an int attribute, optionally creating it.
 *
 * @param udict the pointer to the udict
 * @param value value to set
 * @param name name of the attribute
 * @return true if no allocation failure occurred
 */
static inline bool udict_set_int(struct udict *udict, uint64_t value,
                                 const char *name)
{
    uint8_t *attr = udict_set(udict, name, UDICT_TYPE_INT, 8);
    if (unlikely(attr == NULL))
        return false;
    udict_set_int64(attr, value);
    return true;
}

/** @This sets the value of a float attribute, optionally creating it.
 *
 * @param udict the pointer to the udict
 * @param value value to set
 * @param name name of the attribute
 * @return true if no allocation failure occurred
 */
static inline bool udict_set_float(struct udict *udict, double value,
                                   const char *name)
{
    /* FIXME: this is probably not portable */
    union {
        double f;
        uint64_t i;
    } u;
    uint8_t *attr = udict_set(udict, name, UDICT_TYPE_FLOAT, 8);
    if (unlikely(attr == NULL))
        return false;
    u.f = value;
    udict_set_uint64(attr, u.i);
    return true;
}

/** @This sets the value of a rational attribute, optionally creating it.
 *
 * @param udict the pointer to the udict
 * @param value value to set
 * @param name name of the attribute
 * @return true if no allocation failure occurred
 */
static inline bool udict_set_rational(struct udict *udict,
                                      struct urational value, const char *name)
{
    uint8_t *attr = udict_set(udict, name, UDICT_TYPE_RATIONAL, 16);
    if (unlikely(attr == NULL))
        return false;
    udict_set_int64(attr, value.num);
    udict_set_uint64(attr + 8, value.den);
    return true;
}

/** @internal @This deletes an attribute.
 *
 * @param udict pointer to the udict
 * @param name name of the attribute
 * @param type type of the attribute
 * @return true if the attribute existed before
 */
static inline bool udict_delete(struct udict *udict, const char *name,
                                enum udict_type type)
{
    return udict_control(udict, UDICT_DELETE, name, type);
}
/* type-specific deletion primitives are simple and auto-generated below */

/** @internal This template allows to quickly define printf-stype get and
 * set functions, except for opaque type.
 *
 * @param type upipe attribute type name
 * @param ctype type of the attribute value in C
 */
#define UDICT_TEMPLATE_TYPE1(type, ctype)                                   \
/** @This returns the value of a type attribute with printf-style name      \
 * generation.                                                              \
 *                                                                          \
 * @param udict pointer to the udict                                        \
 * @param p pointer to the retrieved value (modified during execution)      \
 * @param format printf-style format of the attribute, followed by a        \
 * variable list of arguments                                               \
 * @return true if the attribute was found, otherwise p is not modified     \
 */                                                                         \
static inline bool udict_get_##type##_va(struct udict *udict, ctype *p,     \
                                         const char *format, ...)           \
                   __attribute__ ((format(printf, 3, 4)));                  \
/** @hidden */                                                              \
static inline bool udict_get_##type##_va(struct udict *udict, ctype *p,     \
                                         const char *format, ...)           \
{                                                                           \
    UBASE_VARARG(udict_get_##type(udict, p, string))                        \
}                                                                           \
/** @This sets the value of a type attribute, optionally creating it, with  \
 * printf-style name generation.                                            \
 *                                                                          \
 * @param udict pointer to the udict                                        \
 * @param value value to set                                                \
 * @param format printf-style format of the attribute, followed by a        \
 * variable list of arguments                                               \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool udict_set_##type##_va(struct udict *udict,               \
                                         ctype value,                       \
                                         const char *format, ...)           \
                   __attribute__ ((format(printf, 3, 4)));                  \
/** @hidden */                                                              \
static inline bool udict_set_##type##_va(struct udict *udict,               \
                                         ctype value,                       \
                                         const char *format, ...)           \
{                                                                           \
    UBASE_VARARG(udict_set_##type(udict, value, string))                    \
}

UDICT_TEMPLATE_TYPE1(string, const char *)
UDICT_TEMPLATE_TYPE1(void, void *)
UDICT_TEMPLATE_TYPE1(bool, bool)
UDICT_TEMPLATE_TYPE1(small_unsigned, uint8_t)
UDICT_TEMPLATE_TYPE1(small_int, int8_t)
UDICT_TEMPLATE_TYPE1(unsigned, uint64_t)
UDICT_TEMPLATE_TYPE1(int, int64_t)
UDICT_TEMPLATE_TYPE1(float, double)
UDICT_TEMPLATE_TYPE1(rational, struct urational)
#undef UDICT_TEMPLATE_TYPE1

/** @internal This template allows to quickly define type-specific delete
 * functions.
 *
 * @param type upipe attribute type name
 * @param ctype type of the attribute value in C
 * @param attrtype upipe attribute type enum
 */
#define UDICT_TEMPLATE_TYPE2(type, ctype, dicttype)                         \
/** @This deletes a type attribute.                                         \
 *                                                                          \
 * @param udict pointer to the udict                                        \
 * @param name name of the attribute                                        \
 * @return true if the attribute existed before                             \
 */                                                                         \
static inline bool udict_delete_##type(struct udict *udict,                 \
                                       const char *name)                    \
{                                                                           \
    return udict_delete(udict, name, dicttype);                             \
}                                                                           \
/** @This deletes a type attribute with printf-style name generation.       \
 *                                                                          \
 * @param udict pointer to the udict                                        \
 * @param format printf-style format of the attribute, followed by a        \
 * variable list of arguments                                               \
 * @return true if the attribute existed before                             \
 */                                                                         \
static inline bool udict_delete_##type##_va(struct udict *udict,            \
                                            const char *format, ...)        \
                   __attribute__ ((format(printf, 2, 3)));                  \
/** @hidden */                                                              \
static inline bool udict_delete_##type##_va(struct udict *udict,            \
                                            const char *format, ...)        \
{                                                                           \
    UBASE_VARARG(udict_delete_##type(udict, string))                        \
}

UDICT_TEMPLATE_TYPE2(opaque, uint8_t *, UDICT_TYPE_OPAQUE)
UDICT_TEMPLATE_TYPE2(string, const char *, UDICT_TYPE_STRING)
UDICT_TEMPLATE_TYPE2(void, void *, UDICT_TYPE_VOID)
UDICT_TEMPLATE_TYPE2(bool, bool, UDICT_TYPE_BOOL)
UDICT_TEMPLATE_TYPE2(small_unsigned, uint8_t, UDICT_TYPE_SMALL_UNSIGNED)
UDICT_TEMPLATE_TYPE2(small_int, int8_t, UDICT_TYPE_SMALL_INT)
UDICT_TEMPLATE_TYPE2(unsigned, uint64_t, UDICT_TYPE_UNSIGNED)
UDICT_TEMPLATE_TYPE2(int, int64_t, UDICT_TYPE_INT)
UDICT_TEMPLATE_TYPE2(float, double, UDICT_TYPE_FLOAT)
UDICT_TEMPLATE_TYPE2(rational, struct urational, UDICT_TYPE_RATIONAL)
#undef UDICT_TEMPLATE_TYPE2

/* @This allows to define accessors for a standard attribute.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param name string defining the attribute
 * @param type upipe attribute type name
 * @param ctype type of the attribute value in C
 * @param desc description of the attribute
 */
#define UDICT_TEMPLATE(group, attr, name, type, ctype, desc)                \
/** @This returns the desc attribute of a udict.                            \
 *                                                                          \
 * @param udict pointer to the udict                                        \
 * @param p pointer to the retrieved value (modified during execution)      \
 * @return true if the attribute was found, otherwise p is not modified     \
 */                                                                         \
static inline bool udict_##group##_get_##attr(struct udict *udict, ctype *p)\
{                                                                           \
    return udict_get_##type(udict, p, name);                                \
}                                                                           \
/** @This sets the desc attribute of a udict.                               \
 *                                                                          \
 * @param udict pointer to the udict                                        \
 * @param value value to set                                                \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool udict_##group##_set_##attr(struct udict *udict,          \
                                              ctype value)                  \
{                                                                           \
    return udict_set_##type(udict, value, name);                            \
}                                                                           \
/** @This deletes the desc attribute of a udict.                            \
 *                                                                          \
 * @param udict pointer to the udict                                        \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool udict_##group##_delete_##attr(struct udict *udict)       \
{                                                                           \
    return udict_delete_##type(udict, name);                                \
}

/* @This allows to define accessors for a standard void attribute.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param name string defining the attribute
 * @param desc description of the attribute
 */
#define UDICT_TEMPLATE_VOID(group, attr, name, desc)                        \
/** @This returns the presence of a desc attribute in a udict.              \
 *                                                                          \
 * @param udict pointer to the udict                                        \
 * @return true if the attribute was found, otherwise p is not modified     \
 */                                                                         \
static inline bool udict_##group##_get_##attr(struct udict *udict)          \
{                                                                           \
    return udict_get_void(udict, NULL, name);                               \
}                                                                           \
/** @This sets a desc attribute in a udict.                                 \
 *                                                                          \
 * @param udict pointer to the udict                                        \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool udict_##group##_set_##attr(struct udict *udict)          \
{                                                                           \
    return udict_set_void(udict, NULL, name);                               \
}                                                                           \
/** @This deletes a desc attribute from a udict.                            \
 *                                                                          \
 * @param udict pointer to the udict                                        \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool udict_##group##_delete_##attr(struct udict *udict)       \
{                                                                           \
    return udict_delete_void(udict, name);                                  \
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
#define UDICT_TEMPLATE_VA(group, attr, format, type, ctype, desc,           \
                          args_decl, args)                                  \
/** @This returns the desc attribute of a udict.                            \
 *                                                                          \
 * @param udict pointer to the udict                                        \
 * @param p pointer to the retrieved value (modified during execution)      \
 * @return true if the attribute was found, otherwise p is not modified     \
 */                                                                         \
static inline bool udict_##group##_get_##attr(struct udict *udict, ctype *p,\
                                              args_decl)                    \
{                                                                           \
    return udict_get_##type##_va(udict, p, format, args);                   \
}                                                                           \
/** @This sets the desc attribute of a udict.                               \
 *                                                                          \
 * @param udict pointer to the udict                                        \
 * @param value value to set                                                \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool udict_##group##_set_##attr(struct udict *udict,          \
                                              ctype value, args_decl)       \
{                                                                           \
    return udict_set_##type##_va(udict, value, format, args);               \
}                                                                           \
/** @This deletes the desc attribute of a udict.                            \
 *                                                                          \
 * @param udict pointer to the udict                                        \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool udict_##group##_delete_##attr(struct udict *udict,       \
                                                 args_decl)                 \
{                                                                           \
    return udict_delete_##type##_va(udict, format, args);                   \
}

/* @This allows to define accessors for a standard void attribute, with a name
 * depending on printf arguments.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param format printf-style format of the attribute
 * @param desc description of the attribute
 */
#define UDICT_TEMPLATE_VOID_VA(group, attr, name, desc, args_decl, args)    \
/** @This returns the presence of a desc attribute in a udict.              \
 *                                                                          \
 * @param udict pointer to the udict                                        \
 * @return true if the attribute was found, otherwise p is not modified     \
 */                                                                         \
static inline bool udict_##group##_get_##attr(struct udict *udict,          \
                                              args_decl)                    \
{                                                                           \
    return udict_get_void_va(udict, NULL, format, args);                    \
}                                                                           \
/** @This sets a desc attribute in a udict.                                 \
 *                                                                          \
 * @param udict pointer to the udict                                        \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool udict_##group##_set_##attr(struct udict *udict,          \
                                              args_decl)                    \
{                                                                           \
    return udict_set_void_va(udict, NULL, format, args);                    \
}                                                                           \
/** @This deletes a desc attribute from a udict.                            \
 *                                                                          \
 * @param udict pointer to the udict                                        \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool udict_##group##_delete_##attr(struct udict *udict,       \
                                                 args_decl)                 \
{                                                                           \
    return udict_delete_void_va(udict, format, args);                       \
}

/** @This frees a udict.
 *
 * @param udict structure to free
 */
static inline void udict_free(struct udict *udict)
{
    udict->mgr->udict_free(udict);
}

/** @This allocates a new udict with a given manager, and copies all attributes.
 *
 * @param mgr common management structure
 * @param udict source structure to clone
 * @return new udict or NULL in case of allocation failure
 */
static inline struct udict *udict_copy(struct udict_mgr *mgr,
                                       struct udict *udict)
{
    struct udict *new_udict = udict_alloc(mgr, 0);
    if (unlikely(new_udict == NULL))
        return NULL;

    const char *name = NULL;
    enum udict_type type;

    for ( ; ; ) {
        udict_iterate(udict, &name, &type);
        if (unlikely(name == NULL))
            break;

        size_t attr_size;
        const uint8_t *attr = udict_get(udict, name, type, &attr_size);
        if (unlikely(attr == NULL))
            goto udict_copy_err;

        uint8_t *new_attr = udict_set(new_udict, name, type, attr_size);
        if (unlikely(new_attr == NULL))
            goto udict_copy_err;

        memcpy(new_attr, attr, attr_size);
    }
    return new_udict;

udict_copy_err:
    udict_free(new_udict);
    return NULL;
}

/** @This instructs an existing udict manager to release all structures
 * currently kept in pools. It is intended as a debug tool only.
 *
 * @param mgr pointer to udict manager
 */
static inline void udict_mgr_vacuum(struct udict_mgr *mgr)
{
    if (likely(mgr->udict_mgr_vacuum != NULL))
        mgr->udict_mgr_vacuum(mgr);
}

/** @This increments the reference count of a udict manager.
 *
 * @param mgr pointer to udict manager
 */
static inline void udict_mgr_use(struct udict_mgr *mgr)
{
    if (likely(mgr->udict_mgr_use != NULL))
        mgr->udict_mgr_use(mgr);
}

/** @This decrements the reference count of a udict manager or frees it.
 *
 * @param mgr pointer to udict manager
 */
static inline void udict_mgr_release(struct udict_mgr *mgr)
{
    if (likely(mgr->udict_mgr_release != NULL))
        mgr->udict_mgr_release(mgr);
}

#endif
