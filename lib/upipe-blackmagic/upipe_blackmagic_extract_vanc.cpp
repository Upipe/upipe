/*
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
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
 * @short Upipe module to extract Blackmagic vertical ancillary data
 *
 * Normative references:
 *  - SMPTE RP-202-2008 video alignment for compression coding
 */

#define __STDC_LIMIT_MACROS    1
#define __STDC_FORMAT_MACROS   1
#define __STDC_CONSTANT_MACROS 1

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_input.h>
#include <upipe-blackmagic/upipe_blackmagic_extract_vanc.h>
#include <upipe-blackmagic/ubuf_pic_blackmagic.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>

#include "include/DeckLinkAPI.h"

const static struct upipe_bmd_vanc_field_start_line {
    BMDDisplayMode mode;
    unsigned int first_field;
    unsigned int second_field;
} upipe_bmd_vanc_field_start_lines[] = {
    /* SD modes */
    {bmdModeNTSC,           4, 267},
    {bmdModeNTSC2398,       4, 267},
    {bmdModePAL,            1, 314},

    /* HD 1080 modes */
    {bmdModeHD1080i50,      1, 564},
    {bmdModeHD1080i5994,    1, 564},
    {bmdModeHD1080i6000,    1, 564},

    {0, 0, 0}
};

const static struct upipe_bmd_vanc_frame_start_line {
    BMDDisplayMode mode;
    bool sd;
    unsigned int first_active;
    unsigned int first_vanc;
} upipe_bmd_vanc_frame_start_lines[] = {
    /* SD modes */
    {bmdModeNTSC,           true,   283,    270},
    {bmdModeNTSC2398,       true,   283,    270},
    {bmdModePAL,            true,   23,     1},
    {bmdModeNTSCp,          true,   45,     4},
    {bmdModePALp,           true,   45,     1},

    /* HD 1080 modes */
    {bmdModeHD1080p2398,    false,  42,     1},
    {bmdModeHD1080p24,      false,  42,     1},
    {bmdModeHD1080p25,      false,  42,     1},
    {bmdModeHD1080p2997,    false,  42,     1},
    {bmdModeHD1080p30,      false,  42,     1},
    {bmdModeHD1080i50,      false,  21,     1},
    {bmdModeHD1080i5994,    false,  21,     1},
    {bmdModeHD1080i6000,    false,  21,     1},
    {bmdModeHD1080p50,      false,  42,     1},
    {bmdModeHD1080p5994,    false,  42,     1},
    {bmdModeHD1080p6000,    false,  42,     1},

    /* HD 720 modes */
    {bmdModeHD720p50,       false,  26,     1},
    {bmdModeHD720p5994,     false,  26,     1},
    {bmdModeHD720p60,       false,  26,     1},

    /* 4k modes */
    {bmdMode4K2160p2398,    false,  84,     1},
    {bmdMode4K2160p24,      false,  84,     1},
    {bmdMode4K2160p25,      false,  84,     1},
    {bmdMode4K2160p2997,    false,  84,     1},
    {bmdMode4K2160p30,      false,  84,     1},
    {bmdMode4K2160p50,      false,  84,     1},
    {bmdMode4K2160p5994,    false,  84,     1},
    {bmdMode4K2160p60,      false,  84,     1},

    {0, 0, 0, false}
};

/** @internal @This is the private context of a bmd vanc pipe. */
struct upipe_bmd_vanc {
    /** refcount management structure */
    struct urefcount urefcount;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** output pipe */
    struct upipe *output;
    /** flow_definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** temporary uref storage (used during urequest) */
    struct uchain urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers (used during urequest) */
    struct uchain blockers;

    /** last read pixel format */
    BMDPixelFormat PixelFormat;

    /** public upipe structure */
    struct upipe upipe;
};

/** @hidden */
static bool upipe_bmd_vanc_handle(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p);
/** @hidden */
static int upipe_bmd_vanc_check(struct upipe *upipe, struct uref *flow_format);

