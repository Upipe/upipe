/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
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
 * @short Upipe clock attributes for uref
 */

#ifndef _UPIPE_UREF_CLOCK_H_
/** @hidden */
#define _UPIPE_UREF_CLOCK_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uref.h>
#include <upipe/uref_attr.h>

#include <stdint.h>

UREF_ATTR_VOID_UREF(clock, ref, UREF_FLAG_CLOCK_REF,
        flag indicating the presence of a clock reference)
UREF_ATTR_UNSIGNED_UREF(clock, dts_pts_delay, dts_pts_delay,
        delay between DTS and PTS)
UREF_ATTR_UNSIGNED_UREF(clock, cr_dts_delay, cr_dts_delay,
        delay between CR and DTS)
UREF_ATTR_UNSIGNED_UREF(clock, rap_sys, rap_sys,
        system date of the latest random access point)
UREF_ATTR_UNSIGNED_SH(clock, duration, UDICT_TYPE_CLOCK_DURATION, duration)
UREF_ATTR_SMALL_UNSIGNED(clock, index_rap, "k.index_rap",
                    frame offset from last random access point)
UREF_ATTR_RATIONAL(clock, rate, "k.rate", playing rate)
UREF_ATTR_UNSIGNED_SH(clock, latency, UDICT_TYPE_CLOCK_LATENCY,
        latency in uclock units)

/** @hidden */
#define UREF_CLOCK_TEMPLATE(dv, DV)                                         \
/** @This gets the dv date.                                                 \
 *                                                                          \
 * @param uref uref structure                                               \
 * @param date_p filled in with the date in #UCLOCK_FREQ units              \
 * @param type_p filled in with the type of date                            \
 */                                                                         \
static inline void uref_clock_get_date_##dv(const struct uref *uref,        \
                                            uint64_t *date_p,               \
                                            enum uref_date_type *type_p)    \
{                                                                           \
    *date_p = uref->date_##dv;                                              \
    *type_p = (uref->flags >> UREF_FLAG_DATE_##DV##_SHIFT) & 0x3;           \
}                                                                           \
/** @This sets the dv date.                                                 \
 *                                                                          \
 * @param uref uref structure                                               \
 * @param date date in #UCLOCK_FREQ units                                   \
 * @param type type of date                                                 \
 */                                                                         \
static inline void uref_clock_set_date_##dv(struct uref *uref,              \
                                            uint64_t date,                  \
                                            enum uref_date_type type)       \
{                                                                           \
    uref->date_##dv = date;                                                 \
    uref->flags &= ~(UINT64_C(0x3) << UREF_FLAG_DATE_##DV##_SHIFT);         \
    uref->flags |= ((uint64_t)type << UREF_FLAG_DATE_##DV##_SHIFT);         \
}

UREF_CLOCK_TEMPLATE(sys, SYS)
UREF_CLOCK_TEMPLATE(prog, PROG)
UREF_CLOCK_TEMPLATE(orig, ORIG)
#undef UREF_CLOCK_TEMPLATE

/** @hidden */
#define UREF_CLOCK_SET(dv, dt, DT)                                          \
/** @This sets the dv date as a dt.                                         \
 *                                                                          \
 * @param uref uref structure                                               \
 * @param date date in #UCLOCK_FREQ units                                   \
 */                                                                         \
static inline void uref_clock_set_##dt##_##dv(struct uref *uref,            \
                                              uint64_t date)                \
{                                                                           \
    uref_clock_set_date_##dv(uref, date, UREF_DATE_##DT);                   \
}

UREF_CLOCK_SET(sys, cr, CR)
UREF_CLOCK_SET(prog, cr, CR)
UREF_CLOCK_SET(orig, cr, CR)

UREF_CLOCK_SET(sys, dts, DTS)
UREF_CLOCK_SET(prog, dts, DTS)
UREF_CLOCK_SET(orig, dts, DTS)

UREF_CLOCK_SET(sys, pts, PTS)
UREF_CLOCK_SET(prog, pts, PTS)
UREF_CLOCK_SET(orig, pts, PTS)
#undef UREF_CLOCK_SET

/** @hidden */
#define UREF_CLOCK_GET_PTS(dv)                                              \
/** @This gets the dv date as a PTS.                                        \
 *                                                                          \
 * @param uref uref structure                                               \
 * @param date_p filled in with the date in #UCLOCK_FREQ units (may be NULL)\
 * @return false in case of failure                                         \
 */                                                                         \
static inline bool uref_clock_get_pts_##dv(struct uref *uref,               \
                                           uint64_t *date_p)                \
{                                                                           \
    uint64_t date, delay;                                                   \
    enum uref_date_type type;                                               \
    uref_clock_get_date_##dv(uref, &date, &type);                           \
    switch (type) {                                                         \
        /* intended pass-throughs */                                        \
        default:                                                            \
        case UREF_DATE_NONE:                                                \
            return false;                                                   \
        case UREF_DATE_CR:                                                  \
            if (unlikely(!uref_clock_get_cr_dts_delay(uref, &delay)))       \
                return false;                                               \
            date += delay;                                                  \
        case UREF_DATE_DTS:                                                 \
            if (unlikely(!uref_clock_get_dts_pts_delay(uref, &delay)))      \
                return false;                                               \
            date += delay;                                                  \
        case UREF_DATE_PTS:                                                 \
            break;                                                          \
    }                                                                       \
    if (date_p != NULL)                                                     \
        *date_p = date;                                                     \
    return true;                                                            \
}

