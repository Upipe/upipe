/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
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
 * @short probe providing source pipe manager by catching need source pipe
 * manager events
 */

#include <upipe/uprobe_source_mgr.h>
#include <upipe/uprobe_helper_alloc.h>

/** @internal @This catches the UPROBE_NEED_SOURCE_MGR event and provides
 * the source manager.
 *
 * @param uprobe structure used to raise events
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional arguments
 * @return an error code
 */
static int catch_source_mgr(struct uprobe *uprobe, struct upipe *upipe,
                            int event, va_list args)
{
    struct uprobe_source_mgr *uprobe_source_mgr =
        uprobe_source_mgr_from_uprobe(uprobe);

    if (likely(event != UPROBE_NEED_SOURCE_MGR) ||
        unlikely(uprobe_source_mgr->source_mgr == NULL))
        return uprobe_throw_next(uprobe, upipe, event, args);

    struct upipe_mgr **source_mgr_p = va_arg(args, struct upipe_mgr **);
    *source_mgr_p = upipe_mgr_use(uprobe_source_mgr->source_mgr);
    return UBASE_ERR_NONE;
}

/** @This initializes an already allocated uprobe_source_mgr structure.
 *
 * @param uprobe_source_mgr pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @param source_mgr source pipe manager to provide to pipes
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *
uprobe_source_mgr_init(struct uprobe_source_mgr *uprobe_source_mgr,
                       struct uprobe *next,
                       struct upipe_mgr *source_mgr)
{
    struct uprobe *uprobe = uprobe_source_mgr_to_uprobe(uprobe_source_mgr);
    uprobe_init(uprobe, catch_source_mgr, next);
    uprobe_source_mgr->source_mgr = upipe_mgr_use(source_mgr);
    return uprobe;
}

/** @This cleans a uprobe_source_mgr structure.
 *
 * @param uprobe_source_mgr structure to clean
 */
void uprobe_source_mgr_clean(struct uprobe_source_mgr *uprobe_source_mgr)
{
    struct uprobe *uprobe = uprobe_source_mgr_to_uprobe(uprobe_source_mgr);
    upipe_mgr_release(uprobe_source_mgr->source_mgr);
    uprobe_clean(uprobe);
}

#define ARGS_DECL struct uprobe *next, struct upipe_mgr *source_mgr
#define ARGS next, source_mgr
UPROBE_HELPER_ALLOC(uprobe_source_mgr)
#undef ARGS
#undef ARGS_DECL