UPIPE_HELPER_UPIPE(upipe_bmd_vanc, upipe, UPIPE_BMD_VANC_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_bmd_vanc, urefcount, upipe_bmd_vanc_free)
UPIPE_HELPER_VOID(upipe_bmd_vanc);
UPIPE_HELPER_OUTPUT(upipe_bmd_vanc, output, flow_def, output_state, request_list);
UPIPE_HELPER_UBUF_MGR(upipe_bmd_vanc, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_bmd_vanc_check,
                      upipe_bmd_vanc_register_output_request,
                      upipe_bmd_vanc_unregister_output_request)
UPIPE_HELPER_INPUT(upipe_bmd_vanc, urefs, nb_urefs, max_urefs, blockers, upipe_bmd_vanc_handle)

/** @internal @This allocates a bmd_vanc pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_bmd_vanc_alloc(struct upipe_mgr *mgr,
                                          struct uprobe *uprobe,
                                          uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_bmd_vanc_alloc_void(mgr, uprobe, signature,
                                                    args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_bmd_vanc *upipe_bmd_vanc = upipe_bmd_vanc_from_upipe(upipe);
    upipe_bmd_vanc_init_urefcount(upipe);
    upipe_bmd_vanc_init_ubuf_mgr(upipe);
    upipe_bmd_vanc_init_output(upipe);
    upipe_bmd_vanc_init_input(upipe);
    upipe_bmd_vanc->PixelFormat = 0;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This checks if the input may start.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_bmd_vanc_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_bmd_vanc *upipe_bmd_vanc = upipe_bmd_vanc_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_bmd_vanc_store_flow_def(upipe, flow_format);

    if (upipe_bmd_vanc->flow_def == NULL)
        return UBASE_ERR_NONE;

    bool was_buffered = !upipe_bmd_vanc_check_input(upipe);
    upipe_bmd_vanc_output_input(upipe);
    upipe_bmd_vanc_unblock_input(upipe);
    if (was_buffered && upipe_bmd_vanc_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_bmd_vanc_input. */
        upipe_release(upipe);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This copies a line from a 10-bit buffer.
 *
 * @param w buffer to write to
 * @param r buffer to read from
 * @param frame_start_line structure describing the image format
 * @param hsize horizontal size
 */
static void upipe_bmd_vanc_copy10(uint16_t *w, const uint8_t *r,
        const struct upipe_bmd_vanc_frame_start_line *frame_start_line,
        size_t hsize)
{
#define READ(a, b, c)                                                       \
    do {                                                                    \
        *a++ = r[0] | ((uint16_t)(r[1] & 0x3) << 8);                        \
        *b++ = (r[1] >> 2) | ((uint16_t)(r[2] & 0xf) << 6);                 \
        *c++ = (r[2] >> 4) | ((uint16_t)(r[3] & 0x3f) << 4);                \
        r += 4;                                                             \
    } while (0)

    if (frame_start_line->sd) {
        for (unsigned int i = 0; i < hsize - 5; i += 6) {
            READ(w, w, w);
            READ(w, w, w);
            READ(w, w, w);
            READ(w, w, w);
        }
    } else {
        uint16_t *uv = w + hsize;
        for (unsigned int i = 0; i < hsize - 5; i += 6) {
            READ(uv, w, uv);
            READ(w, uv, w);
            READ(uv, w, uv);
            READ(w, uv, w);
        }
    }
#undef READ
}

/** @internal @This copies a line from an 8-bit buffer.
 *
 * @param w buffer to write to
 * @param r buffer to read from
 * @param frame_start_line structure describing the image format
 * @param hsize horizontal size
 */