UREF_CLOCK_GET_PTS(sys)
UREF_CLOCK_GET_PTS(prog)
UREF_CLOCK_GET_PTS(orig)
#undef UREF_CLOCK_GET_PTS

/** @hidden */
#define UREF_CLOCK_GET_DTS(dv)                                              \
/** @This gets the dv date as a DTS.                                        \
 *                                                                          \
 * @param uref uref structure                                               \
 * @param date_p filled in with the date in #UCLOCK_FREQ units (may be NULL)\
 * @return false in case of failure                                         \
 */                                                                         \
static inline bool uref_clock_get_dts_##dv(struct uref *uref,               \
                                           uint64_t *date_p)                \
{                                                                           \
    uint64_t date, delay;                                                   \
    enum uref_date_type type;                                               \
    uref_clock_get_date_##dv(uref, &date, &type);                           \
    switch (type) {                                                         \
        /* intended pass-throughs */                                        \
        default:                                                            \
        case UREF_DATE_NONE:                                                \
            return false;                                                   \
        case UREF_DATE_CR:                                                  \
            if (unlikely(!uref_clock_get_cr_dts_delay(uref, &delay)))       \
                return false;                                               \
            date += delay;                                                  \
        case UREF_DATE_DTS:                                                 \
            break;                                                          \
        case UREF_DATE_PTS:                                                 \
            if (unlikely(!uref_clock_get_dts_pts_delay(uref, &delay)))      \
                return false;                                               \
            date -= delay;                                                  \
            break;                                                          \
    }                                                                       \
    if (date_p != NULL)                                                     \
        *date_p = date;                                                     \
    return true;                                                            \
}

UREF_CLOCK_GET_DTS(sys)
UREF_CLOCK_GET_DTS(prog)
UREF_CLOCK_GET_DTS(orig)
#undef UREF_CLOCK_GET_DTS

/** @hidden */
#define UREF_CLOCK_GET_CR(dv)                                               \
/** @This gets the dv date as a CR.                                         \
 *                                                                          \
 * @param uref uref structure                                               \
 * @param date_p filled in with the date in #UCLOCK_FREQ units (may be NULL)\
 * @return false in case of failure                                         \
 */                                                                         \
static inline bool uref_clock_get_cr_##dv(struct uref *uref,                \
                                          uint64_t *date_p)                 \
{                                                                           \
    uint64_t date, delay;                                                   \
    enum uref_date_type type;                                               \
    uref_clock_get_date_##dv(uref, &date, &type);                           \
    switch (type) {                                                         \
        /* intended pass-throughs */                                        \
        default:                                                            \
        case UREF_DATE_NONE:                                                \
            return false;                                                   \
        case UREF_DATE_PTS:                                                 \
            if (unlikely(!uref_clock_get_dts_pts_delay(uref, &delay)))      \
                return false;                                               \
            date -= delay;                                                  \
        case UREF_DATE_DTS:                                                 \
            if (unlikely(!uref_clock_get_cr_dts_delay(uref, &delay)))       \
                return false;                                               \
            date -= delay;                                                  \
        case UREF_DATE_CR:                                                  \
            break;                                                          \
    }                                                                       \
    if (date_p != NULL)                                                     \
        *date_p = date;                                                     \
    return true;                                                            \
}

UREF_CLOCK_GET_CR(sys)
UREF_CLOCK_GET_CR(prog)
UREF_CLOCK_GET_CR(orig)
#undef UREF_CLOCK_GET_CR

/** @hidden */
#define UREF_CLOCK_REBASE(dv, dt)                                           \
/** @This rebases the dv date as a ##dt.                                    \
 *                                                                          \
 * @param uref uref structure                                               \
 */                                                                         \
static inline bool uref_clock_rebase_##dt##_##dv(struct uref *uref)         \
{                                                                           \
    uint64_t date;                                                          \
    if (unlikely(!uref_clock_get_##dt##_##dv(uref, &date)))                 \
        return false;                                                       \
    uref_clock_set_##dt##_##dv(uref, date);                                 \
    return true;                                                            \
}

UREF_CLOCK_REBASE(sys, cr)
UREF_CLOCK_REBASE(prog, cr)
UREF_CLOCK_REBASE(orig, cr)

UREF_CLOCK_REBASE(sys, dts)
UREF_CLOCK_REBASE(prog, dts)
UREF_CLOCK_REBASE(orig, dts)

UREF_CLOCK_REBASE(sys, pts)
UREF_CLOCK_REBASE(prog, pts)
UREF_CLOCK_REBASE(orig, pts)
#undef UREF_CLOCK_REBASE

#ifdef __cplusplus
}
#endif
#endif
