/*
 * Copyright (C) 2012-2014 OpenHeadend S.A.R.L.
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
 * @short Upipe uref attributes handling
 */

#ifndef _UPIPE_UREF_ATTR_H_
/** @hidden */
#define _UPIPE_UREF_ATTR_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uref.h>
#include <upipe/udict.h>

/** @This imports all attributes from a uref into another uref (see also
 * @ref udict_import).
 *
 * @param uref overwritten uref
 * @param uref_attr uref containing attributes to fetch
 * @return an error code
 */
static inline int uref_attr_import(struct uref *uref, struct uref *uref_attr)
{
    if (uref_attr->udict == NULL)
        return UBASE_ERR_NONE;
    if (uref->udict == NULL) {
        uref->udict = udict_dup(uref_attr->udict);
        return uref->udict != NULL ? UBASE_ERR_NONE : UBASE_ERR_INVALID;
    }
    return udict_import(uref->udict, uref_attr->udict);
}

/** @This copies multiple attributes from an uref to another.
 *
 * @param uref pointer to the uref
 * @param uref_src pointer to the source uref
 * @param list array of copy function to apply
 * @param list_size size of the array
 * @return an error code
 */
static inline int uref_attr_copy_list(struct uref *uref, struct uref *uref_src,
                                      int (*list[])(struct uref *uref,
                                                    struct uref *uref_src),
                                      size_t list_size)
{
    int err = UBASE_ERR_NONE;
    for (size_t i = 0; ubase_check(err) && i < list_size; i++)
        err = list[i](uref, uref_src);
    return err;
}

/** @This deletes an attribute.
 *
 * @param uref pointer to the uref
 * @param type type of the attribute (potentially a shorthand)
 * @param name name of the attribute
 * @return an error code
 */
static inline int uref_attr_delete(struct uref *uref, enum udict_type type,
                                   const char *name)
{
    if (uref->udict == NULL)
        return UBASE_ERR_INVALID;
    return udict_delete(uref->udict, type, name);
}

/** @This deletes an attribute, with printf-style name generation.
 *
 * @param uref pointer to the uref
 * @param type type of the attribute (potentially a shorthand)
 * @param format printf-style format of the attribute, followed by a
 * variable list of arguments
 * @return an error code
 */
static inline int uref_attr_delete_va(struct uref *uref, enum udict_type type,
                                      const char *format, ...)
                   __attribute__ ((format(printf, 3, 4)));
/** @hidden */
static inline int uref_attr_delete_va(struct uref *uref, enum udict_type type,
                                      const char *format, ...)
{
    UBASE_VARARG(uref_attr_delete(uref, type, string))
}

/** @This deletes multiple attributes.
 *
 * @param uref pointer to the uref
 * @param list array of delete function to apply
 * @param list_size size of the array
 * @return an error code
 */
static inline int uref_attr_delete_list(struct uref *uref,
                                        int (*list[])(struct uref *uref),
                                        size_t list_size)
{
    int err = UBASE_ERR_NONE;
    for (size_t i = 0; ubase_check(err) && i < list_size; i++)
        err = list[i](uref);
    return err;
}

#define UREF_ATTR_TEMPLATE(utype, ctype)                                    \
/** @This returns the value of a utype attribute.                           \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param p pointer to the retrieved value (modified during execution)      \
 * @param type type of the attribute (potentially a shorthand)              \
 * @param name name of the attribute                                        \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_attr_get_##utype(struct uref *uref,                  \
        ctype *p, enum udict_type type, const char *name)                   \
{                                                                           \
    if (uref->udict == NULL)                                                \
        return UBASE_ERR_INVALID;                                           \
    return udict_get_##utype(uref->udict, p, type, name);                   \
}                                                                           \
/** @This returns the value of a utype attribute, with printf-style name    \
 * generation.                                                              \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param p pointer to the retrieved value (modified during execution)      \
 * @param type type of the attribute (potentially a shorthand)              \
 * @param format printf-style format of the attribute, followed by a        \
 * variable list of arguments                                               \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_attr_get_##utype##_va(struct uref *uref,             \
        ctype *p, enum udict_type type, const char *format, ...)            \
{                                                                           \
    UBASE_VARARG(uref_attr_get_##utype(uref, p, type, string))              \
}                                                                           \
/** @This sets the value of a utype attribute, optionally creating it.      \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @param type type of the attribute (potentially a shorthand)              \
 * @param name name of the attribute                                        \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_attr_set_##utype(struct uref *uref,                  \
        ctype v, enum udict_type type, const char *name)                    \
{                                                                           \
    if (uref->udict == NULL) {                                              \
        uref->udict = udict_alloc(uref->mgr->udict_mgr, 0);                 \
        if (unlikely(uref->udict == NULL))                                  \
            return UBASE_ERR_ALLOC;                                         \
    }                                                                       \
    return udict_set_##utype(uref->udict, v, type, name);                   \
}                                                                           \
/** @This sets the value of a utype attribute, optionally creating it, with \
 * printf-style name generation.                                            \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @param type type of the attribute (potentially a shorthand)              \
 * @param format printf-style format of the attribute, followed by a        \
 * variable list of arguments                                               \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_attr_set_##utype##_va(struct uref *uref,             \
        ctype v, enum udict_type type, const char *format, ...)             \
{                                                                           \
    UBASE_VARARG(uref_attr_set_##utype(uref, v, type, string))              \
}                                                                           \
/** @This copies the desc attribute from an uref to another.                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param uref_src pointer to the source uref                               \
 * @param type type of the attribute (potentially a shorthand)              \
 * @param name name of the attribute                                        \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_attr_copy_##utype(struct uref *uref,                 \
                                           struct uref *uref_src,           \
                                           enum udict_type type,            \
                                           const char *name)                \
{                                                                           \
    uref_attr_delete(uref, type, name);                                     \
    ctype v;                                                                \
    int err = uref_attr_get_##utype(uref_src, &v, type, name);              \
    if (ubase_check(err))                                                   \
        return uref_attr_set_##utype(uref, v, type, name);                  \
    return UBASE_ERR_NONE;                                                  \
}                                                                           \
/** @This copies the desc attribute of an uref to another, with             \
 * printf-style name generation.                                            \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param uref_src pointer to the source uref                               \
 * @param type type of the attribute (potentially a shorthand)              \
 * @param format printf-style format of the attribute, followed by a        \
 * variable list of arguments                                               \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_attr_copy_##utype##_va(struct uref *uref,            \
                                                struct uref *uref_src,      \
                                                enum udict_type type,       \
                                                const char *format, ...)    \
{                                                                           \
    UBASE_VARARG(uref_attr_copy_##utype(uref, uref_src, type, string))      \
}

UREF_ATTR_TEMPLATE(opaque, struct udict_opaque)
UREF_ATTR_TEMPLATE(string, const char *)
UREF_ATTR_TEMPLATE(void, void *)
UREF_ATTR_TEMPLATE(bool, bool)
UREF_ATTR_TEMPLATE(small_unsigned, uint8_t)
UREF_ATTR_TEMPLATE(small_int, int8_t)
UREF_ATTR_TEMPLATE(unsigned, uint64_t)
UREF_ATTR_TEMPLATE(int, int64_t)
UREF_ATTR_TEMPLATE(float, double)
UREF_ATTR_TEMPLATE(rational, struct urational)
#undef UREF_ATTR_TEMPLATE

/*
 * Opaque attributes
 */

