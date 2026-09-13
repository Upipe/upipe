/*
 * Copyright (C) 2026 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module decapsulating DVB TTML subtitle segments
 *
 * The PES data fields are framed and CRC checked by @ref upipe_ttmlf_mgr_alloc.
 *
 * Normative references:
 *  - ETSI EN 303 560 V1.1.1 (2018-05) (TTML subtitling systems)
 */

#include "config.h"

#include "upipe/uclock.h"
#include "upipe/uref.h"
#include "upipe/uref_block.h"
#include "upipe/uref_clock.h"
#include "upipe/uref_flow.h"
#include "upipe/ubuf_block.h"
#include "upipe/upipe.h"
#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_void.h"
#include "upipe/upipe_helper_output.h"
#include "upipe/upipe_helper_ubuf_mgr.h"
#include "upipe/upipe_helper_input.h"
#include "upipe-ts/upipe_ts_ttml_decaps.h"
#include "upipe-ts/uref_ts_ttml.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>

#ifdef HAVE_ZLIB
#include <zlib.h>
#endif

/** we only accept DVB TTML subtitle PES data fields */
#define EXPECTED_FLOW_DEF "block.dvb_ttml_subtitle."

/** size of the fixed part of the PES_data_field (EN 303 560 table 16) */
#define PES_DATA_FIELD_HEADER_SIZE 7
/** size of the per segment header */
#define SEGMENT_HEADER_SIZE 3
/** size of the trailing CRC_32 */
#define CRC_32_SIZE 4

/** segment_type values (EN 303 560 table 17) */
#define SEGMENT_TYPE_TTML       0x01
#define SEGMENT_TYPE_TTML_GZIP  0x02

/** the segment mediatime is expressed in units of 100 microseconds */
#define MEDIATIME_UNIT (UCLOCK_FREQ / 10000)

/** an inflated document larger than this is treated as corrupt: DVB TTML
 * documents are a few kilobytes of text */
#define MAX_DOCUMENT_SIZE (1 << 20)

/** @internal @This is the private context of a ts_ttmld pipe. */
struct upipe_ts_ttmld {
    /** refcount management structure */
    struct urefcount urefcount;

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** list of input urefs */
    struct uchain urefs;
    /** number of retained urefs */
    unsigned int nb_urefs;
    /** maximum number of retained urefs */
    unsigned int max_urefs;
    /** list of blockers */
    struct uchain blockers;

    /** public upipe structure */
    struct upipe upipe;
};

/** @hidden */
static int upipe_ts_ttmld_check(struct upipe *upipe, struct uref *flow_format);
/** @hidden */
static bool upipe_ts_ttmld_handle(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p);

UPIPE_HELPER_UPIPE(upipe_ts_ttmld, upipe, UPIPE_TS_TTMLD_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_ttmld, urefcount, upipe_ts_ttmld_free)
UPIPE_HELPER_VOID(upipe_ts_ttmld)
UPIPE_HELPER_OUTPUT(upipe_ts_ttmld, output, flow_def, output_state,
                    request_list)
UPIPE_HELPER_UBUF_MGR(upipe_ts_ttmld, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_ts_ttmld_check,
                      upipe_ts_ttmld_register_output_request,
                      upipe_ts_ttmld_unregister_output_request)
UPIPE_HELPER_INPUT(upipe_ts_ttmld, urefs, nb_urefs, max_urefs, blockers,
                   upipe_ts_ttmld_handle)

/** @internal @This allocates a ts_ttmld pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_ttmld_alloc(struct upipe_mgr *mgr,
                                          struct uprobe *uprobe,
                                          uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_ttmld_alloc_void(mgr, uprobe, signature,
                                                    args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_ts_ttmld_init_urefcount(upipe);
    upipe_ts_ttmld_init_output(upipe);
    upipe_ts_ttmld_init_ubuf_mgr(upipe);
    upipe_ts_ttmld_init_input(upipe);

    upipe_throw_ready(upipe);
    return upipe;
}

#ifdef HAVE_ZLIB
/** @internal @This inflates a gzip compressed TTML document into a block.
 *
 * @param upipe description structure of the pipe
 * @param in compressed document
 * @param in_size size of the compressed document
 * @return the inflated document, or NULL
 */
