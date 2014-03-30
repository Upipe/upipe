/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe higher-level module muxing elementary streams in a TS
 */

#ifndef _UPIPE_TS_UPIPE_TS_MUX_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_MUX_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>
#include <upipe-ts/upipe_ts.h>

#define UPIPE_TS_MUX_SIGNATURE UBASE_FOURCC('t','s','m','x')
#define UPIPE_TS_MUX_PROGRAM_SIGNATURE UBASE_FOURCC('t','s','m','p')
#define UPIPE_TS_MUX_INPUT_SIGNATURE UBASE_FOURCC('t','s','m','i')

/** @This defines the modes of multiplexing, by order of strictness. */
enum upipe_ts_mux_mode {
    /** variable octetrate */
    UPIPE_TS_MUX_MODE_VBR,
    /** capped octetrate, drop extra packets */
    UPIPE_TS_MUX_MODE_CAPPED,
    /** constant octetrate, drop extra packets */
    UPIPE_TS_MUX_MODE_CBR
};

/** @This extends upipe_command with specific commands for ts mux. */
enum upipe_ts_mux_command {
    UPIPE_TS_MUX_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the current conformance (int *) */
    UPIPE_TS_MUX_GET_CONFORMANCE,
    /** sets the conformance (int) */
    UPIPE_TS_MUX_SET_CONFORMANCE,
    /** returns the current PAT interval (uint64_t *) */
    UPIPE_TS_MUX_GET_PAT_INTERVAL,
    /** sets the PAT interval (uint64_t) */
    UPIPE_TS_MUX_SET_PAT_INTERVAL,
    /** returns the current PMT interval (uint64_t *) */
    UPIPE_TS_MUX_GET_PMT_INTERVAL,
    /** sets the PMT interval (uint64_t) */
    UPIPE_TS_MUX_SET_PMT_INTERVAL,
    /** returns the current PCR interval (uint64_t *) */
    UPIPE_TS_MUX_GET_PCR_INTERVAL,
    /** sets the PCR interval (uint64_t) */
    UPIPE_TS_MUX_SET_PCR_INTERVAL,
    /** returns the current mux octetrate (uint64_t *) */
    UPIPE_TS_MUX_GET_OCTETRATE,
    /** sets the mux octetrate (uint64_t) */
    UPIPE_TS_MUX_SET_OCTETRATE,
    /** returns the current padding octetrate (uint64_t *) */
    UPIPE_TS_MUX_GET_PADDING_OCTETRATE,
    /** sets the padding octetrate (uint64_t) */
    UPIPE_TS_MUX_SET_PADDING_OCTETRATE,
    /** returns the current mode (int *) */
    UPIPE_TS_MUX_GET_MODE,
    /** sets the mode (int) */
    UPIPE_TS_MUX_SET_MODE,
    /** returns the configured MTU (unsigned int *) */
    UPIPE_TS_MUX_GET_MTU,
    /** sets the MTU (unsigned int) */
    UPIPE_TS_MUX_SET_MTU
};

/** @This returns the current conformance mode. It cannot return
 * UPIPE_TS_CONFORMANCE_AUTO.
 *
 * @param upipe description structure of the pipe
 * @param conformance_p filled in with the conformance
 * @return an error code
 */
static inline int
    upipe_ts_mux_get_conformance(struct upipe *upipe,
                                 enum upipe_ts_conformance *conformance_p)
{
    return upipe_control(upipe, UPIPE_TS_MUX_GET_CONFORMANCE,
                         UPIPE_TS_MUX_SIGNATURE, conformance_p);
}

/** @This sets the conformance mode.
 *
 * @param upipe description structure of the pipe
 * @param conformance conformance mode
 * @return an error code
 */
static inline int
    upipe_ts_mux_set_conformance(struct upipe *upipe,
                                 enum upipe_ts_conformance conformance)
{
    return upipe_control(upipe, UPIPE_TS_MUX_SET_CONFORMANCE,
                         UPIPE_TS_MUX_SIGNATURE, conformance);
}

/** @This returns the current PAT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return an error code
 */
static inline int upipe_ts_mux_get_pat_interval(struct upipe *upipe,
                                                uint64_t *interval_p)
{
    return upipe_control(upipe, UPIPE_TS_MUX_GET_PAT_INTERVAL,
                         UPIPE_TS_MUX_SIGNATURE, interval_p);
}

/** @This sets the PAT interval. It takes effect at the end of the current
 * period.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return an error code
 */
