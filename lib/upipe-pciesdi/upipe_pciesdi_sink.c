/*
 * Copyright (C) 2018 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe source module PCIE SDI cards sink
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/upipe.h>
#include <upipe/ulist.h>
#include <upipe/udict.h>
#include <upipe/upump.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe-pciesdi/upipe_pciesdi_sink.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "libsdi.h"
#include "sdi_config.h"
#include "csr.h"

/** upipe_pciesdi_sink structure */
struct upipe_pciesdi_sink {
    /** refcount management structure */
    struct urefcount urefcount;

    /** file descriptor */
    int fd;

    /** urefs */
    struct uchain urefs;
    struct uref *uref;
    size_t written;

    int first;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** read watcher */
    struct upump *upump;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_pciesdi_sink, upipe, UPIPE_PCIESDI_SINK_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_pciesdi_sink, urefcount, upipe_pciesdi_sink_free)
UPIPE_HELPER_VOID(upipe_pciesdi_sink);
UPIPE_HELPER_UPUMP_MGR(upipe_pciesdi_sink, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_pciesdi_sink, upump, upump_mgr)

/** @internal @This allocates a null pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_pciesdi_sink_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_pciesdi_sink_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_pciesdi_sink *upipe_pciesdi_sink = upipe_pciesdi_sink_from_upipe(upipe);
    upipe_pciesdi_sink_init_urefcount(upipe);
    upipe_pciesdi_sink_init_upump_mgr(upipe);
    upipe_pciesdi_sink_init_upump(upipe);
    upipe_pciesdi_sink_check_upump_mgr(upipe);

    upipe_pciesdi_sink->fd = -1;
    ulist_init(&upipe_pciesdi_sink->urefs);
    upipe_pciesdi_sink->uref = NULL;
    upipe_pciesdi_sink->first = 1;

    upipe_throw_ready(&upipe_pciesdi_sink->upipe);

    return upipe;
}

/** @internal
 */
static void upipe_pciesdi_sink_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_pciesdi_sink *upipe_pciesdi_sink = upipe_pciesdi_sink_from_upipe(upipe);

    struct uref *uref = upipe_pciesdi_sink->uref;
    if (!uref) {
        struct uchain *uchain = ulist_pop(&upipe_pciesdi_sink->urefs);
        if (!uchain) {
//            upipe_err(upipe, "underrun");
            return;
        }
        uref = uref_from_uchain(uchain);
        upipe_pciesdi_sink->uref = uref;
        upipe_pciesdi_sink->written = 0;
    }

    size_t size = 0;
    uref_block_size(uref, &size);

    const uint8_t *buf;
    int s = -1;
    if (!ubase_check(uref_block_read(uref, upipe_pciesdi_sink->written, &s, &buf))) {
        upipe_err(upipe, "could not read");
        return;
    }

    int64_t hw = 0, sw = 0;
    sdi_dma_reader(upipe_pciesdi_sink->fd, upipe_pciesdi_sink->first == 0, &hw, &sw); // enable

    ssize_t ret = write(upipe_pciesdi_sink->fd, buf, s);
    if (ret < 0) {
        upipe_err_va(upipe, "%m");
        return;
    } else if (ret < s) {
        //upipe_warn_va(upipe, "%zd/%u", ret, s);
    }
    if (ret > 0) {
        upipe_pciesdi_sink->first = 0;
        sdi_dma_reader(upipe_pciesdi_sink->fd, upipe_pciesdi_sink->first == 0, &hw, &sw); // enable
    }

    uref_block_unmap(uref, upipe_pciesdi_sink->written);

    upipe_pciesdi_sink->written += ret;
    if (upipe_pciesdi_sink->written == size) {
        uref_free(uref);
        upipe_pciesdi_sink->uref = NULL;
    }
}

