/*
 * Copyright (C) 2018 Open Broadcast Systems Ltd
 *
 * Authors: James Darnley
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/** @file
 * @short Upipe module to make whole frames out of pciesdi source blocks.
 */

#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>

#include <upipe/uclock.h>
#include <upipe/uref_block.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_flow.h>

#include <upipe-hbrmt/upipe_pciesdi_source_framer.h>

#include "upipe_hbrmt_common.h"

enum operating_mode {
    FRAMES,
    FIELDS,
    CHUNKS,
    VSYNC_ONLY,
};

struct upipe_pciesdi_source_framer {
    /** refcount management structure */
    struct urefcount urefcount;
    /* output stuff */
    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** public upipe structure */
    struct upipe upipe;

    /* SDI offsets */
    const struct sdi_offsets_fmt *f;

    int prev_line_num;
    uint16_t prev_fvh;
    bool start;
    bool progressive;
    bool sdi3g_levelb;
    enum operating_mode mode;

    /* vsync handling */
    int expected_line_num;
    uint16_t prev_eav, prev_sav;
    /* whether there was a vsync error at the given transition */
    bool vbi_f1_part1, active_f1, vbi_f1_part2, vbi_f2_part1, active_f2, vbi_f2_part2;
    bool discontinuity;

    struct urational fps;
    uint64_t frame_counter;

    /* uref for output */
    struct uref *uref;

    /* Lines cached in output uref. */
    int cached_lines;
};

UPIPE_HELPER_UPIPE(upipe_pciesdi_source_framer, upipe,
        UPIPE_PCIESDI_SOURCE_FRAMER_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_pciesdi_source_framer, urefcount,
        upipe_pciesdi_source_framer_free)
UPIPE_HELPER_VOID(upipe_pciesdi_source_framer)
UPIPE_HELPER_OUTPUT(upipe_pciesdi_source_framer, output, flow_def, output_state,
        request_list)

static void reset_state(struct upipe_pciesdi_source_framer *ctx)
{
    /* Restart frame alignment. */
    ctx->start = false;
    /* Release any cached data. */
    uref_free(ctx->uref);
    ctx->uref = NULL;
    ctx->cached_lines = 0;
    /* Clear state for finding top of frame. */
    ctx->prev_fvh = 0;
    ctx->prev_line_num = 0;
    /* Clear state for vsync tracking. */
    ctx->expected_line_num = 0;
    ctx->prev_eav = ctx->prev_sav = 0;
    ctx->vbi_f1_part1 = ctx->active_f1 = ctx->vbi_f1_part2
        = ctx->vbi_f2_part1 = ctx->active_f2 = ctx->vbi_f2_part2 = false;
}

static struct upipe *upipe_pciesdi_source_framer_alloc(struct upipe_mgr *mgr, struct uprobe
        *uprobe, uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_pciesdi_source_framer_alloc_void(mgr, uprobe,
            signature, args);
    if (unlikely(upipe == NULL))
        return NULL;
    struct upipe_pciesdi_source_framer *ctx = upipe_pciesdi_source_framer_from_upipe(upipe);

    ctx->prev_line_num = 0;
    ctx->f = NULL;
    ctx->prev_fvh = 0;
    ctx->start = false;
    ctx->progressive = false;
    ctx->uref = NULL;
    ctx->cached_lines = 0;
    ctx->frame_counter = 0;
    ctx->mode = FRAMES;
    ctx->expected_line_num = 0;
    ctx->discontinuity = false;
    ctx->prev_eav = ctx->prev_sav = 0;
    ctx->vbi_f1_part1 = ctx->active_f1 = ctx->vbi_f1_part2
        = ctx->vbi_f2_part1 = ctx->active_f2 = ctx->vbi_f2_part2 = false;

    upipe_pciesdi_source_framer_init_output(upipe);
    upipe_pciesdi_source_framer_init_urefcount(upipe);

    upipe_throw_ready(upipe);
    return upipe;
}


