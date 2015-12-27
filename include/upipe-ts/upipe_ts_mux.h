/*
 * Copyright (C) 2013-2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
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
#define UPIPE_TS_MUX_INNER_SINK_SIGNATURE UBASE_FOURCC('t','s','m','S')
#define UPIPE_TS_MUX_PROGRAM_SIGNATURE UBASE_FOURCC('t','s','m','p')
#define UPIPE_TS_MUX_INPUT_SIGNATURE UBASE_FOURCC('t','s','m','i')

/** @This extends uprobe_event with specific events for ts mux. */
enum uprobe_ts_mux_event {
    UPROBE_TS_MUX_SENTINEL = UPROBE_LOCAL,

    /** last continuity counter for an input (unsigned int) */
    UPROBE_TS_MUX_LAST_CC,

    /** ts_encaps events begin here */
    UPROBE_TS_MUX_ENCAPS = UPROBE_LOCAL + 0x1000
};

/** @This defines the modes of multiplexing. */
enum upipe_ts_mux_mode {
    /** constant octetrate */
    UPIPE_TS_MUX_MODE_CBR,
    /** capped octetrate */
    UPIPE_TS_MUX_MODE_CAPPED
};

/** @This returns a string describing the mode.
 *
 * @param mode coded mode
 * @return a constant string describing the mode
 */
static inline const char *
    upipe_ts_mux_mode_print(enum upipe_ts_mux_mode mode)
{
    switch (mode) {
        case UPIPE_TS_MUX_MODE_CBR: return "CBR";
        case UPIPE_TS_MUX_MODE_CAPPED: return "Capped VBR";
        default: return "unknown";
    }
}

/** @This extends upipe_command with specific commands for ts mux. */
enum upipe_ts_mux_command {
    UPIPE_TS_MUX_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the current conformance (int *) */
    UPIPE_TS_MUX_GET_CONFORMANCE,
    /** sets the conformance (int) */
    UPIPE_TS_MUX_SET_CONFORMANCE,
    /** returns the current continuity counter (unsigned int *) */
    UPIPE_TS_MUX_GET_CC,
    /** sets the continuity counter (unsigned int) */
    UPIPE_TS_MUX_SET_CC,
    /** sets the initial cr_prog of the next access unit (uint64_t) */
    UPIPE_TS_MUX_SET_CR_PROG,
    /** returns the current PAT interval (uint64_t *) */
    UPIPE_TS_MUX_GET_PAT_INTERVAL,
    /** sets the PAT interval (uint64_t) */
    UPIPE_TS_MUX_SET_PAT_INTERVAL,
    /** returns the current PMT interval (uint64_t *) */
    UPIPE_TS_MUX_GET_PMT_INTERVAL,
    /** sets the PMT interval (uint64_t) */
    UPIPE_TS_MUX_SET_PMT_INTERVAL,
    /** returns the current NIT interval (uint64_t *) */
    UPIPE_TS_MUX_GET_NIT_INTERVAL,
    /** sets the NIT interval (uint64_t) */
    UPIPE_TS_MUX_SET_NIT_INTERVAL,
    /** returns the current SDT interval (uint64_t *) */
    UPIPE_TS_MUX_GET_SDT_INTERVAL,
    /** sets the SDT interval (uint64_t) */
    UPIPE_TS_MUX_SET_SDT_INTERVAL,
    /** returns the current EIT interval (uint64_t *) */
    UPIPE_TS_MUX_GET_EIT_INTERVAL,
    /** sets the EIT interval (uint64_t) */
    UPIPE_TS_MUX_SET_EIT_INTERVAL,
    /** returns the current TDT interval (uint64_t *) */
    UPIPE_TS_MUX_GET_TDT_INTERVAL,
    /** sets the TDT interval (uint64_t) */
    UPIPE_TS_MUX_SET_TDT_INTERVAL,
    /** returns the current PCR interval (uint64_t *) */
    UPIPE_TS_MUX_GET_PCR_INTERVAL,
    /** sets the PCR interval (uint64_t) */
    UPIPE_TS_MUX_SET_PCR_INTERVAL,
    /** returns the current SCTE35 interval (uint64_t *) */
    UPIPE_TS_MUX_GET_SCTE35_INTERVAL,
    /** sets the SCTE35 interval (uint64_t) */
    UPIPE_TS_MUX_SET_SCTE35_INTERVAL,
    /** returns the current maximum retention delay (uint64_t *) */
    UPIPE_TS_MUX_GET_MAX_DELAY,
    /** sets the maximum retention delay (uint64_t) */
    UPIPE_TS_MUX_SET_MAX_DELAY,
    /** returns the current muxing delay (uint64_t *) */
    UPIPE_TS_MUX_GET_MUX_DELAY,
    /** sets the muxing delay (uint64_t) */
    UPIPE_TS_MUX_SET_MUX_DELAY,
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
    /** returns the current version number of the table (unsigned int *) */
    UPIPE_TS_MUX_GET_VERSION,
    /** sets the version number of the table (unsigned int) */
    UPIPE_TS_MUX_SET_VERSION,
    /** stops updating a PSI table upon sub removal */
    UPIPE_TS_MUX_FREEZE_PSI,
    /** prepares the next access unit/section for the given date
     * (uint64_t, uint64_t) */
    UPIPE_TS_MUX_PREPARE,

