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
#include <upipe/urefcount.h>

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

    /* short-hand types */
    UDICT_TYPE_SHORTHAND = 0x10,
    /** f.random */
    UDICT_TYPE_FLOW_RANDOM,
    /** f.error */
    UDICT_TYPE_FLOW_ERROR,
    /** f.def */
    UDICT_TYPE_FLOW_DEF,
    /** f.rawdef */
    UDICT_TYPE_FLOW_RAWDEF,
    /** f.program */
    UDICT_TYPE_FLOW_PROGRAM,
    /** f.lang */
    UDICT_TYPE_FLOW_LANG,

    /** k.vbvdelay */
    UDICT_TYPE_CLOCK_VBVDELAY,
    /** k.duration */
    UDICT_TYPE_CLOCK_DURATION,

    /** b.end */
    UDICT_TYPE_BLOCK_END,

    /** p.num */
    UDICT_TYPE_PIC_NUM,
    /** p.hsize */
    UDICT_TYPE_PIC_HSIZE,
    /** p.vsize */
    UDICT_TYPE_PIC_VSIZE,
    /** p.hsizevis */
    UDICT_TYPE_PIC_HSIZE_VISIBLE,
    /** p.vsizevis */
    UDICT_TYPE_PIC_VSIZE_VISIBLE,
    /** p.hposition */
    UDICT_TYPE_PIC_HPOSITION,
    /** p.vposition */
    UDICT_TYPE_PIC_VPOSITION,
    /** p.aspect */
    UDICT_TYPE_PIC_ASPECT,
    /** p.overscan */
    UDICT_TYPE_PIC_OVERSCAN,
    /** p.progressive */
    UDICT_TYPE_PIC_PROGRESSIVE,
    /** p.tf */
    UDICT_TYPE_PIC_TF,
    /** p.bf */
    UDICT_TYPE_PIC_BF,
    /** p.tff */
    UDICT_TYPE_PIC_TFF
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
    /** name a shorthand attribute (enum udict_type, const char **,
     * enum udict_type *) */
    UDICT_NAME,

    /** non-standard commands implemented by a module type can start from
     * there (first arg = signature) */
    UDICT_CONTROL_LOCAL = 0x8000
};

/** @This is a structure describing an opaque. */
struct udict_opaque {
    const uint8_t *v;
    size_t size;
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
    /** refcount management structure */
    urefcount refcount;

    /** function to allocate a udict with a given initial size */
    struct udict *(*udict_alloc)(struct udict_mgr *, size_t);
    /** control function for standard or local commands */
    bool (*udict_control)(struct udict *, enum udict_command, va_list);
    /** function to free a udict */
    void (*udict_free)(struct udict *);