static void upipe_pciesdi_source_framer_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    struct upipe_pciesdi_source_framer *ctx = upipe_pciesdi_source_framer_from_upipe(upipe);
    uref_free(ctx->uref);

    upipe_pciesdi_source_framer_clean_output(upipe);
    upipe_pciesdi_source_framer_clean_urefcount(upipe);
    upipe_pciesdi_source_framer_free_void(upipe);
}

static int upipe_pciesdi_source_framer_set_flow_def(struct upipe *upipe, struct
        uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, "block."))

    struct upipe_pciesdi_source_framer *ctx = upipe_pciesdi_source_framer_from_upipe(upipe);

    ctx->f = sdi_get_offsets(flow_def);
    if (!ctx->f) {
        upipe_err(upipe, "Could not figure out SDI offsets");
        return UBASE_ERR_INVALID;
    }
    ctx->progressive = ubase_check(uref_pic_get_progressive(flow_def));
    ctx->sdi3g_levelb = ubase_check(uref_block_get_sdi3g_levelb(flow_def));
    if (ctx->sdi3g_levelb)
        /* Use interlaced fvh transition to detect start of level B frame. */
        ctx->progressive = false;

    UBASE_RETURN(uref_pic_flow_get_fps(flow_def, &ctx->fps));
    uint64_t height;
    UBASE_RETURN(uref_pic_flow_get_vsize(flow_def, &height));

    /* Flag separate fields. */
    if (ctx->mode == FIELDS) {
        height /= 2;
        ctx->fps.num *= 2;
        UBASE_RETURN(uref_pic_flow_set_sepfields(flow_def));
        UBASE_RETURN(uref_pic_flow_set_fps(flow_def, ctx->fps));
        UBASE_RETURN(uref_pic_flow_set_vsize(flow_def, height));
    }


    upipe_pciesdi_source_framer_store_flow_def(upipe, uref_dup(flow_def));
    return UBASE_ERR_NONE;
}

static int upipe_pciesdi_source_framer_set_option(struct upipe *upipe, const char *option,
        const char *value)
{
    struct upipe_pciesdi_source_framer *ctx = upipe_pciesdi_source_framer_from_upipe(upipe);

    if (!option || !value)
        return UBASE_ERR_INVALID;

    if (!strcmp(option, "mode")) {
        if (!strcmp(value, "frames")) {
            ctx->mode = FRAMES;
            return UBASE_ERR_NONE;
        }
        if (!strcmp(value, "fields")) {
            ctx->mode = FIELDS;
            return UBASE_ERR_NONE;
        }
        if (!strcmp(value, "chunks")) {
            ctx->mode = CHUNKS;
            return UBASE_ERR_NONE;
        }
        if (!strcmp(value, "vsync-only")) {
            ctx->mode = VSYNC_ONLY;
            return UBASE_ERR_NONE;
        }
        upipe_err_va(upipe, "Unknown %s: %s", option, value);
        return UBASE_ERR_INVALID;
    }

    upipe_err_va(upipe, "Unknown option %s", option);
    return UBASE_ERR_INVALID;
}

static int upipe_pciesdi_source_framer_control(struct upipe *upipe, int command,
        va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_pciesdi_source_framer_set_flow_def(upipe, flow_def);
        }

        case UPIPE_SET_OPTION: {
            const char *option = va_arg(args, const char *);
            const char *value  = va_arg(args, const char *);
            return upipe_pciesdi_source_framer_set_option(upipe, option, value);
        }

        case UPIPE_REGISTER_REQUEST:
        case UPIPE_UNREGISTER_REQUEST:
        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_pciesdi_source_framer_control_output(upipe, command, args);
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

static int timestamp_uref(struct upipe_pciesdi_source_framer *ctx, struct uref *uref)
{
    /* TODO: handle errors. */
    uint64_t pts = ctx->frame_counter * ctx->fps.den * UCLOCK_FREQ / ctx->fps.num;
    uref_clock_set_pts_prog(uref, pts);
    ctx->frame_counter += 1;
    return UBASE_ERR_NONE;
}

