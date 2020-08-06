/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 * Copyright (C) 2020 EasyTools
 *
 * Authors: Christophe Massiot
 *          Arnaud de Turckheim
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
 * @short Upipe uref dumping for debug purposes
 */

#ifndef _UPIPE_UREF_DUMP_H_
/** @hidden */
#define _UPIPE_UREF_DUMP_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_clock.h>
#include <upipe/udict_dump.h>
#include <upipe/uprobe.h>

#include <stdint.h>
#include <inttypes.h>

/** @internal @This dumps the content of a uref for debug purposes.
 *
 * @param uref pointer to the uref
 * @param uprobe pipe module printing the messages
 * @param level uprobe log level
 */
static inline void uref_dump_lvl(struct uref *uref, struct uprobe *uprobe,
                                 enum uprobe_log_level level)
{
    if (uref->ubuf != NULL)
        uprobe_log_va(uprobe, NULL, level,
                      "dumping uref %p pointing to ubuf %p", uref, uref->ubuf);
    else
        uprobe_log_va(uprobe, NULL, level, "dumping uref %p", uref);

#define UREF_DUMP_VOID(name, member)                                        \
    if (uref->flags & member)                                               \
        uprobe_log_va(uprobe, NULL, level, " - \"" name "\" [void]");
    UREF_DUMP_VOID("f.end", UREF_FLAG_FLOW_END)
    UREF_DUMP_VOID("f.disc", UREF_FLAG_FLOW_DISC)
    UREF_DUMP_VOID("b.start", UREF_FLAG_BLOCK_START)
#undef UREF_DUMP_VOID

#define UREF_DUMP_DATE(name, member)                                        \
    {                                                                       \
        int type;                                                           \
        uint64_t date;                                                      \
        uref_clock_get_date_##member(uref, &date, &type);                   \
        switch (type) {                                                     \
            case UREF_DATE_PTS:                                             \
                uprobe_log_va(uprobe, NULL, level,                          \
                              " - \"" name"\" [pts]: %" PRIu64, date);      \
                break;                                                      \
            case UREF_DATE_DTS:                                             \
                uprobe_log_va(uprobe, NULL, level,                          \
                              " - \"" name"\" [dts]: %" PRIu64, date);      \
                break;                                                      \
            case UREF_DATE_CR:                                              \
                uprobe_log_va(uprobe, NULL, level,                          \
                              " - \"" name"\" [cr]: %" PRIu64, date);       \
                break;                                                      \
            default:                                                        \
                break;                                                      \
        }                                                                   \
    }
    UREF_DUMP_DATE("k.sys", sys);
    UREF_DUMP_DATE("k.prog", prog);
    UREF_DUMP_DATE("k.orig", orig);
#undef UREF_DUMP_DATE

#define UREF_DUMP_UNSIGNED(name, member)                                    \
    if (uref->member != UINT64_MAX)                                         \
        uprobe_log_va(uprobe, NULL, level,                                  \
                      " - \"" name"\" [unsigned]: %" PRIu64, uref->member);
    UREF_DUMP_UNSIGNED("k.dts_pts_delay", dts_pts_delay)
    UREF_DUMP_UNSIGNED("k.cr_dts_delay", cr_dts_delay)
    UREF_DUMP_UNSIGNED("k.rap_cr_delay", rap_cr_delay)
#undef UREF_DUMP_UNSIGNED

    if (uref->udict != NULL)
        udict_dump_lvl(uref->udict, uprobe, level);
}

/** @hidden */
#define UREF_DUMP(Name, Level)                                              \
/** @internal @This dumps the content of a uref for debug purposes.         \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param uprobe pipe module printing the messages                          \
 */                                                                         \
