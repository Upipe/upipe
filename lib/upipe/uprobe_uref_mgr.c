/*
 * Copyright (C) 2013-2014 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
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
 * @short probe catching provide_request events asking for a uref manager
 */

#include <upipe/ubase.h>
#include <upipe/uref.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_helper_alloc.h>
#include <upipe/upipe.h>

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/** @internal @This catches events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return an error code
 */
static int uprobe_uref_mgr_throw(struct uprobe *uprobe, struct upipe *upipe,
                                 int event, va_list args)
{
    struct uprobe_uref_mgr *uprobe_uref_mgr =
        uprobe_uref_mgr_from_uprobe(uprobe);
    if (event != UPROBE_PROVIDE_REQUEST || uprobe_uref_mgr->uref_mgr == NULL)
        return uprobe_throw_next(uprobe, upipe, event, args);

    va_list args_copy;
    va_copy(args_copy, args);
    struct urequest *urequest = va_arg(args_copy, struct urequest *);
    va_end(args_copy);
    if (urequest->type != UREQUEST_UREF_MGR)
        return uprobe_throw_next(uprobe, upipe, event, args);

    return urequest_provide_uref_mgr(urequest,
                                     uref_mgr_use(uprobe_uref_mgr->uref_mgr));
}

/** @This initializes an already allocated uprobe_uref_mgr structure.
 *
 * @param uprobe_uref_mgr pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @param uref_mgr uref manager to provide to pipes
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_uref_mgr_init(struct uprobe_uref_mgr *uprobe_uref_mgr,
                                    struct uprobe *next,
                                    struct uref_mgr *uref_mgr)
{
    assert(uprobe_uref_mgr != NULL);
    struct uprobe *uprobe = uprobe_uref_mgr_to_uprobe(uprobe_uref_mgr);
    uprobe_uref_mgr->uref_mgr = uref_mgr_use(uref_mgr);
    uprobe_init(uprobe, uprobe_uref_mgr_throw, next);
    return uprobe;
}

/** @This cleans a uprobe_uref_mgr structure.
 *
 * @param uprobe_uref_mgr structure to clean
 */
void uprobe_uref_mgr_clean(struct uprobe_uref_mgr *uprobe_uref_mgr)
{
    assert(uprobe_uref_mgr != NULL);
    struct uprobe *uprobe = uprobe_uref_mgr_to_uprobe(uprobe_uref_mgr);
    uref_mgr_release(uprobe_uref_mgr->uref_mgr);
    uprobe_clean(uprobe);
}

#define ARGS_DECL struct uprobe *next, struct uref_mgr *uref_mgr
#define ARGS next, uref_mgr
UPROBE_HELPER_ALLOC(uprobe_uref_mgr)
#undef ARGS
#undef ARGS_DECL

/** @This changes the uref_mgr set by this probe.
 *
 * @param uprobe pointer to probe
 * @param uref_mgr new uref manager to provide to pipes
 */
void uprobe_uref_mgr_set(struct uprobe *uprobe, struct uref_mgr *uref_mgr)
{
    struct uprobe_uref_mgr *uprobe_uref_mgr =
        uprobe_uref_mgr_from_uprobe(uprobe);
    uref_mgr_release(uprobe_uref_mgr->uref_mgr);
    uprobe_uref_mgr->uref_mgr = uref_mgr_use(uref_mgr);
}