/* @This allows to define accessors for a opaque attribute.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param name opaque defining the attribute
 * @param desc description of the attribute
 */
#define UREF_ATTR_OPAQUE(group, attr, name, desc)                           \
/** @This returns the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param p pointer to the retrieved value (modified during execution)      \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_get_##attr(struct uref *uref,              \
                                            const uint8_t **p,              \
                                            size_t *size_p)                 \
{                                                                           \
    struct udict_opaque opaque;                                             \
    int err = uref_attr_get_opaque(uref, &opaque, UDICT_TYPE_OPAQUE, name); \
    if (ubase_check(err)) {                                                 \
        *p = opaque.v;                                                      \
        *size_p = opaque.size;                                              \
    }                                                                       \
    return err;                                                             \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_set_##attr(struct uref *uref,              \
                                            const uint8_t *v, size_t size)  \
{                                                                           \
    struct udict_opaque opaque;                                             \
    opaque.v = v;                                                           \
    opaque.size = size;                                                     \
    return uref_attr_set_opaque(uref, opaque, UDICT_TYPE_OPAQUE, name);     \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_delete_##attr(struct uref *uref)           \
{                                                                           \
    return uref_attr_delete(uref, UDICT_TYPE_OPAQUE, name);                 \
}                                                                           \
/** @This copies the desc attribute from an uref to another.                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param uref_src pointer to the source uref                               \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_copy_##attr(struct uref *uref,             \
                                             struct uref *uref_src)         \
{                                                                           \
    return uref_attr_copy_opaque(uref, uref_src,                            \
                                 UDICT_TYPE_OPAQUE, name);                  \
}

/* @This allows to define accessors for a shorthand opaque attribute.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param type shorthand type
 * @param desc description of the attribute
 */
#define UREF_ATTR_OPAQUE_SH(group, attr, type, desc)                        \
/** @This returns the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param p pointer to the retrieved value (modified during execution)      \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_get_##attr(struct uref *uref,              \
                                            const uint8_t **p,              \
                                            size_t *size_p)                 \
{                                                                           \
    struct udict_opaque opaque;                                             \
    int err = uref_attr_get_opaque(uref, &opaque, type, NULL);              \
    if (ubase_check(err)) {                                                 \
        *p = opaque.v;                                                      \
        *size_p = opaque.size;                                              \
    }                                                                       \
    return err;                                                             \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_set_##attr(struct uref *uref,              \
                                            const uint8_t *v, size_t size)  \
{                                                                           \
    struct udict_opaque opaque;                                             \
    opaque.v = v;                                                           \
    opaque.size = size;                                                     \
    return uref_attr_set_opaque(uref, opaque, type, NULL);                  \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_delete_##attr(struct uref *uref)           \
{                                                                           \
    return uref_attr_delete(uref, type, NULL);                              \
}                                                                           \
/** @This copies the desc attribute from an uref to another.                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param uref_src pointer to the source uref                               \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_copy_##attr(struct uref *uref,             \
                                             struct uref *uref_src)         \
{                                                                           \
    return uref_attr_copy_opaque(uref, uref_src, type, NULL);               \
}

/* @This allows to define accessors for a opaque attribute, with a name
 * depending on printf arguments.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param format printf-style format of the attribute
 * @param desc description of the attribute
 */
#define UREF_ATTR_OPAQUE_VA(group, attr, format, desc, args_decl, args)     \
/** @This returns the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param p pointer to the retrieved value (modified during execution)      \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_get_##attr(struct uref *uref,              \
                                            const uint8_t **p,              \
                                            size_t *size_p, args_decl)      \
{                                                                           \
    struct udict_opaque opaque;                                             \
    int err = uref_attr_get_opaque_va(uref, &opaque, UDICT_TYPE_OPAQUE,     \
                                      format, args);                        \
    if (ubase_check(err)) {                                                 \
        *p = opaque.v;                                                      \
        *size_p = opaque.size;                                              \
    }                                                                       \
    return err;                                                             \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_set_##attr(struct uref *uref,              \
                                            const uint8_t *v, size_t size,  \
                                            args_decl)                      \
{                                                                           \
    struct udict_opaque opaque;                                             \
    opaque.v = v;                                                           \
    opaque.size = size;                                                     \
    return uref_attr_set_opaque_va(uref, opaque, UDICT_TYPE_OPAQUE,         \
                                   format, args);                           \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_delete_##attr(struct uref *uref, args_decl)\
{                                                                           \
    return uref_attr_delete_va(uref, UDICT_TYPE_OPAQUE, format, args);      \
}                                                                           \
/** @This copies the desc attribute from an uref to another.                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param uref_src pointer to the source uref                               \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_copy_##attr(struct uref *uref,             \
                                             struct uref *uref_src,         \
                                             args_decl)                     \
{                                                                           \
    return uref_attr_copy_opaque_va(uref, uref_src, UDICT_TYPE_OPAQUE,      \
                                    format, args);                          \
}


/*
 * String attributes
 */

/* @This allows to define accessors for a string attribute.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param name string defining the attribute
 * @param desc description of the attribute
 */
#define UREF_ATTR_STRING(group, attr, name, desc)                           \
/** @This returns the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param p pointer to the retrieved value (modified during execution)      \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_get_##attr(struct uref *uref,              \
                                            const char **p)                 \
{                                                                           \
    return uref_attr_get_string(uref, p, UDICT_TYPE_STRING, name);          \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_set_##attr(struct uref *uref,              \
                                            const char *v)                  \
{                                                                           \
    return uref_attr_set_string(uref, v, UDICT_TYPE_STRING, name);          \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_delete_##attr(struct uref *uref)           \
{                                                                           \
    return uref_attr_delete(uref, UDICT_TYPE_STRING, name);                 \
}                                                                           \
/** @This copies the desc attribute from an uref to another.                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param uref_src pointer to the source uref                               \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_copy_##attr(struct uref *uref,             \
                                             struct uref *uref_src)         \
{                                                                           \
    return uref_attr_copy_string(uref, uref_src, UDICT_TYPE_STRING, name);  \
}                                                                           \
/** @This compares the desc attribute to a given prefix.                    \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param prefix prefix to match                                            \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_match_##attr(struct uref *uref,            \
                                              const char *prefix)           \
{                                                                           \
    const char *v;                                                          \
    UBASE_RETURN(uref_##group##_get_##attr(uref, &v));                      \
    return !ubase_ncmp(v, prefix) ? UBASE_ERR_NONE : UBASE_ERR_INVALID;     \
}                                                                           \
/** @This compares the desc attribute in two urefs.                         \
 *                                                                          \
 * @param uref1 pointer to the first uref                                   \
 * @param uref2 pointer to the second uref                                  \
 * @return 0 if both attributes are absent or identical                     \
 */                                                                         \
static inline int uref_##group##_cmp_##attr(struct uref *uref1,             \
                                            struct uref *uref2)             \
{                                                                           \
    const char *v1 = NULL, *v2 = NULL;                                      \
    int err1 = uref_##group##_get_##attr(uref1, &v1);                       \
    int err2 = uref_##group##_get_##attr(uref2, &v2);                       \
    if (!ubase_check(err1) && !ubase_check(err2))                           \
        return 0;                                                           \
    if (!ubase_check(err1) || !ubase_check(err2))                           \
        return -1;                                                          \
    return strcmp(v1, v2);                                                  \
}

