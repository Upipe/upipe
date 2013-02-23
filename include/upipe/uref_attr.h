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
 * @return true if the attribute was found, otherwise p is not modified     \
 */                                                                         \
static inline bool uref_##group##_get_##attr(struct uref *uref,             \
                                             const uint8_t **p,             \
                                             size_t *size_p)                \
{                                                                           \
    return udict_get_opaque(uref->udict, p, size_p, UDICT_TYPE_OPAQUE,      \
                            name);                                          \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_set_##attr(struct uref *uref,             \
                                             const uint8_t *v, size_t size) \
{                                                                           \
    return udict_set_opaque(uref->udict, v, size, UDICT_TYPE_OPAQUE, name); \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_delete_##attr(struct uref *uref)          \
{                                                                           \
    return udict_delete(uref->udict, UDICT_TYPE_OPAQUE, name);              \
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
 * @return true if the attribute was found, otherwise p is not modified     \
 */                                                                         \
static inline bool uref_##group##_get_##attr(struct uref *uref,             \
                                             const uint8_t **p,             \
                                             size_t *size_p)                \
{                                                                           \
    return udict_get_opaque(uref->udict, p, size_p, type, NULL);            \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_set_##attr(struct uref *uref,             \
                                             const uint8_t *v, size_t size) \
{                                                                           \
    return udict_set_opaque(uref->udict, v, size, type, NULL);              \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_delete_##attr(struct uref *uref)          \
{                                                                           \
    return udict_delete(uref->udict, type, NULL);                           \
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
 * @return true if the attribute was found, otherwise p is not modified     \
 */                                                                         \
static inline bool uref_##group##_get_##attr(struct uref *uref,             \
                                             const uint8_t **p,             \
                                             size_t *size_p, args_decl)     \
{                                                                           \
    return udict_get_opaque_va(uref->udict, p, size_p, UDICT_TYPE_OPAQUE,   \
                               format, args);                               \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_set_##attr(struct uref *uref,             \
                                             const uint8_t *v,              \
                                             size_t size, args_decl)        \
{                                                                           \
    return udict_set_opaque_va(uref->udict, v, size, UDICT_TYPE_OPAQUE,     \
                               format, args);                               \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_delete_##attr(struct uref *uref,          \
                                                args_decl)                  \
{                                                                           \
    return udict_delete_va(uref->udict, UDICT_TYPE_OPAQUE, format, args);   \
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
 * @return true if the attribute was found, otherwise p is not modified     \
 */                                                                         \
static inline bool uref_##group##_get_##attr(struct uref *uref,             \
                                             const char **p)                \
{                                                                           \
    return udict_get_string(uref->udict, p, UDICT_TYPE_STRING, name);       \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_set_##attr(struct uref *uref,             \
                                             const char *v)                 \
{                                                                           \
    return udict_set_string(uref->udict, v, UDICT_TYPE_STRING, name);       \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_delete_##attr(struct uref *uref)          \
{                                                                           \
    return udict_delete(uref->udict, UDICT_TYPE_STRING, name);              \
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
 * @return true if the attribute was found, otherwise p is not modified     \
 */                                                                         \
static inline bool uref_##group##_get_##attr(struct uref *uref,             \
                                             const char **p)                \
{                                                                           \
    return udict_get_string(uref->udict, p, type, NULL);                    \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_set_##attr(struct uref *uref,             \
                                             const char *v)                 \
{                                                                           \
    return udict_set_string(uref->udict, v, type, NULL);                    \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_delete_##attr(struct uref *uref)          \
{                                                                           \
    return udict_delete(uref->udict, type, NULL);                           \
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
 * @return true if the attribute was found, otherwise p is not modified     \
 */                                                                         \
static inline bool uref_##group##_get_##attr(struct uref *uref,             \
                                             const char **p, args_decl)     \
{                                                                           \
    return udict_get_string_va(uref->udict, p, UDICT_TYPE_STRING,           \
                               format, args);                               \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_set_##attr(struct uref *uref,             \
                                             const char *v, args_decl)      \
{                                                                           \
    return udict_set_string_va(uref->udict, v, UDICT_TYPE_STRING,           \
                               format, args);                               \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_delete_##attr(struct uref *uref,          \
                                                args_decl)                  \
{                                                                           \
    return udict_delete_va(uref->udict, UDICT_TYPE_STRING, format, args);   \
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
 * @return true if the attribute was found, otherwise p is not modified     \
 */                                                                         \
static inline bool uref_##group##_get_##attr(struct uref *uref)             \
{                                                                           \
    return udict_get_void(uref->udict, NULL, UDICT_TYPE_VOID, name);        \
}                                                                           \
/** @This sets a desc attribute in a uref.                                  \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_set_##attr(struct uref *uref)             \
{                                                                           \
    return udict_set_void(uref->udict, NULL, UDICT_TYPE_VOID, name);        \
}                                                                           \
/** @This deletes a desc attribute from a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_delete_##attr(struct uref *uref)          \
{                                                                           \
    return udict_delete(uref->udict, UDICT_TYPE_VOID, name);                \
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
 * @return true if the attribute was found, otherwise p is not modified     \
 */                                                                         \
static inline bool uref_##group##_get_##attr(struct uref *uref)             \
{                                                                           \
    return udict_get_void(uref->udict, NULL, type, NULL);                   \
}                                                                           \
/** @This sets a desc attribute in a uref.                                  \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_set_##attr(struct uref *uref)             \
{                                                                           \
    return udict_set_void(uref->udict, NULL, type, NULL);                   \
}                                                                           \
/** @This deletes a desc attribute from a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_delete_##attr(struct uref *uref)          \
{                                                                           \
    return udict_delete(uref->udict, type, NULL);                           \
}

/* @This allows to define accessors for a void attribute, with a name
 * depending on printf arguments.
 *
 * @param group group of attributes
 * @param attr readable name of the attribute, for the function names
 * @param format printf-style format of the attribute
 * @param desc description of the attribute
 */
#define UREF_ATTR_VOID_VA(group, attr, name, desc, args_decl, args)         \
/** @This returns the presence of a desc attribute in a uref.               \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return true if the attribute was found, otherwise p is not modified     \
 */                                                                         \
static inline bool uref_##group##_get_##attr(struct uref *uref, args_decl)  \
{                                                                           \
    return udict_get_void_va(uref->udict, NULL, UDICT_TYPE_VOID,            \
                             format, args);                                 \
}                                                                           \
/** @This sets a desc attribute in a uref.                                  \
 *                                                                          \
 * @param uref_p reference to the pointer to the uref (possibly modified)   \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_set_##attr(struct uref *uref,             \
                                             args_decl)                     \
{                                                                           \
    return udict_set_void_va(uref->udict, NULL, UDICT_TYPE_VOID,            \
                             format, args);                                 \
}                                                                           \
/** @This deletes a desc attribute from a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_delete_##attr(struct uref *uref,          \
                                                args_decl)                  \
{                                                                           \
    return udict_delete_va(uref->udict, UDICT_TYPE_VOID, format, args);     \
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
 * @return true if the attribute was found, otherwise p is not modified     \
 */                                                                         \
static inline bool uref_##group##_get_##attr(struct uref *uref,             \
                                             uint8_t *p)                    \
{                                                                           \
    return udict_get_small_unsigned(uref->udict, p,                         \
                                    UDICT_TYPE_SMALL_UNSIGNED, name);       \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_set_##attr(struct uref *uref,             \
                                             uint8_t v)                     \
{                                                                           \
    return udict_set_small_unsigned(uref->udict, v,                         \
                                    UDICT_TYPE_SMALL_UNSIGNED, name);       \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_delete_##attr(struct uref *uref)          \
{                                                                           \
    return udict_delete(uref->udict, UDICT_TYPE_SMALL_UNSIGNED, name);            \
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
 * @return true if the attribute was found, otherwise p is not modified     \
 */                                                                         \
static inline bool uref_##group##_get_##attr(struct uref *uref,             \
                                             uint8_t *p)                    \
{                                                                           \
    return udict_get_small_unsigned(uref->udict, p, type, NULL);            \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_set_##attr(struct uref *uref,             \
                                             uint8_t v)                     \
{                                                                           \
    return udict_set_small_unsigned(uref->udict, v, type, NULL);            \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_delete_##attr(struct uref *uref)          \
{                                                                           \
    return udict_delete(uref->udict, type, NULL);                           \
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
 * @return true if the attribute was found, otherwise p is not modified     \
 */                                                                         \
static inline bool uref_##group##_get_##attr(struct uref *uref,             \
                                             uint8_t *p, args_decl)         \
{                                                                           \
    return udict_get_small_unsigned_va(uref->udict, p,                      \
                                       UDICT_TYPE_SMALL_UNSIGNED,           \
                                       format, args);                       \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_set_##attr(struct uref *uref,             \
                                             uint8_t v, args_decl)          \
{                                                                           \
    return udict_set_small_unsigned_va(uref->udict, v,                      \
                                       UDICT_TYPE_SMALL_UNSIGNED,           \
                                       format, args);                       \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_delete_##attr(struct uref *uref,          \
                                                args_decl)                  \
{                                                                           \
    return udict_delete_va(uref->udict, UDICT_TYPE_SMALL_UNSIGNED,          \
                           format, args);                                   \
}


/*
 * Unsigned attributes
 */

/* @This allows to define accessors for a unsigned attribute.
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
 * @return true if the attribute was found, otherwise p is not modified     \
 */                                                                         \
static inline bool uref_##group##_get_##attr(struct uref *uref,             \
                                             uint64_t *p)                   \
{                                                                           \
    return udict_get_unsigned(uref->udict, p, UDICT_TYPE_UNSIGNED, name);   \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_set_##attr(struct uref *uref,             \
                                             uint64_t v)                    \
{                                                                           \
    return udict_set_unsigned(uref->udict, v, UDICT_TYPE_UNSIGNED, name);   \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_delete_##attr(struct uref *uref)          \
{                                                                           \
    return udict_delete(uref->udict, UDICT_TYPE_UNSIGNED, name);            \
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
 * @return true if the attribute was found, otherwise p is not modified     \
 */                                                                         \
static inline bool uref_##group##_get_##attr(struct uref *uref,             \
                                             uint64_t *p)                   \
{                                                                           \
    return udict_get_unsigned(uref->udict, p, type, NULL);                  \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_set_##attr(struct uref *uref,             \
                                             uint64_t v)                    \
{                                                                           \
    return udict_set_unsigned(uref->udict, v, type, NULL);                  \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_delete_##attr(struct uref *uref)          \
{                                                                           \
    return udict_delete(uref->udict, type, NULL);                           \
}

/* @This allows to define accessors for a unsigned attribute, with a name
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
 * @return true if the attribute was found, otherwise p is not modified     \
 */                                                                         \
static inline bool uref_##group##_get_##attr(struct uref *uref,             \
                                             uint64_t *p, args_decl)        \
{                                                                           \
    return udict_get_unsigned_va(uref->udict, p, UDICT_TYPE_UNSIGNED,       \
                                 format, args);                             \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_set_##attr(struct uref *uref,             \
                                             uint64_t v, args_decl)         \
{                                                                           \
    return udict_set_unsigned_va(uref->udict, v, UDICT_TYPE_UNSIGNED,       \
                                 format, args);                             \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_delete_##attr(struct uref *uref,          \
                                                args_decl)                  \
{                                                                           \
    return udict_delete_va(uref->udict, UDICT_TYPE_UNSIGNED, format, args); \
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
 * @return true if the attribute was found, otherwise p is not modified     \
 */                                                                         \
static inline bool uref_##group##_get_##attr(struct uref *uref,             \
                                             int64_t *p)                    \
{                                                                           \
    return udict_get_int(uref->udict, p, UDICT_TYPE_INT, name);             \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_set_##attr(struct uref *uref,             \
                                             int64_t v)                     \
{                                                                           \
    return udict_set_int(uref->udict, v, UDICT_TYPE_INT, name);             \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_delete_##attr(struct uref *uref)          \
{                                                                           \
    return udict_delete(uref->udict, UDICT_TYPE_INT, name);                 \
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
 * @return true if the attribute was found, otherwise p is not modified     \
 */                                                                         \
static inline bool uref_##group##_get_##attr(struct uref *uref,             \
                                             int64_t *p)                    \
{                                                                           \
    return udict_get_int(uref->udict, p, type, NULL);                       \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_set_##attr(struct uref *uref,             \
                                             int64_t v)                     \
{                                                                           \
    return udict_set_int(uref->udict, v, type, NULL);                       \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_delete_##attr(struct uref *uref)          \
{                                                                           \
    return udict_delete(uref->udict, type, NULL);                           \
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
 * @return true if the attribute was found, otherwise p is not modified     \
 */                                                                         \
static inline bool uref_##group##_get_##attr(struct uref *uref,             \
                                             int64_t *p, args_decl)         \
{                                                                           \
    return udict_get_int_va(uref->udict, p, UDICT_TYPE_INT,                 \
                                 format, args);                             \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_set_##attr(struct uref *uref,             \
                                             int64_t v, args_decl)          \
{                                                                           \
    return udict_set_int_va(uref->udict, v, UDICT_TYPE_INT,                 \
                                 format, args);                             \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_delete_##attr(struct uref *uref,          \
                                                args_decl)                  \
{                                                                           \
    return udict_delete_va(uref->udict, UDICT_TYPE_INT, format, args);      \
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
 * @return true if the attribute was found, otherwise p is not modified     \
 */                                                                         \
static inline bool uref_##group##_get_##attr(struct uref *uref,             \
                                             struct urational *p)           \
{                                                                           \
    return udict_get_rational(uref->udict, p, UDICT_TYPE_RATIONAL, name);   \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_set_##attr(struct uref *uref,             \
                                             struct urational v)            \
{                                                                           \
    return udict_set_rational(uref->udict, v, UDICT_TYPE_RATIONAL, name);   \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_delete_##attr(struct uref *uref)          \
{                                                                           \
    return udict_delete(uref->udict, UDICT_TYPE_RATIONAL, name);            \
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
 * @return true if the attribute was found, otherwise p is not modified     \
 */                                                                         \
static inline bool uref_##group##_get_##attr(struct uref *uref,             \
                                             struct urational *p)           \
{                                                                           \
    return udict_get_rational(uref->udict, p, type, NULL);                  \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_set_##attr(struct uref *uref,             \
                                             struct urational v)            \
{                                                                           \
    return udict_set_rational(uref->udict, v, type, NULL);                  \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_delete_##attr(struct uref *uref)          \
{                                                                           \
    return udict_delete(uref->udict, type, NULL);                           \
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
 * @return true if the attribute was found, otherwise p is not modified     \
 */                                                                         \
static inline bool uref_##group##_get_##attr(struct uref *uref,             \
                                             struct urational *p, args_decl)\
{                                                                           \
    return udict_get_rational_va(uref->udict, p, UDICT_TYPE_RATIONAL,       \
                                 format, args);                             \
}                                                                           \
/** @This sets the desc attribute of a uref.                                \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param v value to set                                                    \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_set_##attr(struct uref *uref,             \
                                             struct urational v, args_decl) \
{                                                                           \
    return udict_set_rational_va(uref->udict, v, UDICT_TYPE_RATIONAL,       \
                                 format, args);                             \
}                                                                           \
/** @This deletes the desc attribute of a uref.                             \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @return true if no allocation failure occurred                           \
 */                                                                         \
static inline bool uref_##group##_delete_##attr(struct uref *uref,          \
                                                args_decl)                  \
{                                                                           \
    return udict_delete_va(uref->udict, UDICT_TYPE_RATIONAL, format, args); \
}

#endif
