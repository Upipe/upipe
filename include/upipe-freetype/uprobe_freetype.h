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

#ifndef _UPIPE_FREETYPE_UPROBE_FREETYPE_H_
#define _UPIPE_FREETYPE_UPROBE_FREETYPE_H_
#ifdef __cplusplus
extern "C" {
#endif

/** @This enumerates the justify options. */
enum uprobe_freetype_justify {
    /** justify text to the left of the box */
    UPROBE_FREETYPE_JUSTIFY_LEFT,
    /** justify text to the right of the box */
    UPROBE_FREETYPE_JUSTIFY_RIGHT,
    /** justify text to the center of the box */
    UPROBE_FREETYPE_JUSTIFY_CENTER,
};


/** @This enumerates the vertical alignment options. */
enum uprobe_freetype_alignment {
    /** align text to the top of the box */
    UPROBE_FREETYPE_ALIGNMENT_TOP,
    /** align text to the center of the box */
    UPROBE_FREETYPE_ALIGNMENT_CENTER,
    /** align text to the bottom of the box */
    UPROBE_FREETYPE_ALIGNMENT_BOTTOM,
};

/** @This describes the text margin in the box. */
struct uprobe_freetype_margin {
    /** left margin in percent */
    unsigned left;
    /** right margin in percent */
    unsigned right;
    /** top margin in percent */
    unsigned top;
    /** bottom margin in percent */
    unsigned bottom;
};

/** @This allocates a freetype probe to size and position the text.
 *
 * @param next next probe in the list
 * @param justify text justification
 * @param margin text margin in the box
 * @return an allocate probe or NULL in case of error
 */
struct uprobe *
uprobe_freetype_alloc(struct uprobe *next,
                      enum uprobe_freetype_justify justify,
                      enum uprobe_freetype_alignment alignment,
                      const struct uprobe_freetype_margin *margin);

#ifdef __cplusplus
}
#endif
#endif
