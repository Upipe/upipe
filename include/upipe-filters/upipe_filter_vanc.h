/*
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
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
 * @short Upipe filter processing vertical ancillary data
 *
 * Normative references:
 *  - SMPTE 291M-2006 (ancillary data packet and space formatting)
 */

#ifndef _UPIPE_FILTERS_UPIPE_FILTER_VANC_H_
/** @hidden */
#define _UPIPE_FILTERS_UPIPE_FILTER_VANC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_VANC_SIGNATURE UBASE_FOURCC('v', 'a', 'n', 'c')
#define UPIPE_VANC_OUTPUT_SIGNATURE UBASE_FOURCC('v', 'n', 'c', 'o')

/** @This extends upipe_command with specific commands for vanc pipes. */
enum upipe_vanc_command {
    UPIPE_VANC_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the afd subpipe (struct upipe **) */
    UPIPE_VANC_GET_AFD_SUB,
    /** returns the scte104 subpipe (struct upipe **) */
    UPIPE_VANC_GET_SCTE104_SUB,
    /** returns the op47 subpipe (struct upipe **) */
    UPIPE_VANC_GET_OP47_SUB,
    /** returns the cea708 subpipe (struct upipe **) */
    UPIPE_VANC_GET_CEA708_SUB
};

/** @This returns the management structure for all vanc pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_vanc_mgr_alloc(void);

/** @This returns the afd subpipe. The refcount is not incremented so you
 * have to use it if you want to keep the pointer.
 *
 * @param upipe description structure of the super pipe
 * @param upipe_p filled in with a pointer to the scte104 subpipe
 * @return an error code
 */
static inline int upipe_vanc_get_afd_sub(struct upipe *upipe,
                                         struct upipe **upipe_p)
{
    return upipe_control(upipe, UPIPE_VANC_GET_AFD_SUB,
                         UPIPE_VANC_SIGNATURE, upipe_p);
}

/** @This returns the scte104 subpipe. The refcount is not incremented so you
 * have to use it if you want to keep the pointer.
 *
 * @param upipe description structure of the super pipe
 * @param upipe_p filled in with a pointer to the scte104 subpipe
 * @return an error code
 */
static inline int upipe_vanc_get_scte104_sub(struct upipe *upipe,
                                             struct upipe **upipe_p)
{
    return upipe_control(upipe, UPIPE_VANC_GET_SCTE104_SUB,
                         UPIPE_VANC_SIGNATURE, upipe_p);
}

/** @This returns the op47 subpipe. The refcount is not incremented so you
 * have to use it if you want to keep the pointer.
 *
 * @param upipe description structure of the super pipe
 * @param upipe_p filled in with a pointer to the op47 subpipe
 * @return an error code
 */
static inline int upipe_vanc_get_op47_sub(struct upipe *upipe,
                                          struct upipe **upipe_p)
{
    return upipe_control(upipe, UPIPE_VANC_GET_OP47_SUB,
                         UPIPE_VANC_SIGNATURE, upipe_p);
}

/** @This returns the cea708 subpipe. The refcount is not incremented so you
 * have to use it if you want to keep the pointer.
 *
 * @param upipe description structure of the super pipe
 * @param upipe_p filled in with a pointer to the cea708 subpipe
 * @return an error code
 */
static inline int upipe_vanc_get_cea708_sub(struct upipe *upipe,
                                            struct upipe **upipe_p)
{
    return upipe_control(upipe, UPIPE_VANC_GET_CEA708_SUB,
                         UPIPE_VANC_SIGNATURE, upipe_p);
}

/** @hidden */
#define ARGS_DECL , struct uprobe *uprobe_afd, struct uprobe *uprobe_scte104, struct uprobe *uprobe_op47, struct uprobe *uprobe_cea708
/** @hidden */
#define ARGS , uprobe_afd, uprobe_scte104, uprobe_op47, uprobe_cea708
UPIPE_HELPER_ALLOC(vanc, UPIPE_VANC_SIGNATURE)
#undef ARGS
#undef ARGS_DECL

#ifdef __cplusplus
}
#endif
#endif
