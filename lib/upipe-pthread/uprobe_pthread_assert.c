/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
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
 * @short probe asserting that all events come from the same thread
 */

#include <upipe/ubase.h>
#include <upipe/upump.h>
#include <upipe/uprobe.h>
#include <upipe-pthread/uprobe_pthread_assert.h>
#include <upipe/uprobe_helper_alloc.h>
#include <upipe/upipe.h>

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

/** @internal @This catches events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return an error code
 */
static int uprobe_pthread_assert_throw(struct uprobe *uprobe,
                                       struct upipe *upipe,
                                       int event, va_list args)
{
    struct uprobe_pthread_assert *uprobe_pthread_assert =
        uprobe_pthread_assert_from_uprobe(uprobe);
#ifndef NDEBUG
    switch (event) {
        case UPROBE_NEED_UPUMP_MGR: {
            struct upump_mgr **upump_mgr_p = va_arg(args, struct upump_mgr **);
            int err = uprobe_throw(uprobe->next, upipe, event, upump_mgr_p);
            if (!ubase_check(err) || *upump_mgr_p == NULL)
                return err;

            if (uprobe_pthread_assert->inited) {
                pthread_t thread_id = pthread_self();
                assert(pthread_equal(uprobe_pthread_assert->thread_id,
                                     thread_id));
            }
            return err;
        }

        case UPROBE_DEAD:
            if (uprobe_pthread_assert->inited) {
                pthread_t thread_id = pthread_self();
                assert(pthread_equal(uprobe_pthread_assert->thread_id,
                                     thread_id));
            }
            break;

        default:
            break;
    }
#endif

    return uprobe_throw_next(uprobe, upipe, event, args);
}

/** @This initializes an already allocated uprobe_pthread_assert structure.
 *
 * @param uprobe_pthread_assert pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_pthread_assert_init(
        struct uprobe_pthread_assert *uprobe_pthread_assert,
        struct uprobe *next)
{
    assert(uprobe_pthread_assert != NULL);
    struct uprobe *uprobe =
        uprobe_pthread_assert_to_uprobe(uprobe_pthread_assert);
    uprobe_pthread_assert->inited = false;
    uprobe_init(uprobe, uprobe_pthread_assert_throw, next);
    return uprobe;
}

/** @This cleans a uprobe_pthread_assert structure.
 *
 * @param uprobe_pthread_assert structure to clean
 */
void uprobe_pthread_assert_clean(
        struct uprobe_pthread_assert *uprobe_pthread_assert)
{
    assert(uprobe_pthread_assert != NULL);
    struct uprobe *uprobe =
        uprobe_pthread_assert_to_uprobe(uprobe_pthread_assert);
    uprobe_clean(uprobe);
}

#define ARGS_DECL struct uprobe *next
#define ARGS next
UPROBE_HELPER_ALLOC(uprobe_pthread_assert)
#undef ARGS
#undef ARGS_DECL

/** @This changes the thread ID used by this probe to the current thread.
 *
 * @param uprobe pointer to probe
 * @param thread_id thread ID
 * @return an error code
 */
int uprobe_pthread_assert_set(struct uprobe *uprobe, pthread_t thread_id)
{
    struct uprobe_pthread_assert *uprobe_pthread_assert =
        uprobe_pthread_assert_from_uprobe(uprobe);
    uprobe_pthread_assert->thread_id = thread_id;
    uprobe_pthread_assert->inited = true;
    return UBASE_ERR_NONE;
}
