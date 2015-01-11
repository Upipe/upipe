/*
 * Copyright (C) 2012-2014 OpenHeadend S.A.R.L.
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
 * @short unit tests for upipe_transfer (using upump_ev)
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_upump_mgr.h>
#include <upipe/uprobe_transfer.h>
#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe/upipe.h>
#include <upipe-modules/upipe_transfer.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>

#include <ev.h>

#define UPUMP_POOL 1
#define UPUMP_BLOCKER_POOL 1
#define XFER_QUEUE 255
#define XFER_POOL 1

static struct upump_mgr *upump_mgr = NULL;
static bool transferred = false;
static bool got_uri = false;
static uatomic_uint32_t source_end;
static pthread_t xfer_thread_id;

/** helper phony pipe */
struct test_pipe {
    struct urefcount urefcount;
    struct upipe upipe;
};

/** helper phony pipe */
static void test_free(struct urefcount *urefcount)
{
    struct test_pipe *test_pipe =
        container_of(urefcount, struct test_pipe, urefcount);
    urefcount_clean(&test_pipe->urefcount);
    upipe_clean(&test_pipe->upipe);
    free(test_pipe);
}

/** helper phony pipe */
static struct upipe *test_alloc(struct upipe_mgr *mgr,
                                struct uprobe *uprobe, uint32_t signature,
                                va_list args)
{
    struct test_pipe *test_pipe = malloc(sizeof(struct test_pipe));
    assert(test_pipe != NULL);
    upipe_init(&test_pipe->upipe, mgr, uprobe);
    urefcount_init(&test_pipe->urefcount, test_free);
    test_pipe->upipe.refcount = &test_pipe->urefcount;
    return &test_pipe->upipe;
}

/** helper phony pipe */
static int test_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR: {
            transferred = true;
            assert(pthread_equal(pthread_self(), xfer_thread_id));
            return UBASE_ERR_NONE;
        }
        case UPIPE_SET_URI: {
            const char *uri = va_arg(args, const char *);
            assert(!strcmp(uri, "toto"));
            got_uri = true;
            upipe_throw_source_end(upipe);
            assert(pthread_equal(pthread_self(), xfer_thread_id));
            return UBASE_ERR_NONE;
        }
        default:
            assert(0);
            return UBASE_ERR_UNHANDLED;
    }
}

/** helper phony pipe */
static struct upipe_mgr test_mgr = {
    .refcount = NULL,
    .upipe_alloc = test_alloc,
    .upipe_input = NULL,
    .upipe_control = test_control
};

static void *thread(void *_upipe_xfer_mgr)
{
    struct upipe_mgr *upipe_xfer_mgr = (struct upipe_mgr *)_upipe_xfer_mgr;

    struct ev_loop *loop = ev_loop_new(0);
    upump_mgr = upump_ev_mgr_alloc(loop, UPUMP_POOL, UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);

    ubase_assert(upipe_xfer_mgr_attach(upipe_xfer_mgr, upump_mgr));
    upipe_mgr_release(upipe_xfer_mgr);

    ev_loop(loop, 0);

    upump_mgr_release(upump_mgr);
    ev_loop_destroy(loop);

    return NULL;
}

/** definition of our uprobe */
static int catch(struct uprobe *uprobe, struct upipe *upipe, int event, va_list args)
{
    switch (event) {
        case UPROBE_READY:
        case UPROBE_DEAD:
            break;
        case UPROBE_SOURCE_END:
            uatomic_fetch_add(&source_end, 1);;
            break;
        default:
            assert(0);
            break;
    }
    return UBASE_ERR_NONE;
}

int main(int argc, char **argv)
{
    struct ev_loop *loop = ev_default_loop(0);
    struct upump_mgr *upump_mgr =
        upump_ev_mgr_alloc(loop, UPUMP_POOL, UPUMP_BLOCKER_POOL);

    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_DEBUG);
    assert(uprobe_stdio != NULL);
    struct uprobe *uprobe_upump_mgr =
        uprobe_upump_mgr_alloc(uprobe_use(uprobe_stdio), upump_mgr);

    uatomic_init(&source_end, 0);

    struct uprobe *uprobe_xfer = uprobe_xfer_alloc(uprobe_use(uprobe_stdio));
    assert(uprobe_xfer != NULL);
    ubase_assert(uprobe_xfer_add(uprobe_xfer, UPROBE_XFER_VOID,
                                 UPROBE_SOURCE_END, 0));
    struct upipe *upipe_test = upipe_void_alloc(&test_mgr,
            uprobe_pfx_alloc(uprobe_xfer, UPROBE_LOG_VERBOSE, "test"));
    assert(upipe_test != NULL);

    struct upipe_mgr *upipe_xfer_mgr =
        upipe_xfer_mgr_alloc(XFER_QUEUE, XFER_POOL);
    assert(upipe_xfer_mgr != NULL);

    upipe_mgr_use(upipe_xfer_mgr);
    assert(pthread_create(&xfer_thread_id, NULL, thread, upipe_xfer_mgr) == 0);

    struct upipe *upipe_handle = upipe_xfer_alloc(upipe_xfer_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_upump_mgr), UPROBE_LOG_VERBOSE,
                             "xfer"),
            upipe_test);
    /* from now on upipe_test shouldn't be accessed from this thread */
    assert(upipe_handle != NULL);
    ubase_assert(upipe_attach_upump_mgr(upipe_handle));
    ubase_assert(upipe_set_uri(upipe_handle, "toto"));
    upipe_release(upipe_handle);

    upipe_mgr_release(upipe_xfer_mgr);

    ev_loop(loop, 0);

    assert(!pthread_join(xfer_thread_id, NULL));
    assert(transferred);
    assert(uatomic_load(&source_end) == 1);

    uprobe_release(uprobe_stdio);
    uprobe_release(uprobe_upump_mgr);
    upump_mgr_release(upump_mgr);
    ev_default_destroy();
    return 0;
}
