/*
 * Copyright (C) 2012-2017 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
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

#include "upipe/uref.h"
#include "upipe/uref_attr.h"

#include <stdint.h>

UREF_ATTR_VOID_UREF(clock, ref, UREF_FLAG_CLOCK_REF,
        flag indicating the presence of a clock reference)
UREF_ATTR_UNSIGNED_UREF(clock, dts_pts_delay, dts_pts_delay,
        delay between DTS and PTS)
UREF_ATTR_UNSIGNED_UREF(clock, cr_dts_delay, cr_dts_delay,
        delay between CR and DTS)
UREF_ATTR_UNSIGNED_UREF(clock, rap_cr_delay, rap_cr_delay,
        delay between RAP and CR)
/* duration and rate live in struct uref, like the dates: every framer writes
 * them and every sink reads them, and a dictionary lookup per frame was
 * measurable.  The setters keep the error code of the dictionary versions. */

/** @This returns the duration attribute of a uref.
 *
 * @param uref pointer to the uref
 * @param p pointer to the retrieved value (modified during execution)
 * @return an error code
 */
static inline int uref_clock_get_duration(struct uref *uref, uint64_t *p)
{
    if (uref->duration != UINT64_MAX) {
        *p = uref->duration;
        return UBASE_ERR_NONE;
    }
    return UBASE_ERR_INVALID;
}
/** @This sets the duration attribute of a uref.
 *
 * @param uref pointer to the uref
 * @param v value to set
 * @return an error code
 */
static inline int uref_clock_set_duration(struct uref *uref, uint64_t v)
{
    uref->duration = v;
    return UBASE_ERR_NONE;
}
/** @This deletes the duration attribute of a uref.
 *
 * @param uref pointer to the uref
 * @return an error code
 */
static inline int uref_clock_delete_duration(struct uref *uref)
{
    uref->duration = UINT64_MAX;
    return UBASE_ERR_NONE;
}
/** @This copies the duration attribute from an uref to another.
 *
 * @param uref pointer to the uref
 * @param uref_src pointer to the source uref
 * @return an error code
 */
static inline int uref_clock_copy_duration(struct uref *uref,
                                           struct uref *uref_src)
{
    uref->duration = uref_src->duration;
    return UBASE_ERR_NONE;
}
/** @This compares the duration attribute to given values.
 *
 * @param uref pointer to the uref
 * @param min minimum value
 * @param max maximum value
 * @return an error code
 */
static inline int uref_clock_match_duration(struct uref *uref,
                                            uint64_t min, uint64_t max)
{
    uint64_t v;
    UBASE_RETURN(uref_clock_get_duration(uref, &v));
    return (v >= min) && (v <= max) ? UBASE_ERR_NONE : UBASE_ERR_INVALID;
}
/** @This compares the duration attribute in two urefs.
 *
 * @param uref1 pointer to the first uref
 * @param uref2 pointer to the second uref
 * @return 0 if both attributes are absent or identical
 */
static inline int uref_clock_cmp_duration(struct uref *uref1,
                                          struct uref *uref2)
{
    uint64_t v1 = 0, v2 = 0;
    int err1 = uref_clock_get_duration(uref1, &v1);
    int err2 = uref_clock_get_duration(uref2, &v2);
    if (!ubase_check(err1) && !ubase_check(err2))
        return 0;
    if (!ubase_check(err1) || !ubase_check(err2))
        return -1;
    return v1 - v2;
}
UREF_ATTR_SMALL_UNSIGNED(clock, index_rap, "k.index_rap",
                    frame offset from last random access point)
/** @This returns the playing rate attribute of a uref.
 *
 * @param uref pointer to the uref
 * @param p pointer to the retrieved value (modified during execution)
 * @return an error code
 */
static inline int uref_clock_get_rate(struct uref *uref, struct urational *p)
{
    if (uref->rate.den != 0) {
        *p = uref->rate;
        return UBASE_ERR_NONE;
    }
    return UBASE_ERR_INVALID;
}
/** @This sets the playing rate attribute of a uref.
 *
 * @param uref pointer to the uref
 * @param v value to set
 * @return an error code
 */