/* @This allows to define accessors for a shorthand string attribute.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param type shorthand type
 * @param desc description of the attribute
 */
#define UREF_ATTR_STRING_SH(group, attr, type, desc)                        \
/** @This returns the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param p pointer to the retrieved value (modified during execution)      \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_get_##attr(struct uref *uref,              \
                                            const char **p)                 \
{                                                                           \
    return uref_attr_get_string(uref, p, type, NULL);                       \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_set_##attr(struct uref *uref,              \
                                            const char *v)                  \
{                                                                           \
    return uref_attr_set_string(uref, v, type, NULL);                       \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_delete_##attr(struct uref *uref)           \
{                                                                           \
    return uref_attr_delete(uref, type, NULL);                              \
}                                                                           \
/** @This copies the desc attribute from an uref to another.                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param uref_src pointer to the source uref                               \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_copy_##attr(struct uref *uref,             \
                                             struct uref *uref_src)         \
{                                                                           \
    return uref_attr_copy_string(uref, uref_src, type, NULL);               \
}                                                                           \
/** @This compares the desc attribute to a given prefix.                    \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param prefix prefix to match                                            \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_match_##attr(struct uref *uref,            \
                                              const char *prefix)           \
{                                                                           \
    const char *v;                                                          \
    UBASE_RETURN(uref_##group##_get_##attr(uref, &v));                      \
    return !ubase_ncmp(v, prefix) ? UBASE_ERR_NONE : UBASE_ERR_INVALID;     \
}                                                                           \
/** @This compares the desc attribute in two urefs.                         \
 *                                                                          \
 * @param uref1 pointer to the first uref                                   \
 * @param uref2 pointer to the second uref                                  \
 * @return 0 if both attributes are absent or identical                     \
 */                                                                         \
static inline int uref_##group##_cmp_##attr(struct uref *uref1,             \
                                            struct uref *uref2)             \
{                                                                           \
    const char *v1 = NULL, *v2 = NULL;                                      \
    int err1 = uref_##group##_get_##attr(uref1, &v1);                       \
    int err2 = uref_##group##_get_##attr(uref2, &v2);                       \
    if (!ubase_check(err1) && !ubase_check(err2))                           \
        return 0;                                                           \
    if (!ubase_check(err1) || !ubase_check(err2))                           \
        return -1;                                                          \
    return strcmp(v1, v2);                                                  \
}

/* @This allows to define accessors for a string attribute, with a name
 * depending on printf arguments.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param format printf-style format of the attribute
 * @param desc description of the attribute
 */
#define UREF_ATTR_STRING_VA(group, attr, format, desc, args_decl, args)     \
/** @This returns the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param p pointer to the retrieved value (modified during execution)      \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_get_##attr(struct uref *uref,              \
                                            const char **p, args_decl)      \
{                                                                           \
    return uref_attr_get_string_va(uref, p, UDICT_TYPE_STRING,              \
                                   format, args);                           \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_set_##attr(struct uref *uref,              \
                                            const char *v, args_decl)       \
{                                                                           \
    return uref_attr_set_string_va(uref, v, UDICT_TYPE_STRING,              \
                                   format, args);                           \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_delete_##attr(struct uref *uref,           \
                                               args_decl)                   \
{                                                                           \
    return uref_attr_delete_va(uref, UDICT_TYPE_STRING, format, args);      \
}                                                                           \
/** @This copies the desc attribute from an uref to another.                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param uref_src pointer to the source uref                               \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_copy_##attr(struct uref *uref,             \
                                             struct uref *uref_src,         \
                                             args_decl)                     \
{                                                                           \
    return uref_attr_copy_string_va(uref, uref_src, UDICT_TYPE_STRING,      \
                                    format, args);                          \
}                                                                           \
/** @This compares the desc attribute to a given prefix.                    \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param prefix prefix to match                                            \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_match_##attr(struct uref *uref,            \
                                              const char *prefix, args_decl)\
{                                                                           \
    const char *v;                                                          \
    UBASE_RETURN(uref_##group##_get_##attr(uref, &v, args));                \
    return !ubase_ncmp(v, prefix) ? UBASE_ERR_NONE : UBASE_ERR_INVALID;     \
}                                                                           \
/** @This compares the desc attribute in two urefs.                         \
 *                                                                          \
 * @param uref1 pointer to the first uref                                   \
 * @param uref2 pointer to the second uref                                  \
 * @return 0 if both attributes are absent or identical                     \
 */                                                                         \
static inline int uref_##group##_cmp_##attr(struct uref *uref1,             \
                                            struct uref *uref2, args_decl)  \
{                                                                           \
    const char *v1 = NULL, *v2 = NULL;                                      \
    int err1 = uref_##group##_get_##attr(uref1, &v1, args);                 \
    int err2 = uref_##group##_get_##attr(uref2, &v2, args);                 \
    if (!ubase_check(err1) && !ubase_check(err2))                           \
        return 0;                                                           \
    if (!ubase_check(err1) || !ubase_check(err2))                           \
        return -1;                                                          \
    return strcmp(v1, v2);                                                  \
}


/*
 * Void attributes
 */

/* @This allows to define accessors for a void attribute.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param name string defining the attribute
 * @param desc description of the attribute
 */
#define UREF_ATTR_VOID(group, attr, name, desc)                             \
/** @This returns the presence of a desc attribute in a uref.               \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_get_##attr(struct uref *uref)              \
{                                                                           \
    return uref_attr_get_void(uref, NULL, UDICT_TYPE_VOID, name);           \
}                                                                           \
/** @This sets a desc attribute in a uref.                                  \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_set_##attr(struct uref *uref)              \
{                                                                           \
    return uref_attr_set_void(uref, NULL, UDICT_TYPE_VOID, name);           \
}                                                                           \
/** @This deletes a desc attribute from a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_delete_##attr(struct uref *uref)           \
{                                                                           \
    return uref_attr_delete(uref, UDICT_TYPE_VOID, name);                   \
}                                                                           \
/** @This copies the desc attribute from an uref to another.                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param uref_src pointer to the source uref                               \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_copy_##attr(struct uref *uref,             \
                                             struct uref *uref_src)         \
{                                                                           \
    return uref_attr_copy_void(uref, uref_src, UDICT_TYPE_VOID, name);      \
}                                                                           \
/** @This compares the desc attribute in two urefs.                         \
 *                                                                          \
 * @param uref1 pointer to the first uref                                   \
 * @param uref2 pointer to the second uref                                  \
 * @return 0 if both attributes are absent or identical                     \
 */                                                                         \
static inline int uref_##group##_cmp_##attr(struct uref *uref1,             \
                                            struct uref *uref2)             \
{                                                                           \
    int err1 = uref_##group##_get_##attr(uref1);                            \
    int err2 = uref_##group##_get_##attr(uref2);                            \
    if (!ubase_check(err1) && !ubase_check(err2))                           \
        return 0;                                                           \
    if (!ubase_check(err1) || !ubase_check(err2))                           \
        return -1;                                                          \
    return 0;                                                               \
}

/* @This allows to define accessors for a shorthand void attribute.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param type shorthand type
 * @param desc description of the attribute
 */
