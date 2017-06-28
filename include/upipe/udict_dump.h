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
 * @short Upipe dictionary dumping for debug purposes
 */

#ifndef _UPIPE_UDICT_DUMP_H_
/** @hidden */
#define _UPIPE_UDICT_DUMP_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/udict.h>
#include <upipe/uprobe.h>

#include <stdint.h>
#include <inttypes.h>

/** @internal @This dumps the content of a udict for debug purposes.
 *
 * @param udict pointer to the udict
 * @param uprobe pipe module printing the messages
 */
static inline void udict_dump(struct udict *udict, struct uprobe *uprobe)
{
    const char *iname = NULL;
    enum udict_type itype = UDICT_TYPE_END;
    uprobe_dbg_va(uprobe, NULL, "dumping udict %p", udict);

    while (ubase_check(udict_iterate(udict, &iname, &itype)) &&
           itype != UDICT_TYPE_END) {
        const char *name;
        enum udict_type type;
        if (!ubase_check(udict_name(udict, itype, &name, &type))) {
            name = iname;
            type = itype;
        }

        switch (type) {
            default:
                uprobe_dbg_va(uprobe, NULL, " - \"%s\" [unknown]", name);
                break;

            case UDICT_TYPE_OPAQUE: {
                struct udict_opaque val;
                if (likely(ubase_check(udict_get_opaque(udict, &val,
                                                        itype, iname))))
                    uprobe_dbg_va(uprobe, NULL, " - \"%s\" [opaque]: %zu octets",
                                  name, val.size);
                else
                    uprobe_dbg_va(uprobe, NULL, " - \"%s\" [opaque]: [invalid]",
                                  name);
                break;
            }

            case UDICT_TYPE_STRING: {
                const char *val = "";
                if (likely(ubase_check(udict_get_string(udict, &val,
                                                        itype, iname))))
                    uprobe_dbg_va(uprobe, NULL, " - \"%s\" [string]: \"%s\"",
                                  name, val);
                else
                    uprobe_dbg_va(uprobe, NULL, " - \"%s\" [string]: [invalid]",
                                  name);
                break;
            }

            case UDICT_TYPE_VOID:
                uprobe_dbg_va(uprobe, NULL, " - \"%s\" [void]", name);
                break;

            case UDICT_TYPE_BOOL: {
                bool val = false; /* to keep gcc happy */
                if (likely(ubase_check(udict_get_bool(udict, &val,
                                                      itype, iname))))
                    uprobe_dbg_va(uprobe, NULL, " - \"%s\" [bool]: %s", name,
                                  val ? "true" : "false");
                else
                    uprobe_dbg_va(uprobe, NULL, " - \"%s\" [bool]: [invalid]",
                                  name);
                break;
            }

            case UDICT_TYPE_RATIONAL: {
                struct urational val;
                /* to keep gcc happy */
                val.num = val.den = 0;
                if (likely(ubase_check(udict_get_rational(udict, &val,
                                                          itype, iname))))
                    uprobe_dbg_va(uprobe, NULL,
                                  " - \"%s\" [rational]: %" PRId64"/%" PRIu64,
                                  name, val.num, val.den);
                else
                    uprobe_dbg_va(uprobe, NULL,
                                  " - \"%s\" [rational]: [invalid]", name);
                break;
            }

#define UDICT_DUMP_TEMPLATE(TYPE, utype, ctype, ftype)                      \
            case UDICT_TYPE_##TYPE: {                                       \
                ctype val = 0; /* to keep gcc happy */                      \
                if (likely(ubase_check(udict_get_##utype(udict, &val,       \
                                                         itype, iname))))   \
                    uprobe_dbg_va(uprobe, NULL,                             \
                                  " - \"%s\" [" #utype "]: " ftype,         \
                                  name, val);                               \
                else                                                        \
                    uprobe_dbg_va(uprobe, NULL,                             \
                                  " - \"%s\" [" #utype "]: [invalid]",      \
                                  name);                                    \
                break;                                                      \
            }

            UDICT_DUMP_TEMPLATE(SMALL_UNSIGNED, small_unsigned, uint8_t,
                                "%" PRIu8)
            UDICT_DUMP_TEMPLATE(SMALL_INT, small_int, int8_t, "%" PRId8)
            UDICT_DUMP_TEMPLATE(UNSIGNED, unsigned, uint64_t, "%" PRIu64)
            UDICT_DUMP_TEMPLATE(INT, int, int64_t, "%" PRId64)
            UDICT_DUMP_TEMPLATE(FLOAT, float, double, "%f")
#undef UDICT_DUMP_TEMPLATE
        }
    }
    uprobe_dbg_va(uprobe, NULL, "end of attributes for udict %p", udict);
}

#ifdef __cplusplus
}
#endif
#endif