static int find_top_of_frame(struct upipe_pciesdi_source_framer *ctx, struct uref *uref)
{
    struct upipe *upipe = upipe_pciesdi_source_framer_to_upipe(ctx);

    int size = -1;
    const uint8_t *src = NULL;
    UBASE_RETURN(uref_block_read(uref, 0, &size, &src));
    size /= sizeof(uint16_t);

    int eav_fvh_offset = 6;
    if (ctx->f->pict_fmt->sd)
        eav_fvh_offset = 3;

    int sdi_width = 2 * ctx->f->width;
    int height = ctx->f->height;
    int offset;
    for (offset = 0; offset < size; offset += sdi_width) {
        const uint16_t *buf = (const uint16_t*)src + offset;
        uint16_t fvh = buf[eav_fvh_offset];

        int line = 0;
        if (height >= 720) {
            line = (buf[8] & 0x1ff) >> 2;
            line |= ((buf[10] & 0x1ff) >> 2) << 7;
        }

        if (fvh != ctx->prev_fvh) {
            upipe_dbg_va(upipe, "fvh change from %#5x to %#5x", ctx->prev_fvh, fvh);
            if (line != ctx->prev_line_num)
                upipe_dbg_va(upipe, "line number change from %d to %d", ctx->prev_line_num, line);
        }

        if (ctx->progressive) {
            /* Since there is no SD progressive format check that the line num
             * wraps around correctly and that the fvh word is correct. */
            if (ctx->prev_line_num == height && line == 1
                    && ctx->prev_fvh == 0x2d8 && fvh == 0x2d8) {
                ctx->start = true;
                break;
            }
        }

        else if (height >= 720) {
            /* For interlaced HD check that both line num and fvh changes are
             * right. */
            if (ctx->prev_line_num == height && line == 1
                    && ctx->prev_fvh == 0x3c4 && fvh == 0x2d8) {
                ctx->start = true;
                break;
            }
        }

        /* Fallback to just interlaced fvh change. */
        else {
            if (ctx->prev_fvh == 0x3c4 && fvh == 0x2d8) {
                ctx->start = true;
                break;
            }
        }
        ctx->prev_fvh = fvh;
        ctx->prev_line_num = line;
    }
    uref_block_unmap(uref, 0);

    /* Keep the top of frame if it was found. */
    if (ctx->start) {
        uref_block_resize(uref, sizeof(uint16_t) * offset, -1);
        ctx->uref = uref;
        ctx->cached_lines = (size - offset) / sdi_width;
    }

    return UBASE_ERR_NONE;
}

static int handle_frames(struct upipe_pciesdi_source_framer *ctx, struct uref *uref, struct upump **upump_p,
        int width, int lines_in_uref)
{
    struct upipe *upipe = upipe_pciesdi_source_framer_to_upipe(ctx);

    /* If there are not enough lines yet store the block. */
    if (ctx->cached_lines + lines_in_uref < ctx->f->height) {
        ctx->cached_lines += lines_in_uref;

        /* If there is a uref, append the new ubuf to the old otherwise just
         * store the uref. */
        if (ctx->uref) {
            uref_block_append(ctx->uref, uref_detach_ubuf(uref));
            uref_free(uref);
            return UBASE_ERR_NONE;
        } else {
            ctx->uref = uref;
            return UBASE_ERR_NONE;
        }
    }

    /* If there is exactly enough lines then output the uref and clear the
     * stored values. */
    else if (ctx->cached_lines + lines_in_uref == ctx->f->height) {
        uref_block_append(ctx->uref, uref_detach_ubuf(uref));
        timestamp_uref(ctx, ctx->uref);
        upipe_pciesdi_source_framer_output(upipe, ctx->uref, upump_p);

        ctx->uref = NULL;
        ctx->cached_lines = 0;
        uref_free(uref);

        return UBASE_ERR_NONE;
    }

    /* If there is more than enough lines then output and store the excess. */
    else if (ctx->cached_lines + lines_in_uref > ctx->f->height) {
        /* Duplicate and resize block to be the end of the frame. */
        struct ubuf *ubuf = ubuf_dup(uref->ubuf);
        UBASE_ALLOC_RETURN(ubuf);

        int lines_needed = ctx->f->height - ctx->cached_lines;
        ubuf_block_resize(ubuf, 0, width * lines_needed);
        uref_block_append(ctx->uref, ubuf);
        timestamp_uref(ctx, ctx->uref);
        upipe_pciesdi_source_framer_output(upipe, ctx->uref, upump_p);

        /* Keep top of next frame. */
        uref_block_resize(uref, width * lines_needed, -1);
        ctx->uref = uref;
        ctx->cached_lines = lines_in_uref - lines_needed;

        return UBASE_ERR_NONE;
    }

    /* Execution should never reach here. */
    return UBASE_ERR_UNHANDLED;
}