static inline int upipe_ts_mux_set_pat_interval(struct upipe *upipe,
                                                uint64_t interval)
{
    return upipe_control(upipe, UPIPE_TS_MUX_SET_PAT_INTERVAL,
                         UPIPE_TS_MUX_SIGNATURE, interval);
}

/** @This returns the current PMT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return an error code
 */
static inline int upipe_ts_mux_get_pmt_interval(struct upipe *upipe,
                                                uint64_t *interval_p)
{
    return upipe_control(upipe, UPIPE_TS_MUX_GET_PMT_INTERVAL,
                         UPIPE_TS_MUX_SIGNATURE, interval_p);
}

/** @This sets the PMT interval. It takes effect at the end of the current
 * period. It may also be called on a program subpipe.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return an error code
 */
static inline int upipe_ts_mux_set_pmt_interval(struct upipe *upipe,
                                                uint64_t interval)
{
    return upipe_control(upipe, UPIPE_TS_MUX_SET_PMT_INTERVAL,
                         UPIPE_TS_MUX_SIGNATURE, interval);
}

/** @This returns the current PCR interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return an error code
 */
static inline int upipe_ts_mux_get_pcr_interval(struct upipe *upipe,
                                                uint64_t *interval_p)
{
    return upipe_control(upipe, UPIPE_TS_MUX_GET_PCR_INTERVAL,
                         UPIPE_TS_MUX_SIGNATURE, interval_p);
}

/** @This sets the PCR interval. It may also be called on a program subpipe.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return an error code
 */
static inline int upipe_ts_mux_set_pcr_interval(struct upipe *upipe,
                                                uint64_t interval)
{
    return upipe_control(upipe, UPIPE_TS_MUX_SET_PCR_INTERVAL,
                         UPIPE_TS_MUX_SIGNATURE, interval);
}

/** @This returns the current mux octetrate. It may also be called on
 * upipe_ts_aggregate.
 *
 * @param upipe description structure of the pipe
 * @param octetrate_p filled in with the octetrate
 * @return an error code
 */
static inline int upipe_ts_mux_get_octetrate(struct upipe *upipe,
                                             uint64_t *octetrate_p)
{
    return upipe_control(upipe, UPIPE_TS_MUX_GET_OCTETRATE,
                         UPIPE_TS_MUX_SIGNATURE, octetrate_p);
}

/** @This sets the mux octetrate. It may also be called on upipe_ts_aggregate.
 *
 * @param upipe description structure of the pipe
 * @param octetrate new octetrate
 * @return an error code
 */
static inline int upipe_ts_mux_set_octetrate(struct upipe *upipe,
                                             uint64_t octetrate)
{
    return upipe_control(upipe, UPIPE_TS_MUX_SET_OCTETRATE,
                         UPIPE_TS_MUX_SIGNATURE, octetrate);
}

/** @This returns the current padding octetrate.
 *
 * @param upipe description structure of the pipe
 * @param octetrate_p filled in with the octetrate
 * @return an error code
 */
static inline int
    upipe_ts_mux_get_padding_octetrate(struct upipe *upipe,
                                       uint64_t *octetrate_p)
{
    return upipe_control(upipe, UPIPE_TS_MUX_GET_PADDING_OCTETRATE,
                         UPIPE_TS_MUX_SIGNATURE, octetrate_p);
}

/** @This sets the padding octetrate.
 *
 * @param upipe description structure of the pipe
 * @param octetrate new octetrate
 * @return an error code
 */
static inline int
    upipe_ts_mux_set_padding_octetrate(struct upipe *upipe, uint64_t octetrate)
{
    return upipe_control(upipe, UPIPE_TS_MUX_SET_PADDING_OCTETRATE,
                         UPIPE_TS_MUX_SIGNATURE, octetrate);
}

/** @This returns the current mode. It may also be called on
 * upipe_ts_aggregate.
 *
 * @param upipe description structure of the pipe
 * @param mode_p filled in with the mode
 * @return an error code
 */
static inline int
    upipe_ts_mux_get_mode(struct upipe *upipe, enum upipe_ts_mux_mode *mode_p)
{
    return upipe_control(upipe, UPIPE_TS_MUX_GET_MODE,
                         UPIPE_TS_MUX_SIGNATURE, mode_p);
}

/** @This sets the mode. It may also be called on upipe_ts_aggregate.
 *
 * @param upipe description structure of the pipe
 * @param mode new mode
 * @return an error code
 */
static inline int upipe_ts_mux_set_mode(struct upipe *upipe,
                                        enum upipe_ts_mux_mode mode)
{
    return upipe_control(upipe, UPIPE_TS_MUX_SET_MODE,
                         UPIPE_TS_MUX_SIGNATURE, mode);
}