static void upipe_bmd_vanc_copy8(uint16_t *w, const uint8_t *r,
        const struct upipe_bmd_vanc_frame_start_line *frame_start_line,
        size_t hsize)
{
    uint16_t *uv;
    if (frame_start_line->sd) {
        for (unsigned int i = 0; i < hsize; i++) {
            *w++ = *r++ << 2;
            *w++ = *r++ << 2;
        }
    } else {
        uint16_t *uv = w + hsize;
        for (unsigned int i = 0; i < hsize; i++) {
            *uv++ = *r++ << 2;
            *w++ = *r++ << 2;
        }
    }
}

/** @internal @This copies a line.
 *
 * @param w buffer to write to
 * @param r buffer to read from
 * @param PixelFormat Blackmagic pixel format
 * @param frame_start_line structure describing the image format
 * @param hsize horizontal size
 */
static void upipe_bmd_vanc_copy(uint16_t *w, const uint8_t *r,
        BMDPixelFormat PixelFormat,
        const struct upipe_bmd_vanc_frame_start_line *frame_start_line,
        size_t hsize)
{
    if (PixelFormat == bmdFormat8BitYUV)
        upipe_bmd_vanc_copy8(w, r, frame_start_line, hsize);
    else
        upipe_bmd_vanc_copy10(w, r, frame_start_line, hsize);
}

/** @internal @This blanks a line.
 *
 * @param w buffer to write to
 * @param frame_start_line structure describing the image format
 * @param hsize horizontal size
 */
static void upipe_bmd_vanc_blank(uint16_t *w,
        const struct upipe_bmd_vanc_frame_start_line *frame_start_line,
        size_t hsize)
{
    uint16_t *uv;
    if (frame_start_line->sd)
        uv = w;
    else
        uv = w + hsize;
    for (unsigned int i = 0; i < hsize; i++) {
        *uv++ = 0x200;
        *w++ = 0x40;
    }
}

/** @internal @This calculates the next line number.
 *
 * @param field_start_line structure describing the field format, or NULL
 * @param line_p pointer to current line, modified on execution
 */
static void upipe_bmd_vanc_next_line(
        const struct upipe_bmd_vanc_field_start_line *field_start_line,
        unsigned int *line_p)
{
    if (field_start_line == NULL) {
        (*line_p)++;
        return;
    }

    if (*line_p >= field_start_line->first_field &&
        *line_p < field_start_line->second_field)
        *line_p = field_start_line->second_field +
                  (*line_p - field_start_line->first_field);
    else
        *line_p = field_start_line->first_field + 1 +
                  (*line_p - field_start_line->second_field);
}

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 * @return false if the input must be blocked
 */
