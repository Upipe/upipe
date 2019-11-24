/*
 * Copyright (C) 2019 OpenHeadend S.A.R.L.
 *
 * Authors: Arnaud de Turckheim
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

#include <upipe/uprobe.h>
#include <upipe/uprobe_helper_alloc.h>
#include <upipe/uprobe_helper_uprobe.h>
#include <upipe/uref_pic_flow.h>
#include <upipe-freetype/upipe_freetype.h>
#include <upipe-freetype/uprobe_freetype.h>


struct uprobe_freetype {
    struct uprobe uprobe;
    enum uprobe_freetype_justify justify;
    enum uprobe_freetype_alignment alignment;
    struct uprobe_freetype_margin margin;
};

UPROBE_HELPER_UPROBE(uprobe_freetype, uprobe);

static int uprobe_freetype_catch(struct uprobe *uprobe,
                                 struct upipe *upipe,
                                 int event, va_list args)
{
    if (event != UPROBE_FREETYPE_NEW_TEXT ||
        ubase_get_signature(args) != UPIPE_FREETYPE_SIGNATURE)
        goto out;

    struct uprobe_freetype *uprobe_freetype =
        uprobe_freetype_from_uprobe(uprobe);
    UBASE_SIGNATURE_CHECK(args, UPIPE_FREETYPE_SIGNATURE);
    va_list args_copy;
    va_copy(args_copy, args);
    const char *text = va_arg(args_copy, const char *);
    va_end(args_copy);

    struct uref *flow_def = NULL;
    if (unlikely(!ubase_check(upipe_get_flow_def(upipe, &flow_def))))
        goto out;

    uint64_t hsize = 0;
    uint64_t vsize = 0;
    if (unlikely(!ubase_check(uref_pic_flow_get_hsize(flow_def, &hsize)) ||
                 !ubase_check(uref_pic_flow_get_vsize(flow_def, &vsize))))
        goto out;

    struct upipe_freetype_metrics metrics;
    if (unlikely(!ubase_check(upipe_freetype_get_metrics(upipe, &metrics))))
        goto out;

    uint64_t width = 0;
    if (unlikely(!ubase_check(
                upipe_freetype_get_advance(upipe, text, &width, NULL)) ||
            !width))
        goto out;

    unsigned scale = metrics.units_per_EM;
    unsigned height = metrics.y.max - metrics.y.min;
    unsigned hscale = 100 - (uprobe_freetype->margin.left +
                             uprobe_freetype->margin.right);
    unsigned vscale = 100 - (uprobe_freetype->margin.top +
                             uprobe_freetype->margin.bottom);
    unsigned hpx = (hsize * scale * hscale) / (width * 100);
    unsigned vpx = (vsize * scale * vscale) / (height * 100);
    unsigned px = hpx > vpx ? vpx : hpx;

    struct upipe_freetype_bbox bbox;
    if (unlikely(!ubase_check(upipe_freetype_set_pixel_size(upipe, px)) ||
                 !ubase_check(upipe_freetype_get_bbox(upipe, text, &bbox))))
        return uprobe_throw_next(uprobe, upipe, event, args);

    int64_t xoff = 0;
    switch (uprobe_freetype->justify) {
        case UPROBE_FREETYPE_JUSTIFY_LEFT:
            xoff = 0;
            break;
        case UPROBE_FREETYPE_JUSTIFY_RIGHT:
            xoff = hsize - bbox.width;
            break;
        case UPROBE_FREETYPE_JUSTIFY_CENTER:
            xoff = (hsize - bbox.width) / 2;
            break;
    }

    int64_t yoff = 0;
    switch (uprobe_freetype->alignment) {
        case UPROBE_FREETYPE_ALIGNMENT_TOP:
            yoff = metrics.y.max * px / scale;
            break;

        case UPROBE_FREETYPE_ALIGNMENT_CENTER:
            yoff = (metrics.y.max * px / scale) +
                (vsize - (height * px / scale)) / 2;
            break;

        case UPROBE_FREETYPE_ALIGNMENT_BOTTOM:
            yoff = vsize + metrics.y.min * px / scale;
            break;
    }

    upipe_freetype_set_baseline(upipe, xoff, yoff);

out:
    return uprobe_throw_next(uprobe, upipe, event, args);
}

static struct uprobe *
uprobe_freetype_init(struct uprobe_freetype *uprobe_freetype,
                     struct uprobe *next,
                     enum uprobe_freetype_justify justify,
                     enum uprobe_freetype_alignment alignment,
                     const struct uprobe_freetype_margin *margin)
{
    struct uprobe *uprobe = uprobe_freetype_to_uprobe(uprobe_freetype);
    uprobe_freetype->justify = justify;
    uprobe_freetype->alignment = alignment;
    uprobe_freetype->margin.left = 0;
    uprobe_freetype->margin.right = 0;
    uprobe_freetype->margin.top = 0;
    uprobe_freetype->margin.bottom = 0;
    if (margin)
        uprobe_freetype->margin = *margin;
    uprobe_init(uprobe, uprobe_freetype_catch, next);
    return uprobe;
}

static void uprobe_freetype_clean(struct uprobe_freetype *uprobe_freetype)
{
    uprobe_clean(uprobe_freetype_to_uprobe(uprobe_freetype));
}

#define ARGS_DECL \
    struct uprobe *next, \
    enum uprobe_freetype_justify justify, \
    enum uprobe_freetype_alignment alignment, \
    const struct uprobe_freetype_margin *margin
#define ARGS next, justify, alignment, margin
UPROBE_HELPER_ALLOC(uprobe_freetype)
#undef ARGS
#undef ARGS_DECL