static struct ubuf *upipe_ts_ttmld_inflate(struct upipe *upipe,
                                           const uint8_t *in, size_t in_size)
{
    struct upipe_ts_ttmld *upipe_ts_ttmld = upipe_ts_ttmld_from_upipe(upipe);

    /* Gzip on a few kilobytes of XML gets nowhere near this, so the estimate
     * holds in practice and the cap is there for a segment that is not what it
     * says it is. */
    size_t size = in_size * 8 + 4096;
    if (size > MAX_DOCUMENT_SIZE)
        size = MAX_DOCUMENT_SIZE;

    for (;;) {
        struct ubuf *ubuf = ubuf_block_alloc(upipe_ts_ttmld->ubuf_mgr, size);
        if (unlikely(ubuf == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return NULL;
        }

        uint8_t *out;
        int out_size = -1;
        if (unlikely(!ubase_check(ubuf_block_write(ubuf, 0, &out_size,
                                                   &out)))) {
            ubuf_free(ubuf);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return NULL;
        }

        z_stream z;
        memset(&z, 0, sizeof (z));
        /* 15 window bits plus 16 to select the gzip wrapper of RFC 1952 */
        if (unlikely(inflateInit2(&z, 15 + 16) != Z_OK)) {
            ubuf_block_unmap(ubuf, 0);
            ubuf_free(ubuf);
            upipe_err(upipe, "could not initialize inflate");
            return NULL;
        }
        z.next_in = (Bytef *)in;
        z.avail_in = in_size;
        z.next_out = out;
        z.avail_out = out_size;

        int err = inflate(&z, Z_FINISH);
        size_t total = z.total_out;
        inflateEnd(&z);
        ubuf_block_unmap(ubuf, 0);

        if (likely(err == Z_STREAM_END)) {
            if (unlikely(!ubase_check(ubuf_block_resize(ubuf, 0, total)))) {
                upipe_warn(upipe, "could not resize inflated segment");
                ubuf_free(ubuf);
                return NULL;
            }
            return ubuf;
        }

        ubuf_free(ubuf);
        if (err != Z_BUF_ERROR || size >= MAX_DOCUMENT_SIZE) {
            upipe_warn_va(upipe, "invalid gzip compressed segment (%d)", err);
            return NULL;
        }
        /* the estimate was short: one more go, at the cap */
        size = MAX_DOCUMENT_SIZE;
    }
}
#endif

/** @internal @This outputs a TTML document.
 *
 * @param upipe description structure of the pipe
 * @param uref input buffer, mapped from its first octet, consumed
 * @param data mapped PES_data_field
 * @param offset offset of the segment_data_field in the input buffer
 * @param size size of the segment_data_field
 * @param compressed whether the document is gzip compressed
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_ttmld_output_document(struct upipe *upipe,
                                           struct uref *uref,
                                           const uint8_t *data,
                                           size_t offset, size_t size,
                                           bool compressed,
                                           struct upump **upump_p)
{
    if (!compressed) {
        /* The document is the payload as it stands: keep the ubuf and just
         * move the window onto it. */
        uref_block_unmap(uref, 0);
        if (unlikely(!ubase_check(uref_block_resize(uref, offset, size)))) {
            upipe_warn(upipe, "could not resize block");
            uref_free(uref);
            return;
        }
        upipe_ts_ttmld_output(upipe, uref, upump_p);
        return;
    }

#ifdef HAVE_ZLIB
    struct ubuf *ubuf = upipe_ts_ttmld_inflate(upipe, data + offset, size);
    uref_block_unmap(uref, 0);
    if (unlikely(ubuf == NULL)) {
        uref_free(uref);
        return;
    }

    uref_attach_ubuf(uref, ubuf);
    upipe_ts_ttmld_output(upipe, uref, upump_p);
#else
    uref_block_unmap(uref, 0);
    upipe_warn(upipe, "gzip compressed segment but zlib is not available");
    uref_free(uref);
#endif
}

/** @internal @This parses a PES_data_field and outputs its TTML segment.
 *
 * @param upipe description structure of the pipe
 * @param uref input buffer
 * @param upump_p reference to pump that generated the buffer
 * @return true if the buffer was handled
 */
