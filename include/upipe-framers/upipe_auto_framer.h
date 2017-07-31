/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
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
 * @short Upipe framers automatic detection
 */

#ifndef _UPIPE_FRAMERS_UPIPE_AUTO_FRAMER_H_
/** @hidden */
#define _UPIPE_FRAMERS_UPIPE_AUTO_FRAMER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_AUTOF_SIGNATURE UBASE_FOURCC('a','u','t','f')

/** @This returns the management structure for all auto framers.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_autof_mgr_alloc(void);

/** @This extends upipe_mgr_command with specific commands for autof. */
enum upipe_autof_mgr_command {
    UPIPE_AUTOF_MGR_SENTINEL = UPIPE_MGR_CONTROL_LOCAL,

/** @hidden */
#define UPIPE_AUTOF_MGR_GET_SET_MGR(name, NAME)                             \
    /** returns the current manager for name inner pipes                    \
     * (struct upipe_mgr **) */                                             \
    UPIPE_AUTOF_MGR_GET_##NAME##_MGR,                                       \
    /** sets the manager for name inner pipes (struct upipe_mgr *) */       \
    UPIPE_AUTOF_MGR_SET_##NAME##_MGR,

    UPIPE_AUTOF_MGR_GET_SET_MGR(idem, IDEM)
    UPIPE_AUTOF_MGR_GET_SET_MGR(mpgaf, MPGAF)
    UPIPE_AUTOF_MGR_GET_SET_MGR(a52f, A52F)
    UPIPE_AUTOF_MGR_GET_SET_MGR(mpgvf, MPGVF)
    UPIPE_AUTOF_MGR_GET_SET_MGR(h264f, H264F)
    UPIPE_AUTOF_MGR_GET_SET_MGR(h265f, H265F)
    UPIPE_AUTOF_MGR_GET_SET_MGR(telxf, TELXF)
    UPIPE_AUTOF_MGR_GET_SET_MGR(dvbsubf, DVBSUBF)
    UPIPE_AUTOF_MGR_GET_SET_MGR(opusf, OPUSF)
    UPIPE_AUTOF_MGR_GET_SET_MGR(s302f, S302F)
#undef UPIPE_AUTOF_MGR_GET_SET_MGR
};

/** @hidden */
#define UPIPE_AUTOF_MGR_GET_SET_MGR2(name, NAME)                            \
/** @This returns the current manager for name inner pipes.                 \
 *                                                                          \
 * @param mgr pointer to manager                                            \
 * @param p filled in with the name manager                                 \
 * @return an error code                                                    \
 */                                                                         \
static inline int                                                           \
    upipe_autof_mgr_get_##name##_mgr(struct upipe_mgr *mgr,                 \
                                     struct upipe_mgr *p)                   \
{                                                                           \
    return upipe_mgr_control(mgr, UPIPE_AUTOF_MGR_GET_##NAME##_MGR,         \
                             UPIPE_AUTOF_SIGNATURE, p);                     \
}                                                                           \
/** @This sets the manager for name inner pipes. This may only be called    \
 * before any pipe has been allocated.                                      \
 *                                                                          \
 * @param mgr pointer to manager                                            \
 * @param m pointer to name manager                                         \
 * @return an error code                                                    \
 */                                                                         \
static inline int                                                           \
    upipe_autof_mgr_set_##name##_mgr(struct upipe_mgr *mgr,                 \
                                     struct upipe_mgr *m)                   \
{                                                                           \
    return upipe_mgr_control(mgr, UPIPE_AUTOF_MGR_SET_##NAME##_MGR,         \
                             UPIPE_AUTOF_SIGNATURE, m);                     \
}

UPIPE_AUTOF_MGR_GET_SET_MGR2(idem, IDEM)
UPIPE_AUTOF_MGR_GET_SET_MGR2(mpgaf, MPGAF)
UPIPE_AUTOF_MGR_GET_SET_MGR2(a52f, A52F)
UPIPE_AUTOF_MGR_GET_SET_MGR2(mpgvf, MPGVF)
UPIPE_AUTOF_MGR_GET_SET_MGR2(h264f, H264F)
UPIPE_AUTOF_MGR_GET_SET_MGR2(h265f, H265F)
UPIPE_AUTOF_MGR_GET_SET_MGR2(telxf, TELXF)
UPIPE_AUTOF_MGR_GET_SET_MGR2(dvbsubf, DVBSUBF)
UPIPE_AUTOF_MGR_GET_SET_MGR2(opusf, OPUSF)
UPIPE_AUTOF_MGR_GET_SET_MGR2(s302f, S302F)
#undef UPIPE_AUTOF_MGR_GET_SET_MGR2

#ifdef __cplusplus
}
#endif
#endif