static bool upipe_bmd_vanc_handle(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p)
{
    struct upipe_bmd_vanc *upipe_bmd_vanc = upipe_bmd_vanc_from_upipe(upipe);
    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        upipe_bmd_vanc_store_flow_def(upipe, NULL);
        upipe_bmd_vanc_require_ubuf_mgr(upipe, uref);
        return true;
    }

    if (upipe_bmd_vanc->flow_def == NULL)
        return false;

    size_t hsize, vsize, uref_stride;
    void *_VideoFrame = NULL;
    if (unlikely(uref->ubuf == NULL ||
                 !ubase_check(uref_pic_size(uref, &hsize, &vsize, NULL)) ||
                 !ubase_check(uref_pic_plane_size(uref, "u8y8v8y8",
                         &uref_stride, NULL, NULL, NULL)) ||
                 !ubase_check(ubuf_pic_bmd_get_video_frame(uref->ubuf,
                                                           &_VideoFrame)) ||
                 _VideoFrame == NULL)) {
        upipe_throw_error(upipe, UBASE_ERR_INVALID);
        uref_free(uref);
        return true;
    }
    IDeckLinkVideoInputFrame *VideoFrame =
        (IDeckLinkVideoInputFrame *)_VideoFrame;

    IDeckLinkVideoFrameAncillary *Ancillary;
    if (unlikely(VideoFrame->GetAncillaryData(&Ancillary) != S_OK)) {
        upipe_throw_error(upipe, UBASE_ERR_EXTERNAL);
        uref_free(uref);
        return true;
    }

    BMDPixelFormat PixelFormat = Ancillary->GetPixelFormat();
    if (unlikely(PixelFormat != upipe_bmd_vanc->PixelFormat)) {
        switch (PixelFormat) {
            case bmdFormat8BitYUV:
                upipe_notice(upipe, "now using 8-bit pixel format");
                break;
            case bmdFormat10BitYUV:
                upipe_notice(upipe, "now using 10-bit pixel format");
                break;
            default:
                upipe_warn_va(upipe, "unsupported pixel format %u", PixelFormat);
                upipe_throw_error(upipe, UBASE_ERR_INVALID);
                Ancillary->Release();
                uref_free(uref);
                return true;
        }
        upipe_bmd_vanc->PixelFormat = PixelFormat;
    }

    BMDDisplayMode DisplayMode = Ancillary->GetDisplayMode();

    const struct upipe_bmd_vanc_field_start_line *field_start_line =
        &upipe_bmd_vanc_field_start_lines[0];
    for ( ; ; ) {
        if (!field_start_line->first_field) {
            field_start_line = NULL;
            break;
        }
        if (field_start_line->mode == DisplayMode)
            break;
        field_start_line++;
    }

    const struct upipe_bmd_vanc_frame_start_line *frame_start_line =
        &upipe_bmd_vanc_frame_start_lines[0];
    for ( ; ; ) {
        if (!frame_start_line->first_active) {
            frame_start_line = NULL;
            break;
        }
        if (frame_start_line->mode == DisplayMode)
            break;
        frame_start_line++;
    }

    if (unlikely(frame_start_line == NULL)) {
        upipe_warn(upipe, "display mode has no ancillary data");
        Ancillary->Release();
        uref_free(uref);
        return true;
    }

    unsigned int line = frame_start_line->first_vanc;
    unsigned int nb_lines = frame_start_line->first_active - line;
    if (field_start_line != NULL)
        nb_lines *= 2;
    /* Lines that are actually part of the VBI, but are in the active area. */
    unsigned int nb_vbi_lines = 2;
    if (line == 283)
        nb_vbi_lines++;

    struct ubuf *ubuf = ubuf_pic_alloc(upipe_bmd_vanc->ubuf_mgr, hsize * 2,
                                       nb_lines + nb_vbi_lines);
    if (unlikely(ubuf == NULL)) {
        upipe_throw_error(upipe, UBASE_ERR_ALLOC);
        Ancillary->Release();
        uref_free(uref);
        return true;
    }

    size_t stride;
    uint8_t *w;
    if (unlikely(!ubase_check(ubuf_pic_plane_size(ubuf, "x10", &stride,
                                                  NULL, NULL, NULL)) ||
                 !ubase_check(ubuf_pic_plane_write(ubuf, "x10", 0, 0, -1, -1,
                                                   &w)))) {
        upipe_throw_error(upipe, UBASE_ERR_ALLOC);
        ubuf_free(ubuf);
        Ancillary->Release();
        uref_free(uref);
        return true;
    }

    while (nb_lines--) {
        void *r;
        if (Ancillary->GetBufferForVerticalBlankingLine(line, &r) == S_OK)
            upipe_bmd_vanc_copy((uint16_t *)w, (const uint8_t *)r,
                                PixelFormat, frame_start_line, hsize);
        else
            upipe_bmd_vanc_blank((uint16_t *)w, frame_start_line, hsize);

        w += stride;

        upipe_bmd_vanc_next_line(field_start_line, &line);
    }

    /* TODO support 10-bit mode */
    const uint8_t *r;
    if (unlikely(!ubase_check(uref_pic_plane_read(uref, "u8y8v8y8",
                                                  0, 0, -1, -1, &r)))) {
        upipe_throw_error(upipe, UBASE_ERR_INVALID);
        while (nb_vbi_lines--) {
            upipe_bmd_vanc_blank((uint16_t *)w, frame_start_line, hsize);
            w += stride;

            upipe_bmd_vanc_next_line(field_start_line, &line);
        }
    } else {
        while (nb_vbi_lines--) {
            upipe_bmd_vanc_copy((uint16_t *)w, r, bmdFormat8BitYUV,
                                frame_start_line, hsize);
            r += uref_stride;
            w += stride;

            upipe_bmd_vanc_next_line(field_start_line, &line);
        }
        uref_pic_plane_unmap(uref, "u8y8v8y8", 0, 0, -1, -1);
    }

    ubuf_pic_plane_unmap(ubuf, "x10", 0, 0, -1, -1);
    Ancillary->Release();
    uref_attach_ubuf(uref, ubuf);
    upipe_bmd_vanc_output(upipe, uref, upump_p);
    return true;
}