    /** function to release all buffers kept in pools */
    void (*udict_mgr_vacuum)(struct udict_mgr *);
    /** function to free the udict manager */
    void (*udict_mgr_free)(struct udict_mgr *);
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
 * execution to the name of the next attribute, or NULL if it is a shorthand
 * @param type_p reference to the type of the attribute, changed to
 * UDICT_TYPE_END at the end of the iteration; start with UDICT_TYPE_END as well
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
 * @param name name of the attribute (NULL if type is a shorthand)
 * @param type type of the attribute (potentially a shorthand)
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

/** @internal @This returns the value of an opaque attribute, potentially
 * shorthand.
 *
 * @param udict pointer to the udict
 * @param p pointer to the retrieved value (modified during execution)
 * @param type type of the attribute (potentially a shorthand)
 * @param name name of the attribute
 * @return true if the attribute was found, otherwise p is not modified
 */
static inline bool udict_get_opaque(struct udict *udict,
                                    struct udict_opaque *p,
                                    enum udict_type type,
                                    const char *name)
{
    const uint8_t *attr = udict_get(udict, name, type, &p->size);
    if (unlikely(attr == NULL))
        return false;
    p->v = attr;
    return true;
}

/** @This returns the value of a string attribute.
 *
 * @param udict pointer to the udict
 * @param p pointer to the retrieved value (modified during execution)
 * @param type type of the attribute (potentially a shorthand)
 * @param name name of the attribute
 * @return true if the attribute was found, otherwise p is not modified
 */
static inline bool udict_get_string(struct udict *udict, const char **p,
                                    enum udict_type type, const char *name)
{
    size_t size;
    const uint8_t *attr = udict_get(udict, name, type, &size);
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
 * @param type type of the attribute (potentially a shorthand)
 * @param name name of the attribute
 * @return true if the attribute was found
 */
static inline bool udict_get_void(struct udict *udict, void *p,
                                  enum udict_type type, const char *name)
{
    const uint8_t *attr = udict_get(udict, name, type, NULL);
    return (attr != NULL);
}

/** @This returns the value of a bool attribute.
 *
 * @param udict pointer to the udict
 * @param p pointer to the retrieved value (modified during execution)
 * @param type type of the attribute (potentially a shorthand)
 * @param name name of the attribute
 * @return true if the attribute was found, otherwise p is not modified
 */
static inline bool udict_get_bool(struct udict *udict, bool *p,
                                  enum udict_type type, const char *name)
{
    size_t size;
    const uint8_t *attr = udict_get(udict, name, type, &size);
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
 * @param type type of the attribute (potentially a shorthand)
 * @param name name of the attribute
 * @return true if the attribute was found, otherwise p is not modified
 */
static inline bool udict_get_small_unsigned(struct udict *udict, uint8_t *p,
                                            enum udict_type type,
                                            const char *name)
{
    size_t size;
    const uint8_t *attr = udict_get(udict, name, type, &size);
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
 * @param type type of the attribute (potentially a shorthand)
 * @param name name of the attribute
 * @return true if the attribute was found, otherwise p is not modified
 */
static inline bool udict_get_small_int(struct udict *udict, int8_t *p,
                                       enum udict_type type, const char *name)
{
    size_t size;
    const uint8_t *attr = udict_get(udict, name, type, &size);
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
 * @param type type of the attribute (potentially a shorthand)
 * @param name name of the attribute
 * @return true if the attribute was found, otherwise p is not modified
 */
static inline bool udict_get_unsigned(struct udict *udict, uint64_t *p,
                                      enum udict_type type, const char *name)
{
    size_t size;
    const uint8_t *attr = udict_get(udict, name, type, &size);
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
 * @param type type of the attribute (potentially a shorthand)
 * @param name name of the attribute
 * @return true if the attribute was found, otherwise p is not modified
 */
static inline bool udict_get_int(struct udict *udict, int64_t *p,
                                 enum udict_type type, const char *name)
{
    size_t size;
    const uint8_t *attr = udict_get(udict, name, type, &size);
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
 * @param type type of the attribute (potentially a shorthand)
 * @param name name of the attribute
 * @return true if the attribute was found, otherwise p is not modified
 */
static inline bool udict_get_float(struct udict *udict, double *p,
                                   enum udict_type type, const char *name)
{
    /* FIXME: this is probably not portable */
    union {
        double f;
        uint64_t i;
    } u;
    size_t size;
    const uint8_t *attr = udict_get(udict, name, type, &size);
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
 * @param type type of the attribute (potentially a shorthand)
 * @param name name of the attribute
 * @return true if the attribute was found, otherwise p is not modified
 */
static inline bool udict_get_rational(struct udict *udict, struct urational *p,
                                      enum udict_type type, const char *name)
{
    size_t size;
    const uint8_t *attr = udict_get(udict, name, type, &size);
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

/** @This sets the value of an opaque attribute, optionally creating it.
 *
 * @param udict the pointer to the udict
 * @param value value to set
 * @param type type of the attribute (potentially a shorthand)
 * @param name name of the attribute
 * @return true if no allocation failure occurred
 */
static inline bool udict_set_opaque(struct udict *udict,
                                    struct udict_opaque value,
                                    enum udict_type type, const char *name)
{
    /* copy the opaque it case in points to us */
    uint8_t v[value.size];
    memcpy(v, value.v, value.size);
    uint8_t *attr = udict_set(udict, name, type, value.size);
    if (unlikely(attr == NULL))
        return false;
    memcpy(attr, v, value.size);
    return true;
}

/** @This sets the value of a string attribute, optionally creating it.
 *
 * @param udict the pointer to the udict
 * @param value value to set
 * @param type type of the attribute (potentially a shorthand)
 * @param name name of the attribute
 * @return true if no allocation failure occurred
 */
static inline bool udict_set_string(struct udict *udict, const char *value,
                                    enum udict_type type, const char *name)
{
    size_t attr_size = strlen(value) + 1;
    /* copy the string it case in points to us */
    uint8_t v[attr_size];
    memcpy(v, value, attr_size);
    uint8_t *attr = udict_set(udict, name, type, attr_size);
    if (unlikely(attr == NULL))
        return false;
    memcpy(attr, v, attr_size);
    return true;
}

/** @This sets the value of a void attribute, optionally creating it.
 *
 * @param udict the pointer to the udict
 * @param value actually unused, but kept for API consistency (should be NULL)
 * @param type type of the attribute (potentially a shorthand)
 * @param name name of the attribute
 * @return true if no allocation failure occurred
 */
static inline bool udict_set_void(struct udict *udict, void *value,
                                  enum udict_type type, const char *name)
{
    uint8_t *attr = udict_set(udict, name, type, 0);
    return (attr != NULL);
}

/** @This sets the value of a bool attribute, optionally creating it.
 *
 * @param udict the pointer to the udict
 * @param value value to set
 * @param type type of the attribute (potentially a shorthand)
 * @param name name of the attribute
 * @return true if no allocation failure occurred
 */
static inline bool udict_set_bool(struct udict *udict, bool value,
                                  enum udict_type type, const char *name)
{
    uint8_t *attr = udict_set(udict, name, type, 1);
    if (unlikely(attr == NULL))
        return false;
    *attr = value ? 1 : 0;
    return true;
}

/** @This sets the value of a small unsigned attribute, optionally creating it.
 *
 * @param udict the pointer to the udict
 * @param value value to set
 * @param type type of the attribute (potentially a shorthand)
 * @param name name of the attribute
 * @return true if no allocation failure occurred
 */
static inline bool udict_set_small_unsigned(struct udict *udict,
                                            uint8_t value, enum udict_type type,
                                            const char *name)
{
    uint8_t *attr = udict_set(udict, name, type, 1);
    if (unlikely(attr == NULL))
        return false;
    *attr = value;
    return true;
}

/** @This sets the value of a small int attribute, optionally creating it.
 *
 * @param udict the pointer to the udict
 * @param value value to set
 * @param type type of the attribute (potentially a shorthand)
 * @param name name of the attribute
 * @return true if no allocation failure occurred
 */
static inline bool udict_set_small_int(struct udict *udict, int8_t value,
                                       enum udict_type type, const char *name)
{
    uint8_t *attr = udict_set(udict, name, type, 1);
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
 * @param type type of the attribute (potentially a shorthand)
 * @param name name of the attribute
 * @return true if no allocation failure occurred
 */
static inline bool udict_set_unsigned(struct udict *udict, uint64_t value,
                                      enum udict_type type, const char *name)
{
    uint8_t *attr = udict_set(udict, name, type, 8);
    if (unlikely(attr == NULL))
        return false;
    udict_set_uint64(attr, value);
    return true;
}

/** @This sets the value of an int attribute, optionally creating it.
 *
 * @param udict the pointer to the udict
 * @param value value to set
 * @param type type of the attribute (potentially a shorthand)
 * @param name name of the attribute
 * @return true if no allocation failure occurred
 */
static inline bool udict_set_int(struct udict *udict, uint64_t value,
                                 enum udict_type type, const char *name)
{
    uint8_t *attr = udict_set(udict, name, type, 8);
    if (unlikely(attr == NULL))
        return false;
    udict_set_int64(attr, value);
    return true;
}

/** @This sets the value of a float attribute, optionally creating it.
 *
 * @param udict the pointer to the udict
 * @param value value to set
 * @param type type of the attribute (potentially a shorthand)
 * @param name name of the attribute
 * @return true if no allocation failure occurred
 */
static inline bool udict_set_float(struct udict *udict, double value,
                                   enum udict_type type, const char *name)
{
    /* FIXME: this is probably not portable */
    union {
        double f;
        uint64_t i;
    } u;
    uint8_t *attr = udict_set(udict, name, type, 8);
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
 * @param type type of the attribute (potentially a shorthand)
 * @param name name of the attribute
 * @return true if no allocation failure occurred
 */
static inline bool udict_set_rational(struct udict *udict,
                                      struct urational value,
                                      enum udict_type type, const char *name)
{
    uint8_t *attr = udict_set(udict, name, type, 16);
    if (unlikely(attr == NULL))
        return false;
    udict_set_int64(attr, value.num);
    udict_set_uint64(attr + 8, value.den);
    return true;
}

/** @This deletes an attribute.
 *
 * @param udict pointer to the udict
 * @param type type of the attribute (potentially a shorthand)
 * @param name name of the attribute
 * @return true if the attribute existed before
 */
static inline bool udict_delete(struct udict *udict, enum udict_type type,
                                const char *name)
{
    return udict_control(udict, UDICT_DELETE, name, type);
}

/** @This names a shorthand attribute.
 *
 * @param udict pointer to the udict
 * @param type shorthand type
 * @param name_p filled in with the name of the shorthand attribute
 * @param base_type_p filled in with the base type of the shorthand attribute
 * @return false in case the shorthand doesn't exist
 */
static inline bool udict_name(struct udict *udict, enum udict_type type,
                              const char **name_p, enum udict_type *base_type_p)
{
    return udict_control(udict, UDICT_NAME, type, name_p, base_type_p);
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
    enum udict_type type = UDICT_TYPE_END;

    for ( ; ; ) {
        udict_iterate(udict, &name, &type);
        if (unlikely(type == UDICT_TYPE_END))
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
    urefcount_use(&mgr->refcount);
}

/** @This decrements the reference count of a udict manager or frees it.
 *
 * @param mgr pointer to udict manager
 */
static inline void udict_mgr_release(struct udict_mgr *mgr)
{
    if (unlikely(urefcount_release(&mgr->refcount)))
        mgr->udict_mgr_free(mgr);
}

#endif
