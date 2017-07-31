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
 * @short Upipe filter processing raw vertical interval analogue data
 */

#ifndef _UPIPE_FILTERS_UPIPE_FILTER_VBI_H_
/** @hidden */
#define _UPIPE_FILTERS_UPIPE_FILTER_VBI_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_VBI_SIGNATURE UBASE_FOURCC('v', 'b', 'i', ' ')
#define UPIPE_VBI_OUTPUT_SIGNATURE UBASE_FOURCC('v', 'b', 'i', 'o')

/** @This extends upipe_command with specific commands for vbi pipes. */
enum upipe_vbi_command {
    UPIPE_VBI_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the ttx subpipe (struct upipe **) */
    UPIPE_VBI_GET_TTX_SUB,
    /** returns the cea708 subpipe (struct upipe **) */
    UPIPE_VBI_GET_CEA708_SUB
};

/** @This returns the management structure for all vbi pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_vbi_mgr_alloc(void);

/** @This returns the ttx subpipe. The refcount is not incremented so you
 * have to use it if you want to keep the pointer.
 *
 * @param upipe description structure of the super pipe
 * @param upipe_p filled in with a pointer to the ttx subpipe
 * @return an error code
 */
static inline int upipe_vbi_get_ttx_sub(struct upipe *upipe,
                                          struct upipe **upipe_p)
{
    return upipe_control(upipe, UPIPE_VBI_GET_TTX_SUB,
                         UPIPE_VBI_SIGNATURE, upipe_p);
}

/** @This returns the cea708 subpipe. The refcount is not incremented so you
 * have to use it if you want to keep the pointer.
 *
 * @param upipe description structure of the super pipe
 * @param upipe_p filled in with a pointer to the cea708 subpipe
 * @return an error code
 */
static inline int upipe_vbi_get_cea708_sub(struct upipe *upipe,
                                            struct upipe **upipe_p)
{
    return upipe_control(upipe, UPIPE_VBI_GET_CEA708_SUB,
                         UPIPE_VBI_SIGNATURE, upipe_p);
}

/** @hidden */
#define ARGS_DECL , struct uprobe *uprobe_ttx, struct uprobe *uprobe_cea708
/** @hidden */
#define ARGS , uprobe_ttx, uprobe_cea708
UPIPE_HELPER_ALLOC(vbi, UPIPE_VBI_SIGNATURE)
#undef ARGS
#undef ARGS_DECL

#ifdef __cplusplus
}
#endif
#endif
