/*****************************************************************************
 * uref_dump.h: upipe uref dumping for debug purposes
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

#ifndef _UPIPE_UREF_DUMP_H_
/** @hidden */
#define _UPIPE_UREF_DUMP_H_

#include <upipe/ubase.h>
#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/ulog.h>

#include <stdint.h>
#include <inttypes.h>

/** @internal @This finds an attribute of the given name and type and returns
 * the name and type of the next attribute.
 *
 * @param uref pointer to the uref
 * @param name_p reference to the name of the attribute to find, changed during
 * execution to the name of the next attribute, or NULL if it was the last
 * attribute; if it was NULL, it is changed to the name of the first attribute
 * @param type_p reference to the type of the attribute, if the name is valid
 */
static inline void uref_dump(struct uref *uref, struct ulog *ulog)
{
    if (uref->ubuf != NULL)
        ulog_debug(ulog, "uref %p points to ubuf %p", uref, uref->ubuf);
    else
        ulog_debug(ulog, "uref %p doesn't point to a ubuf", uref);

    const char *name = NULL;
    enum uref_attrtype type;

    for ( ; ; ) {
        uref_attr_iterate(uref, &name, &type);
        if (unlikely(name == NULL))
            break;

        switch (type) {
            default:
                ulog_debug(ulog, " - \"%s\" [unknown]", name);
                break;

            case UREF_ATTRTYPE_OPAQUE:
                ulog_debug(ulog, " - \"%s\" [opaque]", name);
                break;

            case UREF_ATTRTYPE_STRING: {
                const char *val;
                if (likely(uref_attr_get_string(uref, &val, name)))
                    ulog_debug(ulog, " - \"%s\" [string]: \"%s\"", name, val);
                else
                    ulog_debug(ulog, " - \"%s\" [string]: [invalid]", name);
                break;
            }

            case UREF_ATTRTYPE_VOID:
                ulog_debug(ulog, " - \"%s\" [void]", name);
                break;

            case UREF_ATTRTYPE_BOOL: {
                bool val;
                if (likely(uref_attr_get_bool(uref, &val, name)))
                    ulog_debug(ulog, " - \"%s\" [bool]: %s", name,
                               val ? "true" : "false");
                else
                    ulog_debug(ulog, " - \"%s\" [bool]: [invalid]", name);
                break;
            }

            case UREF_ATTRTYPE_RATIONAL: {
                struct urational val;
                if (likely(uref_attr_get_rational(uref, &val, name)))
                    ulog_debug(ulog, " - \"%s\" [rational]: %"PRId64"/%"PRIu64,
                               name, val.num, val.den);
                else
                    ulog_debug(ulog, " - \"%s\" [rational]: [invalid]", name);
                break;
            }

#define UREF_DUMP_TEMPLATE(TYPE, type, ctype, ftype)                        \
            case UREF_ATTRTYPE_##TYPE: {                                    \
                ctype val;                                                  \
                if (likely(uref_attr_get_##type(uref, &val, name)))         \
                    ulog_debug(ulog, " - \"%s\" [" #type "]: " ftype, name, \
                               val);                                        \
                else                                                        \
                    ulog_debug(ulog, " - \"%s\" [" #type "]: [invalid]",    \
                               name);                                       \
                break;                                                      \
            }

            UREF_DUMP_TEMPLATE(SMALL_UNSIGNED, small_unsigned, uint8_t,
                               "%"PRIu8)
            UREF_DUMP_TEMPLATE(SMALL_INT, small_int, int8_t, "%"PRId8)
            UREF_DUMP_TEMPLATE(UNSIGNED, unsigned, uint64_t, "%"PRIu64)
            UREF_DUMP_TEMPLATE(INT, int, int64_t, "%"PRId64)
            UREF_DUMP_TEMPLATE(FLOAT, float, double, "%f")
#undef UREF_DUMP_TEMPLATE
        }
    }
    ulog_debug(ulog, "end of attributes for uref %p", uref);
}

#endif
