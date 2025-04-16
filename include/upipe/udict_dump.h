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

#include "upipe/ubase.h"
#include "upipe/udict.h"
#include "upipe/uprobe.h"

#include <stdint.h>
#include <inttypes.h>
#include <arpa/inet.h>

static void addr_to_str(const struct sockaddr *addr, socklen_t addrlen, char uri[INET6_ADDRSTRLEN+8])
{
    uint16_t port = 0;
    uri[0] = '\0';

    switch(addr->sa_family) {
    case AF_INET: {
        struct sockaddr_in *in = (struct sockaddr_in *)addr;
        if (addrlen < sizeof(*in))
            return;
        inet_ntop(AF_INET, &in->sin_addr, uri, INET6_ADDRSTRLEN);
        port = ntohs(in->sin_port);
        break;
    }
    case AF_INET6: {
        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)addr;
        if (addrlen < sizeof(*in6))
            return;
        inet_ntop(AF_INET6, &in6->sin6_addr, uri+1, INET6_ADDRSTRLEN);
        uri[0] = '[';
        port = ntohs(in6->sin6_port);
        break;
    }
    default:
        return;
    }

    size_t uri_len = strlen(uri);
    snprintf(&uri[uri_len], INET6_ADDRSTRLEN+8-uri_len, (addr->sa_family == AF_INET6) ? "]:%hu" : ":%hu", port);
}

/** @internal @This dumps the content of a udict for debug purposes.
 *
 * @param udict pointer to the udict
 * @param uprobe pipe module printing the messages
 * @param level uprobe log level
 */
static inline void udict_dump_lvl(struct udict *udict, struct uprobe *uprobe,
                                  enum uprobe_log_level level)
{
    const char *iname = NULL;
    enum udict_type itype = UDICT_TYPE_END;
    uprobe_log_va(uprobe, NULL, level, "dumping udict %p", udict);

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
                uprobe_log_va(uprobe, NULL, level, " - \"%s\" [unknown]", name);
                break;

            case UDICT_TYPE_OPAQUE: {
                struct udict_opaque val;
                if (likely(ubase_check(udict_get_opaque(udict, &val,
                                                        itype, iname))))
                    uprobe_log_va(uprobe, NULL, level,
                                  " - \"%s\" [opaque]: %zu octets",
                                  name, val.size);
                else
                    uprobe_log_va(uprobe, NULL, level,
                                  " - \"%s\" [opaque]: [invalid]", name);
                break;
            }

            case UDICT_TYPE_STRING: {
                const char *val = "";
                if (likely(ubase_check(udict_get_string(udict, &val,
                                                        itype, iname))))
                    uprobe_log_va(uprobe, NULL, level,
                                  " - \"%s\" [string]: \"%s\"", name, val);
                else
                    uprobe_log_va(uprobe, NULL, level,
                                  " - \"%s\" [string]: [invalid]", name);
                break;
            }

            case UDICT_TYPE_VOID:
                uprobe_log_va(uprobe, NULL, level, " - \"%s\" [void]", name);
                break;

            case UDICT_TYPE_BOOL: {
                bool val = false; /* to keep gcc happy */
                if (likely(ubase_check(udict_get_bool(udict, &val,
                                                      itype, iname))))
                    uprobe_log_va(uprobe, NULL, level,
                                  " - \"%s\" [bool]: %s", name,
                                  val ? "true" : "false");
                else
                    uprobe_log_va(uprobe, NULL, level,
                                  " - \"%s\" [bool]: [invalid]", name);
                break;
            }

            case UDICT_TYPE_RATIONAL: {
                struct urational val;
                /* to keep gcc happy */
                val.num = val.den = 0;
                if (likely(ubase_check(udict_get_rational(udict, &val,
                                                          itype, iname))))
                    uprobe_log_va(uprobe, NULL, level,
                                  " - \"%s\" [rational]: %" PRId64"/%" PRIu64,
                                  name, val.num, val.den);
                else
                    uprobe_log_va(uprobe, NULL, level,
                                  " - \"%s\" [rational]: [invalid]", name);
                break;
            }

            case UDICT_TYPE_SOCKADDR: {
                struct udict_opaque val;
                if (likely(ubase_check(udict_get_opaque(udict, &val,
                                                        itype, iname)))) {
                    struct sockaddr *addr = (struct sockaddr*)val.v;
                    socklen_t addrlen = val.size;
                    char uri[INET6_ADDRSTRLEN+8];
                    addr_to_str(addr, addrlen, uri);
                    uprobe_log_va(uprobe, NULL, level,
                                  " - \"%s\" [sockaddr]: %s", name, uri);
                } else
                    uprobe_log_va(uprobe, NULL, level,
                                  " - \"%s\" [sockaddr]: [invalid]", name);
                break;
            }

#define UDICT_DUMP_TEMPLATE(TYPE, utype, ctype, ftype)                      \
            case UDICT_TYPE_##TYPE: {                                       \
                ctype val = 0; /* to keep gcc happy */                      \
                if (likely(ubase_check(udict_get_##utype(udict, &val,       \
                                                         itype, iname))))   \
                    uprobe_log_va(uprobe, NULL, level,                      \
                                  " - \"%s\" [" #utype "]: " ftype,         \
                                  name, val);                               \
                else                                                        \
                    uprobe_log_va(uprobe, NULL, level,                      \
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
    uprobe_log_va(uprobe, NULL, level, "end of attributes for udict %p", udict);
}

/** @hidden */
#define UDICT_DUMP(Name, Level)                                             \
/** @internal @This dumps the content of a uref for debug purposes.         \
 *                                                                          \
 * @param udict pointer to the udict                                        \
 * @param uprobe pipe module printing the messages                          \
 */                                                                         \
static inline void udict_dump##Name(struct udict *udict,                    \
                                    struct uprobe *uprobe)                  \
{                                                                           \
    return udict_dump_lvl(udict, uprobe, UPROBE_LOG_##Level);               \
}
UDICT_DUMP(, DEBUG);
UDICT_DUMP(_verbose, VERBOSE);
UDICT_DUMP(_dbg, DEBUG);
UDICT_DUMP(_info, INFO);
UDICT_DUMP(_notice, NOTICE);
UDICT_DUMP(_warn, WARNING);
UDICT_DUMP(_err, ERROR);
#undef UDICT_DUMP

#ifdef __cplusplus
}
#endif
#endif