#define UREF_ATTR_VOID_SH(group, attr, type, desc)                          \
/** @This returns the presence of a desc attribute in a uref.               \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_get_##attr(struct uref *uref)              \
{                                                                           \
    return uref_attr_get_void(uref, NULL, type, NULL);                      \
}                                                                           \
/** @This sets a desc attribute in a uref.                                  \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_set_##attr(struct uref *uref)              \
{                                                                           \
    return uref_attr_set_void(uref, NULL, type, NULL);                      \
}                                                                           \
/** @This deletes a desc attribute from a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_delete_##attr(struct uref *uref)           \
{                                                                           \
    return uref_attr_delete(uref, type, NULL);                              \
}                                                                           \
/** @This copies the desc attribute from an uref to another.                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param uref_src pointer to the source uref                               \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_copy_##attr(struct uref *uref,             \
                                             struct uref *uref_src)         \
{                                                                           \
    return uref_attr_copy_void(uref, uref_src, type, NULL);                 \
}                                                                           \
/** @This compares the desc attribute in two urefs.                         \
 *                                                                          \
 * @param uref1 pointer to the first uref                                   \
 * @param uref2 pointer to the second uref                                  \
 * @return 0 if both attributes are absent or identical                     \
 */                                                                         \
static inline int uref_##group##_cmp_##attr(struct uref *uref1,             \
                                            struct uref *uref2)             \
{                                                                           \
    int err1 = uref_##group##_get_##attr(uref1);                            \
    int err2 = uref_##group##_get_##attr(uref2);                            \
    if (!ubase_check(err1) && !ubase_check(err2))                           \
        return 0;                                                           \
    if (!ubase_check(err1) || !ubase_check(err2))                           \
        return -1;                                                          \
    return 0;                                                               \
}

/* @This allows to define accessors for a void attribute, with a name
 * depending on printf arguments.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param format printf-style format of the attribute
 * @param desc description of the attribute
 */
#define UREF_ATTR_VOID_VA(group, attr, format, desc, args_decl, args)       \
/** @This returns the presence of a desc attribute in a uref.               \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_get_##attr(struct uref *uref, args_decl)   \
{                                                                           \
    return uref_attr_get_void_va(uref, NULL, UDICT_TYPE_VOID,               \
                                 format, args);                             \
}                                                                           \
/** @This sets a desc attribute in a uref.                                  \
 *                                                                          \
 * @param uref_p reference to the pointer to the uref (possibly modified)   \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_set_##attr(struct uref *uref, args_decl)   \
{                                                                           \
    return uref_attr_set_void_va(uref, NULL, UDICT_TYPE_VOID,               \
                                 format, args);                             \
}                                                                           \
/** @This deletes a desc attribute from a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_delete_##attr(struct uref *uref, args_decl)\
{                                                                           \
    return uref_attr_delete_va(uref, UDICT_TYPE_VOID, format, args);        \
}                                                                           \
/** @This copies the desc attribute from an uref to another.                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param uref_src pointer to the source uref                               \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_copy_##attr(struct uref *uref,             \
                                             struct uref *uref_src,         \
                                             args_decl)                     \
{                                                                           \
    return uref_attr_copy_void_va(uref, uref_src, UDICT_TYPE_VOID,          \
                                  format, args);                            \
}                                                                           \
/** @This compares the desc attribute in two urefs.                         \
 *                                                                          \
 * @param uref1 pointer to the first uref                                   \
 * @param uref2 pointer to the second uref                                  \
 * @return 0 if both attributes are absent or identical                     \
 */                                                                         \
static inline int uref_##group##_cmp_##attr(struct uref *uref1,             \
                                            struct uref *uref2, args_decl)  \
{                                                                           \
    int err1 = uref_##group##_get_##attr(uref1, args);                      \
    int err2 = uref_##group##_get_##attr(uref2, args);                      \
    if (!ubase_check(err1) && !ubase_check(err2))                           \
        return 0;                                                           \
    if (!ubase_check(err1) || !ubase_check(err2))                           \
        return -1;                                                          \
    return 0;                                                               \
}

/* @This allows to define accessors for a void attribute directly in the uref
 * structure.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param flag name of the flag in @ref uref_flag
 * @param desc description of the attribute
 */
#define UREF_ATTR_VOID_UREF(group, attr, flag, desc)                        \
/** @This returns the presence of a desc attribute in a uref.               \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_get_##attr(struct uref *uref)              \
{                                                                           \
    return (uref->flags & flag) ? UBASE_ERR_NONE : UBASE_ERR_INVALID;       \
}                                                                           \
/** @This sets a desc attribute in a uref.                                  \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 */                                                                         \
static inline void uref_##group##_set_##attr(struct uref *uref)             \
{                                                                           \
    uref->flags |= flag;                                                    \
}                                                                           \
/** @This deletes a desc attribute from a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 */                                                                         \
static inline void uref_##group##_delete_##attr(struct uref *uref)          \
{                                                                           \
    uref->flags &= ~(uint64_t)flag;                                         \
}                                                                           \
/** @This copies the desc attribute from an uref to another.                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param uref_src pointer to the source uref                               \
 * @return an error code                                                    \
 */                                                                         \