static int handle_fields(struct upipe_pciesdi_source_framer *ctx, struct uref *uref, struct upump **upump_p,
        int width, int lines_in_uref)
{
    struct upipe *upipe = upipe_pciesdi_source_framer_to_upipe(ctx);

    /* If the number of cached lines is less then the end of the first field
     * then the pipe is working to output the first field otherwise it is the
     * second field. */
    int field_height;
    bool second_field;
    if (ctx->cached_lines < ctx->f->pict_fmt->vbi_f1_part2.end) {
        field_height = ctx->f->pict_fmt->vbi_f1_part2.end;
        second_field = false;
    } else {
        field_height = ctx->f->height;
        second_field = true;
    }

    /* If there are not enough lines yet store the block. */
    if (ctx->cached_lines + lines_in_uref < field_height) {
        ctx->cached_lines += lines_in_uref;

        /* If there is a uref, append the new ubuf to the old otherwise just
         * store the uref. */
        if (ctx->uref) {
            uref_block_append(ctx->uref, uref_detach_ubuf(uref));
            uref_free(uref);
            return UBASE_ERR_NONE;
        } else {
            ctx->uref = uref;
            return UBASE_ERR_NONE;
        }
    }

    /* If there is exactly enough lines then output the uref and clear the
     * stored values. */
    else if (ctx->cached_lines + lines_in_uref == field_height) {
        uref_block_append(ctx->uref, uref_detach_ubuf(uref));
        timestamp_uref(ctx, ctx->uref);
        upipe_pciesdi_source_framer_output(upipe, ctx->uref, upump_p);

        ctx->uref = NULL;
        uref_free(uref);

        if (second_field)
            ctx->cached_lines = 0;
        else
            ctx->cached_lines = field_height;

        return UBASE_ERR_NONE;
    }

    /* If there is more than enough lines then output and store the excess. */
    else if (ctx->cached_lines + lines_in_uref > field_height) {
        /* Duplicate and resize block to be the end of the frame. */
        struct ubuf *ubuf = ubuf_dup(uref->ubuf);
        UBASE_ALLOC_RETURN(ubuf);

        int lines_needed = field_height - ctx->cached_lines;

        ubuf_block_resize(ubuf, 0, width * lines_needed);
        uref_block_append(ctx->uref, ubuf);
        timestamp_uref(ctx, ctx->uref);
        upipe_pciesdi_source_framer_output(upipe, ctx->uref, upump_p);

        /* Keep top of next frame. */
        uref_block_resize(uref, width * lines_needed, -1);
        ctx->uref = uref;

        if (second_field)
            ctx->cached_lines = lines_in_uref - lines_needed;
        else
            ctx->cached_lines += lines_in_uref;

        return UBASE_ERR_NONE;
    }

    /* Execution should never reach here. */
    return UBASE_ERR_UNHANDLED;
}

