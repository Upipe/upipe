/*
 * Copyright (C) 2026 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe attributes describing a styled text page
 *
 * A page is a list of regions laid out on a canvas, each holding a list of
 * styled runs.  The producer resolves the styling and positions the regions;
 * the renderer measures the runs, breaks them into lines and draws them.
 *
 * The runs of all the regions form a single array in region order: the runs of
 * region r start after the runs of every region before it.
 */

#ifndef _UPIPE_UREF_TEXT_H_
/** @hidden */
#define _UPIPE_UREF_TEXT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uref_attr.h"

/** @This is the flow definition of a styled text page. */
#define UREF_TEXT_FLOW_DEF "void.text."

/** @This enumerates how the lines of a region are aligned in the block
 * progression direction (tts:displayAlign). */
enum uref_text_display_align {
    /** at the top of the region */
    UREF_TEXT_DISPLAY_ALIGN_BEFORE = 0,
    /** in the middle of the region */
    UREF_TEXT_DISPLAY_ALIGN_CENTER = 1,
    /** at the bottom of the region */
    UREF_TEXT_DISPLAY_ALIGN_AFTER = 2,
};

/** @This enumerates how a line is aligned in the region (tts:textAlign). */
enum uref_text_align {
    /** at the left of the region */
    UREF_TEXT_ALIGN_START = 0,
    /** in the middle of the region */
    UREF_TEXT_ALIGN_CENTER = 1,
    /** at the right of the region */
    UREF_TEXT_ALIGN_END = 2,
};

/** @This enumerates how the lines of a block are aligned relative to each
 * other (ebutts:multiRowAlign). */
enum uref_text_multi_row_align {
    /** follow tts:textAlign */
    UREF_TEXT_MULTI_ROW_ALIGN_AUTO = 0,
    /** at the left of the block */
    UREF_TEXT_MULTI_ROW_ALIGN_START = 1,
    /** in the middle of the block */
    UREF_TEXT_MULTI_ROW_ALIGN_CENTER = 2,
    /** at the right of the block */
    UREF_TEXT_MULTI_ROW_ALIGN_END = 3,
};

/** the run is emboldened */
#define UREF_TEXT_RUN_BOLD          0x01
/** the run is sheared */
#define UREF_TEXT_RUN_ITALIC        0x02
/** the run is underlined */
#define UREF_TEXT_RUN_UNDERLINE     0x04
/** the run starts a new line */
#define UREF_TEXT_RUN_BREAK         0x08

/* page */
UREF_ATTR_UNSIGNED(text, canvas_hsize, "x.chsize",
        width of the canvas the regions are positioned on)
UREF_ATTR_UNSIGNED(text, canvas_vsize, "x.cvsize",
        height of the canvas the regions are positioned on)
UREF_ATTR_SMALL_UNSIGNED(text, regions, "x.regions", number of regions)

/* regions */
UREF_ATTR_UNSIGNED_VA(text, region_x, "x.rx[%" PRIu8"]",
        left of the region on the canvas, uint8_t nb, nb)
UREF_ATTR_UNSIGNED_VA(text, region_y, "x.ry[%" PRIu8"]",
        top of the region on the canvas, uint8_t nb, nb)
UREF_ATTR_UNSIGNED_VA(text, region_hsize, "x.rw[%" PRIu8"]",
        width of the region, uint8_t nb, nb)
UREF_ATTR_UNSIGNED_VA(text, region_vsize, "x.rh[%" PRIu8"]",
        height of the region, uint8_t nb, nb)
UREF_ATTR_SMALL_UNSIGNED_VA(text, region_display_align, "x.rda[%" PRIu8"]",
        block alignment according to @ref uref_text_display_align,
        uint8_t nb, nb)
UREF_ATTR_SMALL_UNSIGNED_VA(text, region_text_align, "x.rta[%" PRIu8"]",
        line alignment according to @ref uref_text_align, uint8_t nb, nb)
UREF_ATTR_SMALL_UNSIGNED_VA(text, region_multi_row_align, "x.rmra[%" PRIu8"]",
        row alignment according to @ref uref_text_multi_row_align,
        uint8_t nb, nb)
UREF_ATTR_UNSIGNED_VA(text, region_line_height, "x.rlh[%" PRIu8"]",
        line height in pixels or 0 for normal, uint8_t nb, nb)
UREF_ATTR_UNSIGNED_VA(text, region_line_padding, "x.rlp[%" PRIu8"]",
        horizontal padding of the line backgrounds in pixels, uint8_t nb, nb)
UREF_ATTR_UNSIGNED_VA(text, region_background, "x.rbg[%" PRIu8"]",
        region background colour as packed RGBA, uint8_t nb, nb)
UREF_ATTR_VOID_VA(text, region_no_wrap, "x.rnw[%" PRIu8"]",
        the lines of the region are not wrapped, uint8_t nb, nb)
UREF_ATTR_VOID_VA(text, region_fill_line_gap, "x.rflg[%" PRIu8"]",
        the line backgrounds cover the whole line height, uint8_t nb, nb)
UREF_ATTR_SMALL_UNSIGNED_VA(text, region_runs, "x.rruns[%" PRIu8"]",
        number of runs in the region, uint8_t nb, nb)

/* runs */
UREF_ATTR_STRING_VA(text, run_text, "x.rt[%" PRIu8"]",
        UTF-8 text of the run, uint8_t nb, nb)
UREF_ATTR_UNSIGNED_VA(text, run_font_size, "x.rfs[%" PRIu8"]",
        font size of the run in pixels, uint8_t nb, nb)
UREF_ATTR_UNSIGNED_VA(text, run_color, "x.rfg[%" PRIu8"]",
        foreground colour of the run as packed RGBA, uint8_t nb, nb)
UREF_ATTR_UNSIGNED_VA(text, run_background, "x.rrbg[%" PRIu8"]",
        background colour of the run as packed RGBA, uint8_t nb, nb)
UREF_ATTR_SMALL_UNSIGNED_VA(text, run_flags, "x.rfl[%" PRIu8"]",
        style flags of the run, uint8_t nb, nb)

#ifdef __cplusplus
}
#endif
#endif