static inline void uref_##group##_copy_##attr(struct uref *uref,            \
                                              struct uref *uref_src)        \
{                                                                           \
    uref_##group##_delete_##attr(uref);                                     \
    if (ubase_check(uref_##group##_get_##attr(uref_src)))                   \
        uref_##group##_set_##attr(uref);                                    \
}


/*
 * Small unsigned attributes
 */

/* @This allows to define accessors for a small_unsigned attribute.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param name string defining the attribute
 * @param desc description of the attribute
 */
#define UREF_ATTR_SMALL_UNSIGNED(group, attr, name, desc)                   \
/** @This returns the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param p pointer to the retrieved value (modified during execution)      \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_get_##attr(struct uref *uref, uint8_t *p)  \
{                                                                           \
    return uref_attr_get_small_unsigned(uref, p,                            \
                                        UDICT_TYPE_SMALL_UNSIGNED, name);   \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_set_##attr(struct uref *uref, uint8_t v)   \
{                                                                           \
    return uref_attr_set_small_unsigned(uref, v,                            \
                                        UDICT_TYPE_SMALL_UNSIGNED, name);   \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_delete_##attr(struct uref *uref)           \
{                                                                           \
    return uref_attr_delete(uref, UDICT_TYPE_SMALL_UNSIGNED, name);         \
}                                                                           \
/** @This copies the desc attribute from an uref to another.                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param uref_src pointer to the source uref                               \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_copy_##attr(struct uref *uref,             \
                                             struct uref *uref_src)         \
{                                                                           \
    return uref_attr_copy_small_unsigned(uref, uref_src,                    \
                                         UDICT_TYPE_SMALL_UNSIGNED, name);  \
}                                                                           \
/** @This compares the desc attribute to given values.                      \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param min minimum value                                                 \
 * @param max maximum value                                                 \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_match_##attr(struct uref *uref,            \
                                              uint8_t min, uint8_t max)     \
{                                                                           \
    uint8_t v;                                                              \
    UBASE_RETURN(uref_##group##_get_##attr(uref, &v));                      \
    return (v >= min) && (v <= max) ? UBASE_ERR_NONE : UBASE_ERR_INVALID;   \
}                                                                           \
/** @This compares the desc attribute in two urefs.                         \
 *                                                                          \
 * @param uref1 pointer to the first uref                                   \
 * @param uref2 pointer to the second uref                                  \
 * @return 0 if both attributes are absent or identical                     \
 */                                                                         \
static inline int uref_##group##_cmp_##attr(struct uref *uref1,             \
                                            struct uref *uref2)             \
{                                                                           \
    uint8_t v1 = 0, v2 = 0;                                                 \
    int err1 = uref_##group##_get_##attr(uref1, &v1);                       \
    int err2 = uref_##group##_get_##attr(uref2, &v2);                       \
    if (!ubase_check(err1) && !ubase_check(err2))                           \
        return 0;                                                           \
    if (!ubase_check(err1) || !ubase_check(err2))                           \
        return -1;                                                          \
    return v1 - v2;                                                         \
}

/* @This allows to define accessors for a shorthand small_unsigned attribute.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param type shorthand type
 * @param desc description of the attribute
 */
#define UREF_ATTR_SMALL_UNSIGNED_SH(group, attr, type, desc)                \
/** @This returns the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param p pointer to the retrieved value (modified during execution)      \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_get_##attr(struct uref *uref, uint8_t *p)  \
{                                                                           \
    return uref_attr_get_small_unsigned(uref, p, type, NULL);               \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_set_##attr(struct uref *uref, uint8_t v)   \
{                                                                           \
    return uref_attr_set_small_unsigned(uref, v, type, NULL);               \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_delete_##attr(struct uref *uref)           \
{                                                                           \
    return uref_attr_delete(uref, type, NULL);                              \
}                                                                           \
/** @This copies the desc attribute from an uref to another.                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param uref_src pointer to the source uref                               \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_copy_##attr(struct uref *uref,             \
                                             struct uref *uref_src)         \
{                                                                           \
    return uref_attr_copy_small_unsigned(uref, uref_src, type, NULL);       \
}                                                                           \
/** @This compares the desc attribute to given values.                      \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param min minimum value                                                 \
 * @param max maximum value                                                 \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_match_##attr(struct uref *uref,            \
                                              uint8_t min, uint8_t max)     \
{                                                                           \
    uint8_t v;                                                              \
    UBASE_RETURN(uref_##group##_get_##attr(uref, &v));                      \
    return (v >= min) && (v <= max) ? UBASE_ERR_NONE : UBASE_ERR_INVALID;   \
}                                                                           \
/** @This compares the desc attribute in two urefs.                         \
 *                                                                          \
 * @param uref1 pointer to the first uref                                   \
 * @param uref2 pointer to the second uref                                  \
 * @return 0 if both attributes are absent or identical                     \
 */                                                                         \
static inline int uref_##group##_cmp_##attr(struct uref *uref1,             \
                                            struct uref *uref2)             \
{                                                                           \
    uint8_t v1 = 0, v2 = 0;                                                 \
    int err1 = uref_##group##_get_##attr(uref1, &v1);                       \
    int err2 = uref_##group##_get_##attr(uref2, &v2);                       \
    if (!ubase_check(err1) && !ubase_check(err2))                           \
        return 0;                                                           \
    if (!ubase_check(err1) || !ubase_check(err2))                           \
        return -1;                                                          \
    return v1 - v2;                                                         \
}
 

/* @This allows to define accessors for a small_unsigned attribute, with a name
 * depending on printf arguments.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param format printf-style format of the attribute
 * @param desc description of the attribute
 */
#define UREF_ATTR_SMALL_UNSIGNED_VA(group, attr, format, desc, args_decl,   \
                                    args)                                   \
/** @This returns the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param p pointer to the retrieved value (modified during execution)      \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_get_##attr(struct uref *uref, uint8_t *p,  \
                                            args_decl)                      \
{                                                                           \
    return uref_attr_get_small_unsigned_va(uref, p,                         \
                                           UDICT_TYPE_SMALL_UNSIGNED,       \
                                           format, args);                   \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_set_##attr(struct uref *uref, uint8_t v,   \
                                            args_decl)                      \
{                                                                           \
    return uref_attr_set_small_unsigned_va(uref, v,                         \
                                           UDICT_TYPE_SMALL_UNSIGNED,       \
                                           format, args);                   \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_delete_##attr(struct uref *uref, args_decl)\
{                                                                           \
    return uref_attr_delete_va(uref, UDICT_TYPE_SMALL_UNSIGNED,             \
                               format, args);                               \
}                                                                           \
/** @This copies the desc attribute from an uref to another.                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param uref_src pointer to the source uref                               \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_copy_##attr(struct uref *uref,             \
                                             struct uref *uref_src,         \
                                             args_decl)                     \
{                                                                           \
    return uref_attr_copy_small_unsigned_va(uref, uref_src,                 \
                                            UDICT_TYPE_SMALL_UNSIGNED,      \
                                            format, args);                  \
}                                                                           \
/** @This compares the desc attribute to given values.                      \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param min minimum value                                                 \
 * @param max maximum value                                                 \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_match_##attr(struct uref *uref,            \
                                              uint8_t min, uint8_t max,     \
                                              args_decl)                    \
{                                                                           \
    uint8_t v;                                                              \
    UBASE_RETURN(uref_##group##_get_##attr(uref, &v, args));                \
    return (v >= min) && (v <= max) ? UBASE_ERR_NONE : UBASE_ERR_INVALID;   \
}                                                                           \
/** @This compares the desc attribute in two urefs.                         \
 *                                                                          \
 * @param uref1 pointer to the first uref                                   \
 * @param uref2 pointer to the second uref                                  \
 * @return 0 if both attributes are absent or identical                     \
 */                                                                         \
static inline int uref_##group##_cmp_##attr(struct uref *uref1,             \
                                            struct uref *uref2, args_decl)  \
{                                                                           \
    uint8_t v1 = 0, v2 = 0;                                                 \
    int err1 = uref_##group##_get_##attr(uref1, &v1, args);                 \
    int err2 = uref_##group##_get_##attr(uref2, &v2, args);                 \
    if (!ubase_check(err1) && !ubase_check(err2))                           \
        return 0;                                                           \
    if (!ubase_check(err1) || !ubase_check(err2))                           \
        return -1;                                                          \
    return v1 - v2;                                                         \
}

/*
 * Unsigned attributes
 */

/* @This allows to define accessors for an unsigned attribute.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param name string defining the attribute
 * @param desc description of the attribute
 */