static int handle_chunks(struct upipe_pciesdi_source_framer *ctx, struct uref *uref, struct upump **upump_p,
        int width, int lines_in_uref)
{
    struct upipe *upipe = upipe_pciesdi_source_framer_to_upipe(ctx);

    /* If the number of cached lines is less then the end of the first field
     * then the pipe is working to output the first field otherwise it is the
     * second field. */
    int field_height;
    if (ctx->cached_lines < ctx->f->pict_fmt->vbi_f1_part2.end) {
        field_height = ctx->f->pict_fmt->vbi_f1_part2.end;
    } else {
        field_height = ctx->f->height;
    }

    /* If there are not enough lines to cross a field boundary then output
     * everything. */
    if (ctx->cached_lines + lines_in_uref <= field_height) {
        /* If there is a uref, append the new ubuf. */
        if (ctx->uref) {
            uref_block_append(ctx->uref, uref_detach_ubuf(uref));
            uref_free(uref);
            uref = ctx->uref;
            ctx->uref = NULL;
        }

        timestamp_uref(ctx, uref);
        upipe_pciesdi_source_framer_output(upipe, uref, upump_p);

        ctx->cached_lines = (ctx->cached_lines + lines_in_uref) % ctx->f->height;

        return UBASE_ERR_NONE;
    }

    /* If there is more than enough lines then output and store the excess. */
    else if (ctx->cached_lines + lines_in_uref > field_height) {
        int lines_needed = field_height - ctx->cached_lines;

        /* If there is a uref, append the new ubuf. */
        if (ctx->uref) {
            struct uref *temp = ctx->uref;

            /* Duplicate and resize block to be the end of the frame. */
            struct ubuf *ubuf = ubuf_dup(uref->ubuf);
            UBASE_ALLOC_RETURN(ubuf);
            ubuf_block_resize(ubuf, 0, width * lines_needed);
            uref_block_append(temp, ubuf);

            /* Keep top of next frame. */
            uref_block_resize(uref, width * lines_needed, -1);
            ctx->uref = uref;

            uref = temp;
        }

        else {
            /* Duplicate and resize block to be the end of the frame. */
            struct uref *temp = uref_dup(uref);
            UBASE_ALLOC_RETURN(temp);
            uref_block_resize(temp, 0, width * lines_needed);

            /* Keep top of next frame. */
            uref_block_resize(uref, width * lines_needed, -1);
            ctx->uref = uref;

            uref = temp;
        }

        timestamp_uref(ctx, uref);
        upipe_pciesdi_source_framer_output(upipe, uref, upump_p);

        ctx->cached_lines = (ctx->cached_lines + lines_in_uref) % ctx->f->height;

        return UBASE_ERR_NONE;
    }

    /* Execution should never reach here. */
    return UBASE_ERR_UNHANDLED;
}

static int handle_vsync_only(struct upipe_pciesdi_source_framer *ctx, struct uref *uref,
        struct upump **upump_p, int width, int lines_in_uref)
{
    struct upipe *upipe = upipe_pciesdi_source_framer_to_upipe(ctx);

    /* Add cached lines from find_top_of_frame(). */
    if (ctx->uref) {
        uref_block_append(ctx->uref, uref_detach_ubuf(uref));
        uref_free(uref);
        uref = ctx->uref;
        ctx->uref = NULL;
        lines_in_uref += ctx->cached_lines;
    }

    size_t input_size;
    UBASE_RETURN(uref_block_size(uref, &input_size));

    const struct sdi_picture_fmt *p = ctx->f->pict_fmt;
    const bool sdi3g_levelb = ctx->sdi3g_levelb;
    const bool sd = p->sd;
    const bool ntsc = p->active_height == 486;
    const int height = ctx->f->height;
    const int eav_fvh_offset = (sd) ? 3 : 6;
    const int sav_fvh_offset = 2*ctx->f->active_offset - 1;

    int num_consecutive_line_errors = 0;
    int expected_line_num = ctx->expected_line_num;
    uint16_t prev_eav = ctx->prev_eav;
    uint16_t prev_sav = ctx->prev_sav;

