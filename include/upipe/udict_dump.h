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
 * @short Upipe dictionary dumping for debug purposes
 */

#ifndef _UPIPE_UDICT_DUMP_H_
/** @hidden */
#define _UPIPE_UDICT_DUMP_H_

#include <upipe/ubase.h>
#include <upipe/udict.h>
#include <upipe/ulog.h>

#include <stdint.h>
#include <inttypes.h>

/** @internal @This dumps the content of a udict for debug purposes.
 *
 * @param udict pointer to the udict
 * @param ulog printing facility
 */
static inline void udict_dump(struct udict *udict, struct ulog *ulog)
{
    const char *name = NULL;
    enum udict_type type;
    ulog_debug(ulog, "dumping udict %p", udict);

    while (udict_iterate(udict, &name, &type) && name != NULL) {
        switch (type) {
            default:
                ulog_debug(ulog, " - \"%s\" [unknown]", name);
                break;

            case UDICT_TYPE_OPAQUE:
                ulog_debug(ulog, " - \"%s\" [opaque]", name);
                break;

            case UDICT_TYPE_STRING: {
                const char *val;
                if (likely(udict_get_string(udict, &val, name)))
                    ulog_debug(ulog, " - \"%s\" [string]: \"%s\"", name, val);
                else
                    ulog_debug(ulog, " - \"%s\" [string]: [invalid]", name);
                break;
            }

            case UDICT_TYPE_VOID:
                ulog_debug(ulog, " - \"%s\" [void]", name);
                break;

            case UDICT_TYPE_BOOL: {
                bool val;
                if (likely(udict_get_bool(udict, &val, name)))
                    ulog_debug(ulog, " - \"%s\" [bool]: %s", name,
                               val ? "true" : "false");
                else
                    ulog_debug(ulog, " - \"%s\" [bool]: [invalid]", name);
                break;
            }

            case UDICT_TYPE_RATIONAL: {
                struct urational val;
                if (likely(udict_get_rational(udict, &val, name)))
                    ulog_debug(ulog, " - \"%s\" [rational]: %"PRId64"/%"PRIu64,
                               name, val.num, val.den);
                else
                    ulog_debug(ulog, " - \"%s\" [rational]: [invalid]", name);
                break;
            }

#define UDICT_DUMP_TEMPLATE(TYPE, type, ctype, ftype)                       \
            case UDICT_TYPE_##TYPE: {                                       \
                ctype val;                                                  \
                if (likely(udict_get_##type(udict, &val, name)))            \
                    ulog_debug(ulog, " - \"%s\" [" #type "]: " ftype, name, \
                               val);                                        \
                else                                                        \
                    ulog_debug(ulog, " - \"%s\" [" #type "]: [invalid]",    \
                               name);                                       \
                break;                                                      \
            }

            UDICT_DUMP_TEMPLATE(SMALL_UNSIGNED, small_unsigned, uint8_t,
                               "%"PRIu8)
            UDICT_DUMP_TEMPLATE(SMALL_INT, small_int, int8_t, "%"PRId8)
            UDICT_DUMP_TEMPLATE(UNSIGNED, unsigned, uint64_t, "%"PRIu64)
            UDICT_DUMP_TEMPLATE(INT, int, int64_t, "%"PRId64)
            UDICT_DUMP_TEMPLATE(FLOAT, float, double, "%f")
#undef UDICT_DUMP_TEMPLATE
        }
    }
    ulog_debug(ulog, "end of attributes for udict %p", udict);
}

#endif