    /** ts_encaps commands begin here */
    UPIPE_TS_MUX_ENCAPS = UPIPE_CONTROL_LOCAL + 0x1000,
    /** ts_psig commands begin here */
    UPIPE_TS_MUX_PSIG = UPIPE_CONTROL_LOCAL + 0x2000,
    /** ts_psig_program commands begin here */
    UPIPE_TS_MUX_PSIG_PROGRAM = UPIPE_CONTROL_LOCAL + 0x3000,
    /** ts_sig commands begin here */
    UPIPE_TS_MUX_SIG = UPIPE_CONTROL_LOCAL + 0x4000
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

/** @This returns the current continuity counter.
 *
 * @param upipe description structure of the pipe
 * @param cc_p filled in with the countinuity counter
 * @return an error code
 */
static inline int upipe_ts_mux_get_cc(struct upipe *upipe, unsigned int *cc_p)
{
    return upipe_control(upipe, UPIPE_TS_MUX_GET_CC,
                         UPIPE_TS_MUX_SIGNATURE, cc_p);
}

/** @This sets the continuity counter.
 *
 * @param upipe description structure of the pipe
 * @param cc new continuity counter
 * @return an error code
 */
static inline int upipe_ts_mux_set_cc(struct upipe *upipe, unsigned int cc)
{
    return upipe_control(upipe, UPIPE_TS_MUX_SET_CC,
                         UPIPE_TS_MUX_SIGNATURE, cc);
}

/** @This sets the cr_prog of the next access unit.
 *
 * @param upipe description structure of the pipe
 * @param cr_prog cr_prog of the next access unit
 * @return an error code
 */
static inline int upipe_ts_mux_set_cr_prog(struct upipe *upipe,
                                           uint64_t cr_prog)
{
    return upipe_control(upipe, UPIPE_TS_MUX_SET_CR_PROG,
                         UPIPE_TS_MUX_SIGNATURE, cr_prog);
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

/** @This returns the current NIT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return an error code
 */
static inline int upipe_ts_mux_get_nit_interval(struct upipe *upipe,
                                                uint64_t *interval_p)
{
    return upipe_control(upipe, UPIPE_TS_MUX_GET_NIT_INTERVAL,
                         UPIPE_TS_MUX_SIGNATURE, interval_p);
}

/** @This sets the NIT interval. It takes effect at the end of the current
 * period. It may also be called on a program subpipe.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return an error code
 */
static inline int upipe_ts_mux_set_nit_interval(struct upipe *upipe,
                                                uint64_t interval)
{
    return upipe_control(upipe, UPIPE_TS_MUX_SET_NIT_INTERVAL,
                         UPIPE_TS_MUX_SIGNATURE, interval);
}

/** @This returns the current SDT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return an error code
 */
static inline int upipe_ts_mux_get_sdt_interval(struct upipe *upipe,
                                                uint64_t *interval_p)
{
    return upipe_control(upipe, UPIPE_TS_MUX_GET_SDT_INTERVAL,
                         UPIPE_TS_MUX_SIGNATURE, interval_p);
}

/** @This sets the SDT interval. It takes effect at the end of the current
 * period. It may also be called on a program subpipe.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return an error code
 */
static inline int upipe_ts_mux_set_sdt_interval(struct upipe *upipe,
                                                uint64_t interval)
{
    return upipe_control(upipe, UPIPE_TS_MUX_SET_SDT_INTERVAL,
                         UPIPE_TS_MUX_SIGNATURE, interval);
}

/** @This returns the current EIT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return an error code
 */
static inline int upipe_ts_mux_get_eit_interval(struct upipe *upipe,
                                                uint64_t *interval_p)
{
    return upipe_control(upipe, UPIPE_TS_MUX_GET_EIT_INTERVAL,
                         UPIPE_TS_MUX_SIGNATURE, interval_p);
}

/** @This sets the EIT interval. It takes effect at the end of the current
 * period. It may also be called on a program subpipe.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return an error code
 */
static inline int upipe_ts_mux_set_eit_interval(struct upipe *upipe,
                                                uint64_t interval)
{
    return upipe_control(upipe, UPIPE_TS_MUX_SET_EIT_INTERVAL,
                         UPIPE_TS_MUX_SIGNATURE, interval);
}

/** @This returns the current TDT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return an error code
 */
static inline int upipe_ts_mux_get_tdt_interval(struct upipe *upipe,
                                                uint64_t *interval_p)
{
    return upipe_control(upipe, UPIPE_TS_MUX_GET_TDT_INTERVAL,
                         UPIPE_TS_MUX_SIGNATURE, interval_p);
}

/** @This sets the TDT interval. It takes effect at the end of the current
 * period. It may also be called on a program subpipe.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return an error code
 */
static inline int upipe_ts_mux_set_tdt_interval(struct upipe *upipe,
                                                uint64_t interval)
{
    return upipe_control(upipe, UPIPE_TS_MUX_SET_TDT_INTERVAL,
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

/** @This returns the current SCTE35 interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return an error code
 */
static inline int upipe_ts_mux_get_scte35_interval(struct upipe *upipe,
                                                   uint64_t *interval_p)
{
    return upipe_control(upipe, UPIPE_TS_MUX_GET_SCTE35_INTERVAL,
                         UPIPE_TS_MUX_SIGNATURE, interval_p);
}

/** @This sets the SCTE35 interval.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return an error code
 */
static inline int upipe_ts_mux_set_scte35_interval(struct upipe *upipe,
                                                   uint64_t interval)
{
    return upipe_control(upipe, UPIPE_TS_MUX_SET_SCTE35_INTERVAL,
                         UPIPE_TS_MUX_SIGNATURE, interval);
}

/** @This returns the current maximum retention delay.
 *
 * @param upipe description structure of the pipe
 * @param delay_p filled in with the delay
 * @return an error code
 */
static inline int upipe_ts_mux_get_max_delay(struct upipe *upipe,
                                             uint64_t *delay_p)
{
    return upipe_control(upipe, UPIPE_TS_MUX_GET_MAX_DELAY,
                         UPIPE_TS_MUX_SIGNATURE, delay_p);
}

/** @This sets the maximum retention delay. It may also be called on an input
 * subpipe.
 *
 * @param upipe description structure of the pipe
 * @param delay new delay
 * @return an error code
 */
static inline int upipe_ts_mux_set_max_delay(struct upipe *upipe,
                                             uint64_t delay)
{
    return upipe_control(upipe, UPIPE_TS_MUX_SET_MAX_DELAY,
                         UPIPE_TS_MUX_SIGNATURE, delay);
}

/** @This returns the current mux delay (live mode).
 *
 * @param upipe description structure of the pipe
 * @param delay_p filled in with the delay
 * @return an error code
 */
static inline int upipe_ts_mux_get_mux_delay(struct upipe *upipe,
                                             uint64_t *delay_p)
{
    return upipe_control(upipe, UPIPE_TS_MUX_GET_MUX_DELAY,
                         UPIPE_TS_MUX_SIGNATURE, delay_p);
}

/** @This sets the mux delay (live mode).
 *
 * @param upipe description structure of the pipe
 * @param delay new delay
 * @return an error code
 */
static inline int upipe_ts_mux_set_mux_delay(struct upipe *upipe,
                                             uint64_t delay)
{
    return upipe_control(upipe, UPIPE_TS_MUX_SET_MUX_DELAY,
                         UPIPE_TS_MUX_SIGNATURE, delay);
}

/** @This returns the current mux octetrate.
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

/** @This sets the mux octetrate.
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

/** @This returns the current mode.
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

/** @This sets the mode.
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

/** @This returns the current version of the PSI table. It may also be called on
 * upipe_ts_psi_generator.
 *
 * @param upipe description structure of the pipe
 * @param version_p filled in with the version
 * @return an error code
 */
static inline int upipe_ts_mux_get_version(struct upipe *upipe,
                                           unsigned int *version_p)
{
    return upipe_control(upipe, UPIPE_TS_MUX_GET_VERSION,
                         UPIPE_TS_MUX_SIGNATURE, version_p);
}

/** @This sets the version of the PSI table. It may also be called on
 * upipe_ts_psi_generator.
 *
 * @param upipe description structure of the pipe
 * @param version new version
 * @return an error code
 */
static inline int upipe_ts_mux_set_version(struct upipe *upipe,
                                           unsigned int version)
{
    return upipe_control(upipe, UPIPE_TS_MUX_SET_VERSION,
                         UPIPE_TS_MUX_SIGNATURE, version);
}

/** @This stops updating a PSI table upon sub removal.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static inline int upipe_ts_mux_freeze_psi(struct upipe *upipe)
{
    return upipe_control(upipe, UPIPE_TS_MUX_FREEZE_PSI,
                         UPIPE_TS_MUX_SIGNATURE);
}

/** @This prepares the access unit/section for the given date.
 *
 * @param upipe description structure of the pipe
 * @param cr_sys current muxing date
 * @param latency latency before the packet is output
 * @return an error code
 */
static inline int upipe_ts_mux_prepare(struct upipe *upipe, uint64_t cr_sys,
                                       uint64_t latency)
{
    return upipe_control_nodbg(upipe, UPIPE_TS_MUX_PREPARE,
                               UPIPE_TS_MUX_SIGNATURE, cr_sys, latency);
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

    UPIPE_TS_MUX_MGR_GET_SET_MGR(ts_encaps, TS_ENCAPS)
    UPIPE_TS_MUX_MGR_GET_SET_MGR(ts_tstd, TS_TSTD)
    UPIPE_TS_MUX_MGR_GET_SET_MGR(ts_psi_join, TS_PSI_JOIN)
    UPIPE_TS_MUX_MGR_GET_SET_MGR(ts_psig, TS_PSIG)
    UPIPE_TS_MUX_MGR_GET_SET_MGR(ts_sig, TS_SIG)
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

UPIPE_TS_MUX_MGR_GET_SET_MGR2(ts_encaps, TS_ENCAPS)
UPIPE_TS_MUX_MGR_GET_SET_MGR2(ts_tstd, TS_TSTD)
UPIPE_TS_MUX_MGR_GET_SET_MGR2(ts_psi_join, TS_PSI_JOIN)
UPIPE_TS_MUX_MGR_GET_SET_MGR2(ts_psig, TS_PSIG)
UPIPE_TS_MUX_MGR_GET_SET_MGR2(ts_sig, TS_SIG)
#undef UPIPE_TS_MUX_MGR_GET_SET_MGR2

#ifdef __cplusplus
}
#endif
#endif