    /* For each segment in the uref. */
    for (int offset = 0; input_size > 0; /*do nothing*/) {
        const uint8_t *src = NULL;
        int buf_size = -1;
        /* Map input buffer. */
        UBASE_RETURN(uref_block_read(uref, offset, &buf_size, &src));

        /* TODO: full checks like the debug mode of upipe_sdi_dec? */

        /* For each line in the segment. */
        for (int h = buf_size / width; h > 0; h--) {
            const uint16_t *buf = (const uint16_t*)src;

            uint16_t eav = buf[eav_fvh_offset];
            uint16_t sav = buf[sav_fvh_offset];
            int line = 0;
            if (!sd) {
                line = (buf[8] & 0x1ff) >> 2;
                line |= ((buf[10] & 0x1ff) >> 2) << 7;
            }

            if (sd) {
                int line_num = expected_line_num + 1;
                if (ntsc)
                    line_num = ((line_num + 2) % 525) + 1;

                if (line_num == p->vbi_f1_part1.start)
                    ctx->vbi_f1_part1 = eav != eav_fvh_cword[0][true]
                        && sav != sav_fvh_cword[0][true]
                        && prev_eav != eav_fvh_cword[1][true]
                        && prev_sav != sav_fvh_cword[1][true];

                if (line_num == p->active_f1.start)
                    ctx->active_f1 = eav != eav_fvh_cword[0][false]
                        && sav != sav_fvh_cword[0][false]
                        && prev_eav != eav_fvh_cword[0][true]
                        && prev_sav != sav_fvh_cword[0][true];

                if (line_num == p->vbi_f1_part2.start)
                    ctx->vbi_f1_part2 = eav != eav_fvh_cword[0][true]
                        && sav != sav_fvh_cword[0][true]
                        && prev_eav != eav_fvh_cword[0][false]
                        && prev_sav != sav_fvh_cword[0][false];

                if (line_num == p->vbi_f2_part1.start)
                    ctx->vbi_f2_part1 = eav != eav_fvh_cword[1][true]
                        && sav != sav_fvh_cword[1][true]
                        && prev_eav != eav_fvh_cword[0][true]
                        && prev_sav != sav_fvh_cword[0][true];

                if (line_num == p->active_f2.start)
                    ctx->active_f2 = eav != eav_fvh_cword[1][false]
                        && sav != sav_fvh_cword[1][false]
                        && prev_eav != eav_fvh_cword[1][true]
                        && prev_sav != sav_fvh_cword[1][true];

                if (line_num == p->vbi_f2_part2.start)
                    ctx->vbi_f2_part2 = eav != eav_fvh_cword[1][true]
                        && sav != sav_fvh_cword[1][true]
                        && prev_eav != eav_fvh_cword[1][false]
                        && prev_sav != sav_fvh_cword[1][false];


                expected_line_num = (expected_line_num + 1) % height;
                prev_eav = eav;
                prev_sav = sav;
            }

            else if (sdi3g_levelb) {
                if (line != expected_line_num/2 + 1)
                    num_consecutive_line_errors += 1;
                else
                    num_consecutive_line_errors = 0;

                expected_line_num = (expected_line_num + 1) % (2*height);
            }

            else {
                if (line != expected_line_num + 1)
                    num_consecutive_line_errors += 1;
                else
                    num_consecutive_line_errors = 0;

                expected_line_num = (expected_line_num + 1) % height;
            }

            src += width;
        }

        /* Unmap segment at offset. */
        uref_block_unmap(uref, offset);
        /* Advance to next segment. */
        offset += buf_size;
        input_size -= buf_size;
    }

    /* If every line had a wrong number assume vsync is lost and restart as
     * though the discontinuity was seen. */
    if (num_consecutive_line_errors == lines_in_uref) {
        upipe_warn_va(upipe, "vsync assumed lost between lines %d and %d, please report this",
                ctx->expected_line_num+1, expected_line_num+1);
        reset_state(ctx);
        return UBASE_ERR_EXTERNAL;
    }