/** @This returns the configured mtu of TS packets. It may also be called on
 * upipe_ts_aggregate.
 *
 * @param upipe description structure of the pipe
 * @param mtu_p filled in with the configured mtu, in octets
 * @return an error code
 */
static inline int upipe_ts_mux_get_mtu(struct upipe *upipe, unsigned int *mtu_p)
{
    return upipe_control(upipe, UPIPE_TS_MUX_GET_MTU,
                         UPIPE_TS_MUX_SIGNATURE, mtu_p);
}

/** @This sets the configured mtu of TS packets. It may also be called on
 * upipe_ts_aggregate.
 *
 * @param upipe description structure of the pipe
 * @param mtu configured mtu, in octets
 * @return an error code
 */
static inline int upipe_ts_mux_set_mtu(struct upipe *upipe, unsigned int mtu)
{
    return upipe_control(upipe, UPIPE_TS_MUX_SET_MTU,
                         UPIPE_TS_MUX_SIGNATURE, mtu);
}

/** @This returns the management structure for all ts_mux pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_mux_mgr_alloc(void);

/** @This is a list of specific commands for ts mux managers. */
enum upipe_ts_mux_mgr_command {
/** @hidden */
#define UPIPE_TS_MUX_MGR_GET_SET_MGR(name, NAME)                            \
    /** returns the current manager for name subpipes                       \
     * (struct upipe_mgr **) */                                             \
    UPIPE_TS_MUX_MGR_GET_##NAME##_MGR,                                      \
    /** sets the manager for name subpipes (struct upipe_mgr *) */          \
    UPIPE_TS_MUX_MGR_SET_##NAME##_MGR,

    UPIPE_TS_MUX_MGR_GET_SET_MGR(ts_agg, TS_AGG)
    UPIPE_TS_MUX_MGR_GET_SET_MGR(ts_join, TS_JOIN)
    UPIPE_TS_MUX_MGR_GET_SET_MGR(ts_encaps, TS_ENCAPS)
    UPIPE_TS_MUX_MGR_GET_SET_MGR(ts_pese, TS_PESE)
    UPIPE_TS_MUX_MGR_GET_SET_MGR(ts_psig, TS_PSIG)
    UPIPE_TS_MUX_MGR_GET_SET_MGR(ts_psii, TS_PSII)
#undef UPIPE_TS_MUX_MGR_GET_SET_MGR
};


/** @hidden */
#define UPIPE_TS_MUX_MGR_GET_SET_MGR2(name, NAME)                           \
/** @This returns the current manager for name subpipes.                    \
 *                                                                          \
 * @param mgr pointer to manager                                            \
 * @param p filled in with the name manager                                 \
 * @return an error code                                                    \
 */                                                                         \
static inline int                                                           \
    upipe_ts_mux_mgr_get_##name##_mgr(struct upipe_mgr *mgr,                \
                                      struct upipe_mgr *p)                  \
{                                                                           \
    return upipe_mgr_control(mgr, UPIPE_TS_MUX_MGR_GET_##NAME##_MGR,        \
                             UPIPE_TS_MUX_SIGNATURE, p);                    \
}                                                                           \
/** @This sets the manager for name subpipes.                               \
 *                                                                          \
 * @param mgr pointer to manager                                            \
 * @param m pointer to name manager                                         \
 * @return an error code                                                    \
 */                                                                         \
static inline int                                                           \
    upipe_ts_mux_mgr_set_##name##_mgr(struct upipe_mgr *mgr,                \
                                      struct upipe_mgr *m)                  \
{                                                                           \
    return upipe_mgr_control(mgr, UPIPE_TS_MUX_MGR_SET_##NAME##_MGR,        \
                             UPIPE_TS_MUX_SIGNATURE, m);                    \
}

UPIPE_TS_MUX_MGR_GET_SET_MGR2(ts_join, TS_JOIN)
UPIPE_TS_MUX_MGR_GET_SET_MGR2(ts_agg, TS_AGG)
UPIPE_TS_MUX_MGR_GET_SET_MGR2(ts_encaps, TS_ENCAPS)
UPIPE_TS_MUX_MGR_GET_SET_MGR2(ts_pese, TS_PESE)
UPIPE_TS_MUX_MGR_GET_SET_MGR2(ts_psig, TS_PSIG)
UPIPE_TS_MUX_MGR_GET_SET_MGR2(ts_psii, TS_PSII)
#undef UPIPE_TS_MUX_MGR_GET_SET_MGR2

#ifdef __cplusplus
}
#endif
#endif
