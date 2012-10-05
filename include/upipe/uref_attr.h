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
 * @short Upipe uref attributes handling
 */

#ifndef _UPIPE_UREF_ATTR_H_
/** @hidden */
#define _UPIPE_UREF_ATTR_H_

#include <upipe/uref.h>
#include <upipe/udict.h>

/** @see udict_iterate */
static inline bool uref_attr_iterate(struct uref *uref, const char **name_p,
                                     enum udict_type *type_p)
{
    return udict_iterate(uref->udict, name_p, type_p);
}

/** @see udict_get_opaque */
static inline bool uref_attr_get_opaque(struct uref *uref, const uint8_t **p,
                                        size_t *size_p, const char *name)
{
    return udict_get_opaque(uref->udict, p, size_p, name);
}
/** @see udict_get_opaque_va */
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
/** @see udict_set_opaque */
static inline bool uref_attr_set_opaque(struct uref *uref,
                                        const uint8_t *value, size_t attr_size,
                                        const char *name)
{
    return udict_set_opaque(uref->udict, value, attr_size, name);
}
/** @see udict_set_opaque_va */
static inline bool uref_attr_set_opaque_va(struct uref *uref,
                                           const uint8_t *value,
                                           size_t attr_size,
                                           const char *format, ...)
                   __attribute__ ((format(printf, 4, 5)));
/** @hidden */
static inline bool uref_attr_set_opaque_va(struct uref *uref,
                                           const uint8_t *value,
                                           size_t attr_size,
                                           const char *format, ...)
{
    UBASE_VARARG(uref_attr_set_opaque(uref, value, attr_size, string))
}

/** @internal This template allows to quickly define uref attributes
 * functions mapped on udict.
 *
 * @param type upipe attribute type name
 * @param ctype type of the attribute value in C
 */
#define UREF_ATTR_TEMPLATE_TYPE1(type, ctype)                               \
/** @see udict_get_##type */                                                \
static inline bool uref_attr_get_##type(struct uref *uref, ctype *p,        \
                                        const char *name)                   \
{                                                                           \
    return udict_get_##type(uref->udict, p, name);                          \
}                                                                           \
/** @see udict_get_##type##_va */                                           \
static inline bool uref_attr_get_##type##_va(struct uref *uref, ctype *p,   \
                                             const char *format, ...)       \
                   __attribute__ ((format(printf, 3, 4)));                  \
/** @hidden */                                                              \
static inline bool uref_attr_get_##type##_va(struct uref *uref, ctype *p,   \
                                             const char *format, ...)       \
{                                                                           \
    UBASE_VARARG(uref_attr_get_##type(uref, p, string))                     \
}                                                                           \
/** @see udict_set_##type */                                                \
static inline bool uref_attr_set_##type(struct uref *uref, ctype value,     \
                                        const char *name)                   \
{                                                                           \
    return udict_set_##type(uref->udict, value, name);                      \
}                                                                           \
/** @see udict_set_##type##_va */                                           \
static inline bool uref_attr_set_##type##_va(struct uref *uref,             \
                                             ctype value,                   \
                                             const char *format, ...)       \
                   __attribute__ ((format(printf, 3, 4)));                  \
/** @hidden */                                                              \
static inline bool uref_attr_set_##type##_va(struct uref *uref,             \
                                             ctype value,                   \
                                             const char *format, ...)       \
{                                                                           \
    UBASE_VARARG(uref_attr_set_##type(uref, value, string))                 \
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

/** @internal This template allows to quickly define uref attributes
 * functions mapped on udict.
 *
 * @param type upipe attribute type name
 * @param ctype type of the attribute value in C
 */
#define UREF_ATTR_TEMPLATE_TYPE2(type, ctype)                               \
/** @see udict_delete_##type */                                             \
static inline bool uref_attr_delete_##type(struct uref *uref,               \
                                           const char *name)                \
{                                                                           \
    return udict_delete_##type(uref->udict, name);                          \
}                                                                           \
/** @see udict_delete_##type##_va */                                        \
static inline bool uref_attr_delete_##type##_va(struct uref *uref,          \
                                                const char *format, ...)    \
                   __attribute__ ((format(printf, 2, 3)));                  \
/** @hidden */                                                              \
static inline bool uref_attr_delete_##type##_va(struct uref *uref,          \
                                                const char *format, ...)    \
{                                                                           \
    UBASE_VARARG(uref_attr_delete_##type(uref, string))                     \
}

UREF_ATTR_TEMPLATE_TYPE2(opaque, uint8_t *)
UREF_ATTR_TEMPLATE_TYPE2(string, const char *)
UREF_ATTR_TEMPLATE_TYPE2(void, void *)
UREF_ATTR_TEMPLATE_TYPE2(bool, bool)
UREF_ATTR_TEMPLATE_TYPE2(small_unsigned, uint8_t)
UREF_ATTR_TEMPLATE_TYPE2(small_int, int8_t)
UREF_ATTR_TEMPLATE_TYPE2(unsigned, uint64_t)
UREF_ATTR_TEMPLATE_TYPE2(int, int64_t)
UREF_ATTR_TEMPLATE_TYPE2(float, double)
UREF_ATTR_TEMPLATE_TYPE2(rational, struct urational)
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
 * @param uref pointer to the uref                                          \
 * @param value value to set                                                \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_set_##attr(struct uref *uref,             \
                                             ctype value)                   \
{                                                                           \
    return uref_attr_set_##type(uref, value, name);                         \
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
 * @param uref pointer to the uref                                          \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_set_##attr(struct uref *uref)             \
{                                                                           \
    return uref_attr_set_void(uref, NULL, name);                            \
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
 * @param uref pointer to the uref                                          \
 * @param value value to set                                                \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_set_##attr(struct uref *uref,             \
                                             ctype value, args_decl)        \
{                                                                           \
    return uref_attr_set_##type##_va(uref, value, format, args);            \
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
static inline bool uref_##group##_set_##attr(struct uref *uref,             \
                                             args_decl)                     \
{                                                                           \
    return uref_attr_set_void_va(uref, NULL, format, args);                 \
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