/** @internal @This inputs data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_bmd_vanc_input(struct upipe *upipe, struct uref *uref,
                                 struct upump **upump_p)
{
    if (!upipe_bmd_vanc_check_input(upipe)) {
        upipe_bmd_vanc_hold_input(upipe, uref);
        upipe_bmd_vanc_block_input(upipe, upump_p);
    } else if (!upipe_bmd_vanc_handle(upipe, uref, upump_p)) {
        upipe_bmd_vanc_hold_input(upipe, uref);
        upipe_bmd_vanc_block_input(upipe, upump_p);
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    }
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_bmd_vanc_set_flow_def(struct upipe *upipe,
                                       struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    uint64_t hsize;
    UBASE_RETURN(uref_flow_match_def(flow_def, "pic."))
    UBASE_RETURN(uref_pic_flow_match_macropixel(flow_def, 2, 2))
    UBASE_RETURN(uref_pic_flow_match_planes(flow_def, 1, 1))
    UBASE_RETURN(uref_pic_flow_check_chroma(flow_def, 1, 1, 4, "u8y8v8y8"))
    UBASE_RETURN(uref_pic_flow_get_hsize(flow_def, &hsize))
    struct uref *flow_def_dup;
    if ((flow_def_dup = uref_dup(flow_def)) == NULL)
        return UBASE_ERR_ALLOC;
    uref_pic_flow_clear_format(flow_def_dup);
    UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def_dup, 1))
    UBASE_RETURN(uref_pic_flow_add_plane(flow_def_dup, 1, 1, 2, "x10"))
    UBASE_RETURN(uref_pic_flow_set_hsize(flow_def_dup, hsize * 2))
    upipe_input(upipe, flow_def_dup, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a vmd_vanc pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_bmd_vanc_control(struct upipe *upipe,
                                  int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_bmd_vanc_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_bmd_vanc_set_flow_def(upipe, flow_def);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_bmd_vanc_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_bmd_vanc_clean_input(upipe);
    upipe_bmd_vanc_clean_ubuf_mgr(upipe);
    upipe_bmd_vanc_clean_output(upipe);
    upipe_bmd_vanc_clean_urefcount(upipe);
    upipe_bmd_vanc_free_void(upipe);
}

extern "C" {
/** module manager static descriptor */
static struct upipe_mgr upipe_bmd_vanc_mgr = {
    /* .refcount = */ NULL,
    /* .signature = */ UPIPE_BMD_VANC_SIGNATURE,

    /* .upipe_err_str = */ NULL,
    /* .upipe_command_str = */ NULL,
    /* .upipe_event_str = */ NULL,

    /* .upipe_alloc = */ upipe_bmd_vanc_alloc,
    /* .upipe_input = */ upipe_bmd_vanc_input,
    /* .upipe_control = */ upipe_bmd_vanc_control,

    /* .upipe_mgr_control = */ NULL
};
}

/** @This returns the management structure for all bmd_vanc pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_bmd_vanc_mgr_alloc(void)
{
    return &upipe_bmd_vanc_mgr;
}