static inline int uref_clock_set_rate(struct uref *uref, struct urational v)
{
    uref->rate = v;
    return UBASE_ERR_NONE;
}
/** @This deletes the playing rate attribute of a uref.
 *
 * @param uref pointer to the uref
 * @return an error code
 */
static inline int uref_clock_delete_rate(struct uref *uref)
{
    uref->rate.num = 0;
    uref->rate.den = 0;
    return UBASE_ERR_NONE;
}
/** @This copies the playing rate attribute from an uref to another.
 *
 * @param uref pointer to the uref
 * @param uref_src pointer to the source uref
 * @return an error code
 */
static inline int uref_clock_copy_rate(struct uref *uref,
                                       struct uref *uref_src)
{
    uref->rate = uref_src->rate;
    return UBASE_ERR_NONE;
}
UREF_ATTR_UNSIGNED_SH(clock, latency, UDICT_TYPE_CLOCK_LATENCY,
        latency in uclock units)
UREF_ATTR_UNSIGNED_SH(clock, wrap, UDICT_TYPE_CLOCK_WRAP, wrap around value)

/** @hidden */
#define UREF_CLOCK_TEMPLATE(dv, DV)                                         \
/** @This gets the dv date.                                                 \
 *                                                                          \
 * @param uref uref structure                                               \
 * @param date_p filled in with the date in #UCLOCK_FREQ units              \
 * @param type_p filled in with the type of date                            \
 */                                                                         \
