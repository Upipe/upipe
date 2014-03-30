/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
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
 * @short probe catching need_uclock events and providing a given uclock
 */

#include <upipe/ubase.h>
#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_uclock.h>
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
static enum ubase_err uprobe_uclock_throw(struct uprobe *uprobe,
                                          struct upipe *upipe,
                                          int event, va_list args)
{
    struct uprobe_uclock *uprobe_uclock =
        uprobe_uclock_from_uprobe(uprobe);
    if (event != UPROBE_NEED_UCLOCK || uprobe_uclock->uclock == NULL)
        return uprobe_throw_next(uprobe, upipe, event, args);

    struct uclock **uclock_p = va_arg(args, struct uclock **);
    *uclock_p = uclock_use(uprobe_uclock->uclock);
    return UBASE_ERR_NONE;
}

/** @This initializes an already allocated uprobe_uclock structure.
 *
 * @param uprobe_uclock pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @param uclock uref manager to provide to pipes
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_uclock_init(struct uprobe_uclock *uprobe_uclock,
                                  struct uprobe *next,
                                  struct uclock *uclock)
{
    assert(uprobe_uclock != NULL);
    struct uprobe *uprobe = uprobe_uclock_to_uprobe(uprobe_uclock);
    uprobe_uclock->uclock = uclock_use(uclock);
    uprobe_init(uprobe, uprobe_uclock_throw, next);
    return uprobe;
}

/** @This cleans a uprobe_uclock structure.
 *
 * @param uprobe_uclock structure to clean
 */
void uprobe_uclock_clean(struct uprobe_uclock *uprobe_uclock)
{
    assert(uprobe_uclock != NULL);
    struct uprobe *uprobe = uprobe_uclock_to_uprobe(uprobe_uclock);
    uclock_release(uprobe_uclock->uclock);
    uprobe_clean(uprobe);
}

#define ARGS_DECL struct uprobe *next, struct uclock *uclock
#define ARGS next, uclock
UPROBE_HELPER_ALLOC(uprobe_uclock)
#undef ARGS
#undef ARGS_DECL

/** @This changes the uclock set by this probe.
 *
 * @param uprobe pointer to probe
 * @param uclock new uref manager to provide to pipes
 */
void uprobe_uclock_set(struct uprobe *uprobe, struct uclock *uclock)
{
    struct uprobe_uclock *uprobe_uclock =
        uprobe_uclock_from_uprobe(uprobe);
    uclock_release(uprobe_uclock->uclock);
    uprobe_uclock->uclock = uclock_use(uclock);
}
