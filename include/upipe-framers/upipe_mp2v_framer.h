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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe module building frames from chunks of an ISO 13818-2 stream
 */

#ifndef _UPIPE_FRAMERS_UPIPE_MP2V_FRAMER_H_
/** @hidden */
#define _UPIPE_FRAMERS_UPIPE_MP2V_FRAMER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_MP2VF_SIGNATURE UBASE_FOURCC('m','2','v','f')
/** We only accept the ISO 13818-2 elementary stream. */
#define UPIPE_MP2VF_EXPECTED_FLOW_DEF "block.mpeg2video."

/** @This extends upipe_command with specific commands for mp2v framer. */
enum upipe_mp2vf_command {
    UPIPE_MP2VF_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the current setting for sequence header insertion (int *) */
    UPIPE_MP2VF_GET_SEQUENCE_INSERTION,
    /** sets or unsets the sequence header insertion (int) */
    UPIPE_MP2VF_SET_SEQUENCE_INSERTION
};

/** @This returns the management structure for all mp2vf pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_mp2vf_mgr_alloc(void);

/** @This returns the current setting for sequence header insertion.
 *
 * @param upipe description structure of the pipe
 * @param val_p filled with the current setting
 * @return false in case of error
 */
static inline bool upipe_mp2vf_get_sequence_insertion(struct upipe *upipe,
                                                      bool *val_p)
{
    int val;
    bool ret = upipe_control(upipe, UPIPE_MP2VF_GET_SEQUENCE_INSERTION,
                             UPIPE_MP2VF_SIGNATURE, &val);
    *val_p = !!val;
    return ret;
}

/** @This sets or unsets the sequence header insertion. When true, a sequence
 * headers is inserted in front of every I frame if it is missing, as per
 * ISO-13818-2 specification.
 *
 * @param upipe description structure of the pipe
 * @param val true for sequence header insertion
 * @return false in case of error
 */
static inline bool upipe_mp2vf_set_sequence_insertion(struct upipe *upipe,
                                                      bool val)
{
    return upipe_control(upipe, UPIPE_MP2VF_SET_SEQUENCE_INSERTION,
                         UPIPE_MP2VF_SIGNATURE, val ? 1 : 0);
}

#ifdef __cplusplus
}
#endif
#endif
