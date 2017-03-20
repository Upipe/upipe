/*
 * Copyright (C) 2014-2017 OpenHeadend S.A.R.L.
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
 * @short unit tests for uprobe_pthread_upump_mgr implementation
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe-pthread/uprobe_pthread_upump_mgr.h>
#include <upipe/upipe.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

#define UPUMP_POOL 1
#define UPUMP_BLOCKER_POOL 1
#define NB_THREADS 10

static struct uprobe *uprobe;

/** helper phony pipe to test uprobe_pthread_upump_mgr */
static struct upipe *uprobe_test_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_init(upipe, mgr, uprobe);
    struct upump_mgr *m = NULL;
    upipe_throw_need_upump_mgr(upipe, &m);
    if (m != NULL) {
        upump_mgr_set_opaque(m, upipe);
        upump_mgr_release(m);
    }
    return upipe;
}

/** helper phony pipe to test uprobe_pthread_upump_mgr */
static void uprobe_test_free(struct upipe *upipe)
{
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe to test uprobe_pthread_upump_mgr */
static struct upipe_mgr uprobe_test_mgr = {
    .refcount = NULL,
    .upipe_alloc = uprobe_test_alloc,
    .upipe_input = NULL,
    .upipe_control = NULL
};

static void *thread(void *unused)
{
    struct upump_mgr *upump_mgr =
        upump_ev_mgr_alloc_loop(UPUMP_POOL, UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);

    struct upipe *upipe;
    upipe = upipe_void_alloc(&uprobe_test_mgr, uprobe_use(uprobe));
    assert(upump_mgr_get_opaque(upump_mgr, struct upipe *) == NULL);
    uprobe_test_free(upipe);

    uprobe_pthread_upump_mgr_set(uprobe, upump_mgr);
    upipe = upipe_void_alloc(&uprobe_test_mgr, uprobe_use(uprobe));
    assert(upump_mgr_get_opaque(upump_mgr, struct upipe *) == upipe);
    uprobe_test_free(upipe);
    upump_mgr_set_opaque(upump_mgr, NULL);

    uprobe_throw(uprobe, NULL, UPROBE_FREEZE_UPUMP_MGR);
    upipe = upipe_void_alloc(&uprobe_test_mgr, uprobe_use(uprobe));
    assert(upump_mgr_get_opaque(upump_mgr, struct upipe *) == NULL);
    uprobe_test_free(upipe);

    uprobe_throw(uprobe, NULL, UPROBE_THAW_UPUMP_MGR);
    upipe = upipe_void_alloc(&uprobe_test_mgr, uprobe_use(uprobe));
    assert(upump_mgr_get_opaque(upump_mgr, struct upipe *) == upipe);
    uprobe_test_free(upipe);

    upump_mgr_release(upump_mgr);
    return NULL;
}

int main(int argc, char **argv)
{
    uprobe = uprobe_pthread_upump_mgr_alloc(NULL);
    assert(uprobe != NULL);

    int i;
    pthread_t ids[NB_THREADS];
    for (i = 0; i < NB_THREADS; i++)
        assert(pthread_create(&ids[i], NULL, thread, NULL) == 0);
    for (i = 0; i < NB_THREADS; i++)
        assert(pthread_join(ids[i], NULL) == 0);

    uprobe_release(uprobe);
    return 0;
}