#define UREF_ATTR_UNSIGNED(group, attr, name, desc)                         \
/** @This returns the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param p pointer to the retrieved value (modified during execution)      \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_get_##attr(struct uref *uref, uint64_t *p) \
{                                                                           \
    return uref_attr_get_unsigned(uref, p, UDICT_TYPE_UNSIGNED, name);      \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_set_##attr(struct uref *uref, uint64_t v)  \
{                                                                           \
    return uref_attr_set_unsigned(uref, v, UDICT_TYPE_UNSIGNED, name);      \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_delete_##attr(struct uref *uref)           \
{                                                                           \
    return uref_attr_delete(uref, UDICT_TYPE_UNSIGNED, name);               \
}                                                                           \
/** @This copies the desc attribute from an uref to another.                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param uref_src pointer to the source uref                               \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_copy_##attr(struct uref *uref,             \
                                             struct uref *uref_src)         \
{                                                                           \
    return uref_attr_copy_unsigned(uref, uref_src, UDICT_TYPE_UNSIGNED,     \
                                   name);                                   \
}                                                                           \
/** @This compares the desc attribute to given values.                      \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param min minimum value                                                 \
 * @param max maximum value                                                 \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_match_##attr(struct uref *uref,            \
                                              uint64_t min, uint64_t max)   \
{                                                                           \
    uint64_t v;                                                             \
    UBASE_RETURN(uref_##group##_get_##attr(uref, &v));                      \
    return (v >= min) && (v <= max) ? UBASE_ERR_NONE : UBASE_ERR_INVALID;   \
}                                                                           \
/** @This compares the desc attribute in two urefs.                         \
 *                                                                          \
 * @param uref1 pointer to the first uref                                   \
 * @param uref2 pointer to the second uref                                  \
 * @return 0 if both attributes are absent or identical                     \
 */                                                                         \
static inline int uref_##group##_cmp_##attr(struct uref *uref1,             \
                                            struct uref *uref2)             \
{                                                                           \
    uint64_t v1 = 0, v2 = 0;                                                \
    int err1 = uref_##group##_get_##attr(uref1, &v1);                       \
    int err2 = uref_##group##_get_##attr(uref2, &v2);                       \
    if (!ubase_check(err1) && !ubase_check(err2))                           \
        return 0;                                                           \
    if (!ubase_check(err1) || !ubase_check(err2))                           \
        return -1;                                                          \
    return v1 - v2;                                                         \
}


/* @This allows to define accessors for a shorthand unsigned attribute.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param type shorthand type
 * @param desc description of the attribute
 */
#define UREF_ATTR_UNSIGNED_SH(group, attr, type, desc)                      \
/** @This returns the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param p pointer to the retrieved value (modified during execution)      \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_get_##attr(struct uref *uref, uint64_t *p) \
{                                                                           \
    return uref_attr_get_unsigned(uref, p, type, NULL);                     \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_set_##attr(struct uref *uref, uint64_t v)  \
{                                                                           \
    return uref_attr_set_unsigned(uref, v, type, NULL);                     \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_delete_##attr(struct uref *uref)           \
{                                                                           \
    return uref_attr_delete(uref, type, NULL);                              \
}                                                                           \
/** @This copies the desc attribute from an uref to another.                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param uref_src pointer to the source uref                               \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_copy_##attr(struct uref *uref,             \
                                             struct uref *uref_src)         \
{                                                                           \
    return uref_attr_copy_unsigned(uref, uref_src, type, NULL);             \
}                                                                           \
/** @This compares the desc attribute to given values.                      \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param min minimum value                                                 \
 * @param max maximum value                                                 \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_match_##attr(struct uref *uref,            \
                                              uint64_t min, uint64_t max)   \
{                                                                           \
    uint64_t v;                                                             \
    UBASE_RETURN(uref_##group##_get_##attr(uref, &v));                      \
    return (v >= min) && (v <= max) ? UBASE_ERR_NONE : UBASE_ERR_INVALID;   \
}                                                                           \
/** @This compares the desc attribute in two urefs.                         \
 *                                                                          \
 * @param uref1 pointer to the first uref                                   \
 * @param uref2 pointer to the second uref                                  \
 * @return 0 if both attributes are absent or identical                     \
 */                                                                         \
static inline int uref_##group##_cmp_##attr(struct uref *uref1,             \
                                            struct uref *uref2)             \
{                                                                           \
    uint64_t v1 = 0, v2 = 0;                                                \
    int err1 = uref_##group##_get_##attr(uref1, &v1);                       \
    int err2 = uref_##group##_get_##attr(uref2, &v2);                       \
    if (!ubase_check(err1) && !ubase_check(err2))                           \
        return 0;                                                           \
    if (!ubase_check(err1) || !ubase_check(err2))                           \
        return -1;                                                          \
    return v1 - v2;                                                         \
}


/* @This allows to define accessors for an unsigned attribute, with a name
 * depending on printf arguments.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param format printf-style format of the attribute
 * @param desc description of the attribute
 */
#define UREF_ATTR_UNSIGNED_VA(group, attr, format, desc, args_decl, args)   \
/** @This returns the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param p pointer to the retrieved value (modified during execution)      \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_get_##attr(struct uref *uref, uint64_t *p, \
                                            args_decl)                      \
{                                                                           \
    return uref_attr_get_unsigned_va(uref, p, UDICT_TYPE_UNSIGNED,          \
                                     format, args);                         \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_set_##attr(struct uref *uref, uint64_t v,  \
                                            args_decl)                      \
{                                                                           \
    return uref_attr_set_unsigned_va(uref, v, UDICT_TYPE_UNSIGNED,          \
                                     format, args);                         \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_delete_##attr(struct uref *uref, args_decl)\
{                                                                           \
    return uref_attr_delete_va(uref, UDICT_TYPE_UNSIGNED, format, args);    \
}                                                                           \
/** @This copies the desc attribute from an uref to another.                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param uref_src pointer to the source uref                               \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_copy_##attr(struct uref *uref,             \
                                             struct uref *uref_src,         \
                                             args_decl)                     \
{                                                                           \
    return uref_attr_copy_unsigned_va(uref, uref_src, UDICT_TYPE_UNSIGNED,  \
                                      format, args);                        \
}                                                                           \
/** @This compares the desc attribute to given values.                      \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param min minimum value                                                 \
 * @param max maximum value                                                 \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_match_##attr(struct uref *uref,            \
                                              uint8_t min, uint8_t max,     \
                                              args_decl)                    \
{                                                                           \
    uint64_t v;                                                             \
    UBASE_RETURN(uref_##group##_get_##attr(uref, &v, args));                \
    return (v >= min) && (v <= max) ? UBASE_ERR_NONE : UBASE_ERR_INVALID;   \
}                                                                           \
/** @This compares the desc attribute in two urefs.                         \
 *                                                                          \
 * @param uref1 pointer to the first uref                                   \
 * @param uref2 pointer to the second uref                                  \
 * @return 0 if both attributes are absent or identical                     \
 */                                                                         \
static inline int uref_##group##_cmp_##attr(struct uref *uref1,             \
                                            struct uref *uref2, args_decl)  \
{                                                                           \
    uint64_t v1 = 0, v2 = 0;                                                \
    int err1 = uref_##group##_get_##attr(uref1, &v1, args);                 \
    int err2 = uref_##group##_get_##attr(uref2, &v2, args);                 \
    if (!ubase_check(err1) && !ubase_check(err2))                           \
        return 0;                                                           \
    if (!ubase_check(err1) || !ubase_check(err2))                           \
        return -1;                                                          \
    return v1 - v2;                                                         \
}

/* @This allows to define accessors for an unsigned attribute directly in the
 * uref structure.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param member name of the member in uref structure
 * @param desc description of the attribute
 */
#define UREF_ATTR_UNSIGNED_UREF(group, attr, member, desc)                  \
/** @This returns the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param p pointer to the retrieved value (modified during execution)      \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_get_##attr(struct uref *uref, uint64_t *p) \
{                                                                           \
    if (uref->member != UINT64_MAX) {                                       \
        *p = uref->member;                                                  \
        return UBASE_ERR_NONE;                                              \
    }                                                                       \
    return UBASE_ERR_INVALID;                                               \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 */                                                                         \