static inline void uref_dump##Name(struct uref *uref,                       \
                                   struct uprobe *uprobe)                   \
{                                                                           \
    return uref_dump_lvl(uref, uprobe, UPROBE_LOG_##Level);                 \
}
UREF_DUMP(, DEBUG);
UREF_DUMP(_verbose, VERBOSE);
UREF_DUMP(_dbg, DEBUG);
UREF_DUMP(_info, INFO);
UREF_DUMP(_notice, NOTICE);
UREF_DUMP(_warn, WARNING);
UREF_DUMP(_err, ERROR);
#undef UREF_DUMP

/** @internal @This dumps the clock content of a uref for debug purposes.
 *
 * @param uref pointer to the uref
 * @param uprobe pipe module printing the messages
 * @param level uprobe log level
 */
static inline void uref_dump_clock_lvl(struct uref *uref,
                                       struct uprobe *uprobe,
                                       enum uprobe_log_level level)
{
    const char *pfx;

#define UREF_DUMP_CLOCK_BD_FMT "%02u:%02u:%02u.%03u+%05u"

#define UREF_DUMP_CLOCK_BD_ARGS(x) \
    x.hours, x.minutes, x.seconds, x.milliseconds, x.ticks

#define UREF_DUMP_CLOCK_DUMP(Type, TYPE, Clock)                             \
        uint64_t Type##_##Clock = UINT64_MAX;                               \
        uref_clock_get_##Type##_##Clock(uref, &Type##_##Clock);             \
        if (type_##Clock == UREF_DATE_##TYPE)                               \
            pfx = " *";                                                     \
        else                                                                \
            pfx = "  ";                                                     \
        struct uclock_brokendown Type##_##Clock##_bd =                      \
            uclock_breakdown(Type##_##Clock);                               \
        if (Type##_##Clock != UINT64_MAX)                                   \
            uprobe_log_va(uprobe, NULL, level,                              \
                          "%s%4s %3s: " UREF_DUMP_CLOCK_BD_FMT              \
                          " - %" PRIu64 "\n",                               \
                          pfx, #Clock, #Type,                               \
                          UREF_DUMP_CLOCK_BD_ARGS(Type##_##Clock##_bd),     \
                          Type##_##Clock);                                  \

#define UREF_DUMP_CLOCK_DUMP_CLOCK(Clock)                                   \
    uint64_t date_##Clock = UINT64_MAX;                                     \
    int type_##Clock = UREF_DATE_NONE;                                      \
    uref_clock_get_date_##Clock(uref, &date_##Clock, &type_##Clock);        \
    UREF_DUMP_CLOCK_DUMP(pts, PTS, Clock);                                  \
    UREF_DUMP_CLOCK_DUMP(dts, DTS, Clock);                                  \
    UREF_DUMP_CLOCK_DUMP(cr, CR, Clock);

    uprobe_log_va(uprobe, NULL, level, "uref %p:", uref);
UREF_DUMP_CLOCK_DUMP_CLOCK(orig);
UREF_DUMP_CLOCK_DUMP_CLOCK(prog);
UREF_DUMP_CLOCK_DUMP_CLOCK(sys);

#undef UREF_DUMP_CLOCK_DUMP_CLOCK
#undef UREF_DUMP_CLOCK_DUMP
#undef UREF_DUMP_CLOCK_BD_ARGS
#undef UREF_DUMP_CLOCK_BD_FMT
}

/** @hidden */
#define UREF_DUMP_CLOCK(Name, Level)                                        \
/** @internal @This dumps the clock of a uref for debug purposes.           \
 *                                                                          \
 * @param uref pointer to the uref                                          \
 * @param uprobe pipe module printing the messages                          \
 */                                                                         \
static inline void uref_dump_clock##Name(struct uref *uref,                 \
                                         struct uprobe *uprobe)             \
{                                                                           \
    return uref_dump_clock_lvl(uref, uprobe, UPROBE_LOG_##Level);           \
}
UREF_DUMP_CLOCK(, DEBUG);
UREF_DUMP_CLOCK(_verbose, VERBOSE);
UREF_DUMP_CLOCK(_dbg, DEBUG);
UREF_DUMP_CLOCK(_info, INFO);
UREF_DUMP_CLOCK(_notice, NOTICE);
UREF_DUMP_CLOCK(_warn, WARNING);
UREF_DUMP_CLOCK(_err, ERROR);
#undef UREF_DUMP_CLOCK

#ifdef __cplusplus
}
#endif
#endif