static bool upipe_ts_ttmld_handle(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p)
{
    struct upipe_ts_ttmld *upipe_ts_ttmld = upipe_ts_ttmld_from_upipe(upipe);
    if (upipe_ts_ttmld->ubuf_mgr == NULL)
        return false;

    /* The framed packet is a chain of one ubuf per transport stream packet,
     * so put it back into one before reading across it. */
    const uint8_t *buffer;
    int size = -1;
    if (unlikely(!ubase_check(uref_block_merge(uref, upipe_ts_ttmld->ubuf_mgr,
                                               0, -1)) ||
                 !ubase_check(uref_block_read(uref, 0, &size, &buffer)))) {
        upipe_warn(upipe, "could not read PES data field");
        uref_free(uref);
        return true;
    }

    if (unlikely(size < PES_DATA_FIELD_HEADER_SIZE + CRC_32_SIZE)) {
        upipe_warn(upipe, "invalid PES data field");
        uref_block_unmap(uref, 0);
        uref_free(uref);
        return true;
    }

    uint64_t mediatime = ((uint64_t)buffer[0] << 40) |
                         ((uint64_t)buffer[1] << 32) |
                         ((uint64_t)buffer[2] << 24) |
                         ((uint64_t)buffer[3] << 16) |
                         ((uint64_t)buffer[4] << 8) | buffer[5];
    uint8_t num_of_segments = buffer[6];
    if (unlikely(!num_of_segments)) {
        upipe_warn(upipe, "PES data field with no segment");
        uref_block_unmap(uref, 0);
        uref_free(uref);
        return true;
    }

    /* At most one TTML segment per PES packet (EN 303 560 5.2.2.2.2); the
     * other segment types are reserved for future use. */
    size_t offset = PES_DATA_FIELD_HEADER_SIZE;
    size_t end = (size_t)size - CRC_32_SIZE;
    for (uint8_t i = 0; i < num_of_segments; i++) {
        if (unlikely(offset + SEGMENT_HEADER_SIZE > end)) {
            upipe_warn(upipe, "truncated segment header");
            break;
        }
        uint8_t type = buffer[offset];
        size_t length = (buffer[offset + 1] << 8) | buffer[offset + 2];
        offset += SEGMENT_HEADER_SIZE;
        if (unlikely(offset + length > end)) {
            upipe_warn(upipe, "truncated segment");
            break;
        }

        if (type == SEGMENT_TYPE_TTML || type == SEGMENT_TYPE_TTML_GZIP) {
            UBASE_FATAL(upipe, uref_ts_ttml_set_mediatime(uref,
                        mediatime * MEDIATIME_UNIT))
            upipe_ts_ttmld_output_document(upipe, uref, buffer, offset, length,
                    type == SEGMENT_TYPE_TTML_GZIP, upump_p);
            return true;
        }

        upipe_verbose_va(upipe, "ignoring segment type 0x%"PRIx8, type);
        offset += length;
    }

    uref_block_unmap(uref, 0);
    uref_free(uref);
    return true;
}

/** @internal @This receives a framed PES data field, holding it if the pipe
 * is not ready to output yet.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_ttmld_input(struct upipe *upipe, struct uref *uref,
                                 struct upump **upump_p)
{
    if (!upipe_ts_ttmld_check_input(upipe)) {
        upipe_ts_ttmld_hold_input(upipe, uref);
        upipe_ts_ttmld_block_input(upipe, upump_p);
    } else if (!upipe_ts_ttmld_handle(upipe, uref, upump_p)) {
        upipe_ts_ttmld_hold_input(upipe, uref);
        upipe_ts_ttmld_block_input(upipe, upump_p);
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    }
}

/** @internal @This checks the state of the pipe.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_ts_ttmld_check(struct upipe *upipe, struct uref *flow_format)
{
    if (flow_format != NULL)
        upipe_ts_ttmld_store_flow_def(upipe, flow_format);

    bool was_buffered = !upipe_ts_ttmld_check_input(upipe);
    upipe_ts_ttmld_output_input(upipe);
    upipe_ts_ttmld_unblock_input(upipe);
    if (was_buffered && upipe_ts_ttmld_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_ts_ttmld_input. */
        upipe_release(upipe);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_ttmld_set_flow_def(struct upipe *upipe,
                                       struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def))
    if (ubase_ncmp(def, EXPECTED_FLOW_DEF))
        return UBASE_ERR_INVALID;

    struct uref *flow_def_dup = uref_dup(flow_def);
    if (unlikely(flow_def_dup == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    UBASE_FATAL(upipe, uref_flow_set_def(flow_def_dup, UREF_TS_TTML_FLOW_DEF))
    upipe_ts_ttmld_store_flow_def(upipe, flow_def_dup);

    struct uref *flow_format = uref_dup(flow_def_dup);
    if (unlikely(flow_format == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    upipe_ts_ttmld_require_ubuf_mgr(upipe, flow_format);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_ttmld_control(struct upipe *upipe,
                                  int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_ts_ttmld_control_ubuf_mgr(upipe, command,
                                                         args));
    UBASE_HANDLED_RETURN(upipe_ts_ttmld_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_ttmld_set_flow_def(upipe, flow_def);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_ttmld_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_ts_ttmld_clean_input(upipe);
    upipe_ts_ttmld_clean_ubuf_mgr(upipe);
    upipe_ts_ttmld_clean_output(upipe);
    upipe_ts_ttmld_clean_urefcount(upipe);
    upipe_ts_ttmld_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_ttmld_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_TTMLD_SIGNATURE,

    .upipe_alloc = upipe_ts_ttmld_alloc,
    .upipe_input = upipe_ts_ttmld_input,
    .upipe_control = upipe_ts_ttmld_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_ttmld pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_ttmld_mgr_alloc(void)
{
    return &upipe_ts_ttmld_mgr;
}