static inline void uref_clock_get_date_##dv(const struct uref *uref,        \
                                            uint64_t *date_p, int *type_p)  \
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
                                            uint64_t date, int type)        \
{                                                                           \
    int current_type;                                                       \
    uint64_t current_date;                                                  \
    uref_clock_get_date_##dv(uref, &current_date, &current_type);           \
    switch (current_type) {                                                 \
        case UREF_DATE_CR: {                                                \
            uint64_t dts_pts_delay;                                         \
            if (type == UREF_DATE_PTS &&                                    \
                ubase_check(uref_clock_get_dts_pts_delay(uref,              \
                                                         &dts_pts_delay)))  \
                uref_clock_set_cr_dts_delay(uref,                           \
                        date - dts_pts_delay - current_date);               \
            else if (type == UREF_DATE_DTS)                                 \
                uref_clock_set_cr_dts_delay(uref, date - current_date);     \
            break;                                                          \
        }                                                                   \
        case UREF_DATE_DTS:                                                 \
            if (type == UREF_DATE_PTS)                                      \
                uref_clock_set_dts_pts_delay(uref, date - current_date);    \
            break;                                                          \
        default:                                                            \
            break;                                                          \
    }                                                                       \
    uref->date_##dv = date;                                                 \
    uref->flags &= ~(UINT64_C(0x3) << UREF_FLAG_DATE_##DV##_SHIFT);         \
    uref->flags |= ((uint64_t)type << UREF_FLAG_DATE_##DV##_SHIFT);         \
}                                                                           \
/** @This deletes the dv date.                                              \
 *                                                                          \
 * @param uref uref structure                                               \
 */                                                                         \
static inline void uref_clock_delete_date_##dv(struct uref *uref)           \
{                                                                           \
    uref->date_##dv = UINT64_MAX;                                           \
    uref->flags &= ~(UINT64_C(0x3) << UREF_FLAG_DATE_##DV##_SHIFT);         \
}                                                                           \
/** @This adds the given delay to the dv date.                              \
 *                                                                          \
 * @param uref uref structure                                               \
 * @param delay delay to add in #UCLOCK_FREQ units                          \
 */                                                                         \
static inline void uref_clock_add_date_##dv(struct uref *uref,              \
                                            int64_t delay)                  \
{                                                                           \
    if (uref->date_##dv != UINT64_MAX)                                      \
        uref->date_##dv += delay;                                           \
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
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_clock_get_pts_##dv(struct uref *uref,                \
                                          uint64_t *date_p)                 \
{                                                                           \
    uint64_t date, delay;                                                   \
    int type;                                                               \
    uref_clock_get_date_##dv(uref, &date, &type);                           \
    switch (type) {                                                         \
        default:                                                            \
        case UREF_DATE_NONE:                                                \
            return UBASE_ERR_INVALID;                                       \
        case UREF_DATE_CR:                                                  \
            UBASE_RETURN(uref_clock_get_cr_dts_delay(uref, &delay))         \
            date += delay;                                                  \
            /* fallthrough */                                               \
        case UREF_DATE_DTS:                                                 \
            UBASE_RETURN(uref_clock_get_dts_pts_delay(uref, &delay))        \
            date += delay;                                                  \
            /* fallthrough */                                               \
        case UREF_DATE_PTS:                                                 \
            break;                                                          \
    }                                                                       \
    if (date_p != NULL)                                                     \
        *date_p = date;                                                     \
    return UBASE_ERR_NONE;                                                  \
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
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_clock_get_dts_##dv(struct uref *uref,                \
                                          uint64_t *date_p)                 \
{                                                                           \
    uint64_t date, delay;                                                   \
    int type;                                                               \
    uref_clock_get_date_##dv(uref, &date, &type);                           \
    switch (type) {                                                         \
        default:                                                            \
        case UREF_DATE_NONE:                                                \
            return UBASE_ERR_INVALID;                                       \
        case UREF_DATE_CR:                                                  \
            UBASE_RETURN(uref_clock_get_cr_dts_delay(uref, &delay))         \
            date += delay;                                                  \
            /* fallthrough */                                               \
        case UREF_DATE_DTS:                                                 \
            break;                                                          \
        case UREF_DATE_PTS:                                                 \
            UBASE_RETURN(uref_clock_get_dts_pts_delay(uref, &delay))        \
            date -= delay;                                                  \
            break;                                                          \
    }                                                                       \
    if (date_p != NULL)                                                     \
        *date_p = date;                                                     \
    return UBASE_ERR_NONE;                                                  \
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
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_clock_get_cr_##dv(struct uref *uref,                 \
                                         uint64_t *date_p)                  \
{                                                                           \
    uint64_t date, delay;                                                   \
    int type;                                                               \
    uref_clock_get_date_##dv(uref, &date, &type);                           \
    switch (type) {                                                         \
        default:                                                            \
        case UREF_DATE_NONE:                                                \
            return UBASE_ERR_INVALID;                                       \
        case UREF_DATE_PTS:                                                 \
            UBASE_RETURN(uref_clock_get_dts_pts_delay(uref, &delay))        \
            date -= delay;                                                  \
            /* fallthrough */                                               \
        case UREF_DATE_DTS:                                                 \
            UBASE_RETURN(uref_clock_get_cr_dts_delay(uref, &delay))         \
            date -= delay;                                                  \
            /* fallthrough */                                               \
        case UREF_DATE_CR:                                                  \
            break;                                                          \
    }                                                                       \
    if (date_p != NULL)                                                     \
        *date_p = date;                                                     \
    return UBASE_ERR_NONE;                                                  \
}

UREF_CLOCK_GET_CR(sys)
UREF_CLOCK_GET_CR(prog)
UREF_CLOCK_GET_CR(orig)
#undef UREF_CLOCK_GET_CR

/** @hidden */
#define UREF_CLOCK_GET_RAP(dv)                                              \
/** @This gets the dv date as a RAP.                                        \
 *                                                                          \
 * @param uref uref structure                                               \
 * @param date_p filled in with the date in #UCLOCK_FREQ units (may be NULL)\
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_clock_get_rap_##dv(struct uref *uref,                \
                                          uint64_t *date_p)                 \
{                                                                           \
    uint64_t date, delay;                                                   \
    int type;                                                               \
    uref_clock_get_date_##dv(uref, &date, &type);                           \
    switch (type) {                                                         \
        default:                                                            \
        case UREF_DATE_NONE:                                                \
            return UBASE_ERR_INVALID;                                       \
        case UREF_DATE_PTS:                                                 \
            UBASE_RETURN(uref_clock_get_dts_pts_delay(uref, &delay))        \
            date -= delay;                                                  \
            /* fallthrough */                                               \
        case UREF_DATE_DTS:                                                 \
            UBASE_RETURN(uref_clock_get_cr_dts_delay(uref, &delay))         \
            date -= delay;                                                  \
            /* fallthrough */                                               \
        case UREF_DATE_CR:                                                  \
            UBASE_RETURN(uref_clock_get_rap_cr_delay(uref, &delay))         \
            date -= delay;                                                  \
            break;                                                          \
    }                                                                       \
    if (date_p != NULL)                                                     \
        *date_p = date;                                                     \
    return UBASE_ERR_NONE;                                                  \
}

UREF_CLOCK_GET_RAP(sys)
UREF_CLOCK_GET_RAP(prog)
UREF_CLOCK_GET_RAP(orig)
#undef UREF_CLOCK_GET_RAP

/** @hidden */
#define UREF_CLOCK_SET_RAP(dv)                                              \
/** @This sets the CR/RAP delay.                                            \
 *                                                                          \
 * @param uref uref structure                                               \
 * @param rap RAP in #UCLOCK_FREQ units                                     \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_clock_set_rap_##dv(struct uref *uref, uint64_t rap)  \
{                                                                           \
    uint64_t cr;                                                            \
    UBASE_RETURN(uref_clock_get_cr_##dv(uref, &cr));                        \
    if (rap > cr)                                                           \
        return UBASE_ERR_INVALID;                                           \
    uref_clock_set_rap_cr_delay(uref, cr - rap);                            \
    return UBASE_ERR_NONE;                                                  \
}

UREF_CLOCK_SET_RAP(sys)
UREF_CLOCK_SET_RAP(prog)
UREF_CLOCK_SET_RAP(orig)
#undef UREF_CLOCK_SET_RAP

/** @hidden */
#define UREF_CLOCK_REBASE(dv, dt)                                           \
/** @This rebases the dv date as a ##dt.                                    \
 *                                                                          \
 * @param uref uref structure                                               \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_clock_rebase_##dt##_##dv(struct uref *uref)          \
{                                                                           \
    uint64_t date;                                                          \
    UBASE_RETURN(uref_clock_get_##dt##_##dv(uref, &date))                   \
    uref_clock_set_##dt##_##dv(uref, date);                                 \
    return UBASE_ERR_NONE;                                                  \
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

/** @hidden */
#define UREF_CLOCK_CMP(dv, dt)                                              \
/** @This compares the dv dt date of two uref.                              \
 *                                                                          \
 * @param uref1 uref structure                                              \
 * @param uref2 uref structure                                              \
 * @return 0 if both attributes are absent or identical                     \
 */                                                                         \
static inline int uref_clock_cmp_##dt##_##dv(struct uref *uref1,            \
                                             struct uref *uref2)            \
{                                                                           \
    uint64_t v1 = UINT64_MAX, v2 = UINT64_MAX;                              \
    int err1 = uref_clock_get_##dt##_##dv(uref1, &v1);                      \
    int err2 = uref_clock_get_##dt##_##dv(uref2, &v2);                      \
    if (!ubase_check(err1) && !ubase_check(err2))                           \
        return 0;                                                           \
    if (!ubase_check(err1) || !ubase_check(err2))                           \
        return -1;                                                          \
    return v1 == v2 ? 0 : 1;                                                \
}

UREF_CLOCK_CMP(sys, cr)
UREF_CLOCK_CMP(prog, cr)
UREF_CLOCK_CMP(orig, cr)

UREF_CLOCK_CMP(sys, dts)
UREF_CLOCK_CMP(prog, dts)
UREF_CLOCK_CMP(orig, dts)

UREF_CLOCK_CMP(sys, pts)
UREF_CLOCK_CMP(prog, pts)
UREF_CLOCK_CMP(orig, pts)
#undef UREF_CLOCK_CMP

#ifdef __cplusplus
}
#endif
#endif