    /* If every transition had an error then assume vsync is lost. */
    if (sd && ctx->vbi_f1_part1 && ctx->active_f1 && ctx->vbi_f1_part2
            && ctx->vbi_f2_part1 && ctx->active_f2 && ctx->vbi_f2_part2) {
        upipe_warn_va(upipe, "vsync assumed lost between lines %d and %d, please report this",
                ctx->expected_line_num+1, expected_line_num+1);
        reset_state(ctx);
        return UBASE_ERR_EXTERNAL;
    }

    /* If no problem is found with the vsync then just pass through the uref. */
    ctx->expected_line_num = expected_line_num;
    ctx->prev_eav = prev_eav;
    ctx->prev_sav = prev_sav;
    if (ctx->discontinuity) {
        uref_flow_set_discontinuity(uref);
        ctx->discontinuity = false;
    }
    upipe_pciesdi_source_framer_output(upipe, uref, upump_p);
    return UBASE_ERR_NONE;
}

static void upipe_pciesdi_source_framer_input(struct upipe *upipe, struct uref
        *uref, struct upump **upump_p)
{
    struct upipe_pciesdi_source_framer *ctx = upipe_pciesdi_source_framer_from_upipe(upipe);

    /* There was a discontinuity in the signal.  Restart frame alignment. */
    if (ubase_check(uref_flow_get_discontinuity(uref))) {
        reset_state(ctx);
        ctx->discontinuity = true;
    }

    /* Find top of frame if not started. */
    if (!ctx->start) {
        int err = find_top_of_frame(ctx, uref);
        if (!ubase_check(err)) {
            upipe_throw_error(upipe, err);
            uref_free(uref);
            return;
        }

        /* If the top was not found release the uref. */
        if (!ctx->start)
            uref_free(uref);

        return;
    }

    if (ctx->start) {
        size_t src_size_bytes;
        int err = uref_block_size(uref, &src_size_bytes);
        if (!ubase_check(err)) {
            upipe_throw_fatal(upipe, err);
            uref_free(uref);
            return;
        }

        int sdi_width_bytes = sizeof(uint16_t) * 2 * ctx->f->width;
        int lines_in_uref = src_size_bytes / sdi_width_bytes;

        if (ctx->mode == FRAMES) {
            err = handle_frames(ctx, uref, upump_p, sdi_width_bytes, lines_in_uref);
            if (!ubase_check(err)) {
                upipe_throw_error(upipe, err);
                uref_free(uref);
                return;
            }
        }

        else if (ctx->mode == FIELDS) {
            err = handle_fields(ctx, uref, upump_p, sdi_width_bytes, lines_in_uref);
            if (!ubase_check(err)) {
                upipe_throw_error(upipe, err);
                uref_free(uref);
            }
        }

        else if (ctx->mode == CHUNKS) {
            err = handle_chunks(ctx, uref, upump_p, sdi_width_bytes, lines_in_uref);
            if (!ubase_check(err)) {
                upipe_throw_error(upipe, err);
                uref_free(uref);
            }
        }

        else if (ctx->mode == VSYNC_ONLY) {
            err = handle_vsync_only(ctx, uref, upump_p, sdi_width_bytes, lines_in_uref);
            if (!ubase_check(err)) {
                upipe_throw_error(upipe, err);
                uref_free(uref);
            }
        }
    }

    return;
}

static struct upipe_mgr upipe_pciesdi_source_framer_mgr = {
    .signature = UPIPE_PCIESDI_SOURCE_FRAMER_SIGNATURE,
    .upipe_alloc = upipe_pciesdi_source_framer_alloc,
    .upipe_input = upipe_pciesdi_source_framer_input,
    .upipe_control = upipe_pciesdi_source_framer_control,
};

struct upipe_mgr *upipe_pciesdi_source_framer_mgr_alloc(void)
{
    return &upipe_pciesdi_source_framer_mgr;
}