static inline void uref_##group##_set_##attr(struct uref *uref,             \
                                             uint64_t v)                    \
{                                                                           \
    uref->member = v;                                                       \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 */                                                                         \
static inline void uref_##group##_delete_##attr(struct uref *uref)          \
{                                                                           \
    uref->member = UINT64_MAX;                                              \
}                                                                           \
/** @This copies the desc attribute from an uref to another.                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param uref_src pointer to the source uref                               \
 * @return an error code                                                    \
 */                                                                         \
static inline void uref_##group##_copy_##attr(struct uref *uref,            \
                                              struct uref *uref_src)        \
{                                                                           \
    uint64_t v;                                                             \
    uref_##group##_delete_##attr(uref);                                     \
    if (ubase_check(uref_##group##_get_##attr(uref_src, &v)))               \
        uref_##group##_set_##attr(uref, v);                                 \
}                                                                           \
/** @This compares the desc attribute to given values.                      \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param min minimum value                                                 \
 * @param max maximum value                                                 \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_match_##attr(struct uref *uref,            \
                                              uint8_t min, uint8_t max)     \
{                                                                           \
    uint64_t v;                                                             \
    UBASE_RETURN(uref_##group##_get_##attr(uref, &v));                      \
    return (v >= min) && (v <= max) ? UBASE_ERR_NONE : UBASE_ERR_INVALID;   \
}                                                                           \
/** @This compares the desc attribute in two urefs.                         \
 *                                                                          \
 * @param uref1 pointer to the first uref                                   \
 * @param uref2 pointer to the second uref                                  \
 * @return 0 if both attributes are absent or identical                     \
 */                                                                         \
static inline int uref_##group##_cmp_##attr(struct uref *uref1,             \
                                            struct uref *uref2)             \
{                                                                           \
    uint64_t v1 = 0, v2 = 0;                                                \
    int err1 = uref_##group##_get_##attr(uref1, &v1);                       \
    int err2 = uref_##group##_get_##attr(uref2, &v2);                       \
    if (!ubase_check(err1) && !ubase_check(err2))                           \
        return 0;                                                           \
    if (!ubase_check(err1) || !ubase_check(err2))                           \
        return -1;                                                          \
    return v1 - v2;                                                         \
}



/*
 * Int attributes
 */

/* @This allows to define accessors for a int attribute.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param name string defining the attribute
 * @param desc description of the attribute
 */
#define UREF_ATTR_INT(group, attr, name, desc)                              \
/** @This returns the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param p pointer to the retrieved value (modified during execution)      \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_get_##attr(struct uref *uref, int64_t *p)  \
{                                                                           \
    return uref_attr_get_int(uref, p, UDICT_TYPE_INT, name);                \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_set_##attr(struct uref *uref, int64_t v)   \
{                                                                           \
    return uref_attr_set_int(uref, v, UDICT_TYPE_INT, name);                \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_delete_##attr(struct uref *uref)           \
{                                                                           \
    return uref_attr_delete(uref, UDICT_TYPE_INT, name);                    \
}                                                                           \
/** @This copies the desc attribute from an uref to another.                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param uref_src pointer to the source uref                               \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_copy_##attr(struct uref *uref,             \
                                             struct uref *uref_src)         \
{                                                                           \
    return uref_attr_copy_int(uref, uref_src, UDICT_TYPE_INT, name);        \
}                                                                           \
/** @This compares the desc attribute in two urefs.                         \
 *                                                                          \
 * @param uref1 pointer to the first uref                                   \
 * @param uref2 pointer to the second uref                                  \
 * @return 0 if both attributes are absent or identical                     \
 */                                                                         \
static inline int uref_##group##_cmp_##attr(struct uref *uref1,             \
                                            struct uref *uref2)             \
{                                                                           \
    int64_t v1 = 0, v2 = 0;                                                 \
    int err1 = uref_##group##_get_##attr(uref1, &v1);                       \
    int err2 = uref_##group##_get_##attr(uref2, &v2);                       \
    if (!ubase_check(err1) && !ubase_check(err2))                           \
        return 0;                                                           \
    if (!ubase_check(err1) || !ubase_check(err2))                           \
        return -1;                                                          \
    return v1 - v2;                                                         \
}

/* @This allows to define accessors for a shorthand int attribute.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param type shorthand type
 * @param desc description of the attribute
 */
#define UREF_ATTR_INT_SH(group, attr, type, desc)                           \
/** @This returns the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param p pointer to the retrieved value (modified during execution)      \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_get_##attr(struct uref *uref, int64_t *p)  \
{                                                                           \
    return uref_attr_get_int(uref, p, type, NULL);                          \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_set_##attr(struct uref *uref, int64_t v)   \
{                                                                           \
    return uref_attr_set_int(uref, v, type, NULL);                          \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_delete_##attr(struct uref *uref)           \
{                                                                           \
    return uref_attr_delete(uref, type, NULL);                              \
}                                                                           \
/** @This copies the desc attribute from an uref to another.                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param uref_src pointer to the source uref                               \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_copy_##attr(struct uref *uref,             \
                                             struct uref *uref_src)         \
{                                                                           \
    return uref_attr_copy_int(uref, uref_src, type, NULL);                  \
}

/* @This allows to define accessors for a int attribute, with a name
 * depending on printf arguments.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param format printf-style format of the attribute
 * @param desc description of the attribute
 */
#define UREF_ATTR_INT_VA(group, attr, format, desc, args_decl, args)        \
/** @This returns the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param p pointer to the retrieved value (modified during execution)      \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_get_##attr(struct uref *uref, int64_t *p,  \
                                            args_decl)                      \
{                                                                           \
    return uref_attr_get_int_va(uref, p, UDICT_TYPE_INT, format, args);     \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_set_##attr(struct uref *uref, int64_t v,   \
                                            args_decl)                      \
{                                                                           \
    return uref_attr_set_int_va(uref, v, UDICT_TYPE_INT, format, args);     \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_delete_##attr(struct uref *uref, args_decl)\
{                                                                           \
    return uref_attr_delete_va(uref, UDICT_TYPE_INT, format, args);         \
}                                                                           \
/** @This copies the desc attribute from an uref to another.                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param uref_src pointer to the source uref                               \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_copy_##attr(struct uref *uref,             \
                                             struct uref *uref_src,         \
                                             args_decl)                     \
{                                                                           \
    return uref_attr_copy_int_va(uref, uref_src, UDICT_TYPE_INT, format,    \
                                 args);                                     \
}

/*
 * Float (double) attributes
 */

/* @This allows to define accessors for a float attribute.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param name string defining the attribute
 * @param desc description of the attribute
 */