/** @internal
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_pciesdi_sink_input(struct upipe *upipe, struct uref *uref, struct upump **upump_p)
{
    struct upipe_pciesdi_sink *upipe_pciesdi_sink = upipe_pciesdi_sink_from_upipe(upipe);

    ulist_add(&upipe_pciesdi_sink->urefs, uref_to_uchain(uref));
    size_t n = ulist_depth(&upipe_pciesdi_sink->urefs);
    upipe_dbg_va(upipe, "Buffered %zu urefs", n);

    if (upipe_pciesdi_sink->upump || n < 2)
        return;

    int64_t hw = 0, sw = 0;
    sdi_dma_reader(upipe_pciesdi_sink->fd, upipe_pciesdi_sink->first == 0, &hw, &sw);

    struct upump *upump = upump_alloc_fd_write(upipe_pciesdi_sink->upump_mgr,
            upipe_pciesdi_sink_worker, upipe,
            upipe->refcount, upipe_pciesdi_sink->fd);
    if (unlikely(upump == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
        return;
    }
    upipe_pciesdi_sink_set_upump(upipe, upump);
    upump_start(upump);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_pciesdi_sink_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    struct upipe_pciesdi_sink *upipe_pciesdi_sink = upipe_pciesdi_sink_from_upipe(upipe);
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    UBASE_RETURN(uref_flow_match_def(flow_def, "block."));

    return UBASE_ERR_NONE;
}

/** @internal @This asks to open the given device.
 *
 * @param upipe description structure of the pipe
 * @param path relative or absolute path of the file
 * @return an error code
 */
static int upipe_pciesdi_set_uri(struct upipe *upipe, const char *path)
{
    struct upipe_pciesdi_sink *upipe_pciesdi_sink = upipe_pciesdi_sink_from_upipe(upipe);

    upipe_pciesdi_sink_check_upump_mgr(upipe);
    if (upipe_pciesdi_sink->upump_mgr == NULL)
        return UBASE_ERR_BUSY;

    ubase_clean_fd(&upipe_pciesdi_sink->fd);
    upipe_pciesdi_sink_set_upump(upipe, NULL);

    upipe_pciesdi_sink->fd = open(path, O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (unlikely(upipe_pciesdi_sink->fd < 0)) {
        upipe_err_va(upipe, "can't open %s (%m)", path);
        return UBASE_ERR_EXTERNAL;
    }

    sdi_set_pattern(upipe_pciesdi_sink->fd, 0, 0);

    sdi_writel(upipe_pciesdi_sink->fd, CSR_SDI_REFCLK_SEL_OUT_ADDR, 1);

    sdi_dma(upipe_pciesdi_sink->fd, 1, 0, 0); // disable loopback

    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_pciesdi_sink_control(struct upipe *upipe, int command, va_list args)
{
    struct upipe_pciesdi_sink *upipe_pciesdi_sink = upipe_pciesdi_sink_from_upipe(upipe);

    switch (command) {
        case UPIPE_REGISTER_REQUEST:
        case UPIPE_UNREGISTER_REQUEST:
            return upipe_control_provide_request(upipe, command, args);

        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_pciesdi_sink_set_upump(upipe, NULL);
            return upipe_pciesdi_sink_attach_upump_mgr(upipe);
/*
        case UPIPE_ATTACH_UCLOCK:
           upipe_pciesdi_sink_set_upump(upipe, NULL);
           upipe_pciesdi_sink_require_uclock(upipe);
           return UBASE_ERR_NONE;
*/

        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow = va_arg(args, struct uref *);
            return upipe_pciesdi_sink_set_flow_def(upipe, flow);
        }

        case UPIPE_SET_URI: {
            const char *path = va_arg(args, const char *);
            return upipe_pciesdi_set_uri(upipe, path);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_pciesdi_sink_free(struct upipe *upipe)
{
    struct upipe_pciesdi_sink *upipe_pciesdi_sink = upipe_pciesdi_sink_from_upipe(upipe);
    upipe_throw_dead(upipe);
    ubase_clean_fd(&upipe_pciesdi_sink->fd);

    uref_free(upipe_pciesdi_sink->uref);
    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach(&upipe_pciesdi_sink->urefs, uchain, uchain_tmp) {
        uref_free(uref_from_uchain(uchain));
        ulist_delete(uchain);
    }

    upipe_pciesdi_sink_clean_upump(upipe);
    upipe_pciesdi_sink_clean_upump_mgr(upipe);
    upipe_pciesdi_sink_clean_urefcount(upipe);
    upipe_pciesdi_sink_free_void(upipe);
}

/** upipe_pciesdi_sink (/dev/null) */
static struct upipe_mgr upipe_pciesdi_sink_mgr = {
    .refcount = NULL,
    .signature = UPIPE_PCIESDI_SINK_SIGNATURE,

    .upipe_alloc = upipe_pciesdi_sink_alloc,
    .upipe_input = upipe_pciesdi_sink_input,
    .upipe_control = upipe_pciesdi_sink_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for null pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_pciesdi_sink_mgr_alloc(void)
{
    return &upipe_pciesdi_sink_mgr;
}