#define UREF_ATTR_FLOAT(group, attr, name, desc)                            \
/** @This returns the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param p pointer to the retrieved value (modified during execution)      \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_get_##attr(struct uref *uref, double *p)   \
{                                                                           \
    return uref_attr_get_float(uref, p, UDICT_TYPE_FLOAT, name);              \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_set_##attr(struct uref *uref, double v)    \
{                                                                           \
    return uref_attr_set_float(uref, v, UDICT_TYPE_FLOAT, name);              \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_delete_##attr(struct uref *uref)           \
{                                                                           \
    return uref_attr_delete(uref, UDICT_TYPE_FLOAT, name);                  \
}                                                                           \
/** @This copies the desc attribute from an uref to another.                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param uref_src pointer to the source uref                               \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_copy_##attr(struct uref *uref,             \
                                             struct uref *uref_src)         \
{                                                                           \
    return uref_attr_copy_float(uref, uref_src, UDICT_TYPE_FLOAT, name);    \
}                                                                           \
/** @This compares the desc attribute in two urefs.                         \
 *                                                                          \
 * @param uref1 pointer to the first uref                                   \
 * @param uref2 pointer to the second uref                                  \
 * @return 0 if both attributes are absent or identical                     \
 */                                                                         \
static inline int uref_##group##_cmp_##attr(struct uref *uref1,             \
                                            struct uref *uref2)             \
{                                                                           \
    double v1 = 0, v2 = 0;                                                  \
    int err1 = uref_##group##_get_##attr(uref1, &v1);                       \
    int err2 = uref_##group##_get_##attr(uref2, &v2);                       \
    if (!ubase_check(err1) && !ubase_check(err2))                           \
        return 0;                                                           \
    if (!ubase_check(err1) || !ubase_check(err2))                           \
        return -1;                                                          \
    return v1 - v2;                                                         \
}

/* @This allows to define accessors for a shorthand int attribute.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param type shorthand type
 * @param desc description of the attribute
 */
#define UREF_ATTR_FLOAT_SH(group, attr, type, desc)                         \
/** @This returns the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param p pointer to the retrieved value (modified during execution)      \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_get_##attr(struct uref *uref, double *p)   \
{                                                                           \
    return uref_attr_get_float(uref, p, type, NULL);                          \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_set_##attr(struct uref *uref, double v)    \
{                                                                           \
    return uref_attr_set_float(uref, v, type, NULL);                          \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_delete_##attr(struct uref *uref)           \
{                                                                           \
    return uref_attr_delete(uref, type, NULL);                              \
}                                                                           \
/** @This copies the desc attribute from an uref to another.                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param uref_src pointer to the source uref                               \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_copy_##attr(struct uref *uref,             \
                                             struct uref *uref_src)         \
{                                                                           \
    return uref_attr_copy_float(uref, uref_src, type, NULL);                \
}

/* @This allows to define accessors for a int attribute, with a name
 * depending on printf arguments.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param format printf-style format of the attribute
 * @param desc description of the attribute
 */
#define UREF_ATTR_FLOAT_VA(group, attr, format, desc, args_decl, args)      \
/** @This returns the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param p pointer to the retrieved value (modified during execution)      \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_get_##attr(struct uref *uref, double *p,   \
                                            args_decl)                      \
{                                                                           \
    return uref_attr_get_float_va(uref, p, UDICT_TYPE_FLOAT, format, args);   \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_set_##attr(struct uref *uref, double v,    \
                                            args_decl)                      \
{                                                                           \
    return uref_attr_set_float_va(uref, v, UDICT_TYPE_FLOAT, format, args);   \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_delete_##attr(struct uref *uref, args_decl)\
{                                                                           \
    return uref_attr_delete_va(uref, UDICT_TYPE_FLOAT, format, args);       \
}                                                                           \
/** @This copies the desc attribute from an uref to another.                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param uref_src pointer to the source uref                               \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_copy_##attr(struct uref *uref,             \
                                             struct uref *uref_src,         \
                                             args_decl)                     \
{                                                                           \
    return uref_attr_copy_float_va(uref, uref_src, UDICT_TYPE_FLOAT,        \
                                   format, args);                           \
}


/*
 * Rational attributes
 */

/* @This allows to define accessors for a rational attribute.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param name string defining the attribute
 * @param desc description of the attribute
 */
#define UREF_ATTR_RATIONAL(group, attr, name, desc)                         \
/** @This returns the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param p pointer to the retrieved value (modified during execution)      \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_get_##attr(struct uref *uref,              \
                                            struct urational *p)            \
{                                                                           \
    return uref_attr_get_rational(uref, p, UDICT_TYPE_RATIONAL, name);      \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_set_##attr(struct uref *uref,              \
                                            struct urational v)             \
{                                                                           \
    return uref_attr_set_rational(uref, v, UDICT_TYPE_RATIONAL, name);      \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_delete_##attr(struct uref *uref)           \
{                                                                           \
    return uref_attr_delete(uref, UDICT_TYPE_RATIONAL, name);               \
}                                                                           \
/** @This copies the desc attribute from an uref to another.                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param uref_src pointer to the source uref                               \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_copy_##attr(struct uref *uref,             \
                                             struct uref *uref_src)         \
{                                                                           \
    return uref_attr_copy_rational(uref, uref_src, UDICT_TYPE_RATIONAL,     \
                                   name);                                   \
}

/* @This allows to define accessors for a shorthand rational attribute.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param type shorthand type
 * @param desc description of the attribute
 */
#define UREF_ATTR_RATIONAL_SH(group, attr, type, desc)                      \
/** @This returns the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param p pointer to the retrieved value (modified during execution)      \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_get_##attr(struct uref *uref,              \
                                            struct urational *p)            \
{                                                                           \
    return uref_attr_get_rational(uref, p, type, NULL);                     \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_set_##attr(struct uref *uref,              \
                                            struct urational v)             \
{                                                                           \
    return uref_attr_set_rational(uref, v, type, NULL);                     \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_delete_##attr(struct uref *uref)           \
{                                                                           \
    return uref_attr_delete(uref, type, NULL);                              \
}                                                                           \
/** @This copies the desc attribute from an uref to another.                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param uref_src pointer to the source uref                               \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_copy_##attr(struct uref *uref,             \
                                             struct uref *uref_src)         \
{                                                                           \
    return uref_attr_copy_rational(uref, uref_src, type, NULL);             \
}

/* @This allows to define accessors for a rational attribute, with a name
 * depending on prrationalf arguments.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param format prrationalf-style format of the attribute
 * @param desc description of the attribute
 */
#define UREF_ATTR_RATIONAL_VA(group, attr, format, desc, args_decl, args)   \
/** @This returns the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param p pointer to the retrieved value (modified during execution)      \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_get_##attr(struct uref *uref,              \
                                            struct urational *p, args_decl) \
{                                                                           \
    return uref_attr_get_rational_va(uref, p, UDICT_TYPE_RATIONAL,          \
                                   format, args);                           \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_set_##attr(struct uref *uref,              \
                                            struct urational v, args_decl)  \
{                                                                           \
    return uref_attr_set_rational_va(uref, v, UDICT_TYPE_RATIONAL,          \
                                   format, args);                           \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_delete_##attr(struct uref *uref, args_decl)\
{                                                                           \
    return uref_attr_delete_va(uref, UDICT_TYPE_RATIONAL, format, args);    \
}                                                                           \
/** @This copies the desc attribute from an uref to another.                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param uref_src pointer to the source uref                               \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_##group##_copy_##attr(struct uref *uref,             \
                                             struct uref *uref_src,         \
                                             args_decl)                     \
{                                                                           \
    return uref_attr_copy_rational_va(uref, uref_src, UDICT_TYPE_RATIONAL,  \
                                      format, args);                        \
}

UREF_ATTR_UNSIGNED_UREF(attr, priv, priv, private (internal pipe use))

#ifdef __cplusplus
}
#endif
#endif
