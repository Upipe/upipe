/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
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

/** helper phony pipe */
static struct upipe *test_alloc(struct upipe_mgr *mgr,
                                struct uprobe *uprobe, uint32_t signature,
                                va_list args)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_init(upipe, mgr, uprobe);
    return upipe;
}

/** helper phony pipe */
static bool test_control(struct upipe *upipe, enum upipe_command command,
                         va_list args)
{
    switch (command) {
        case UPIPE_SET_UPUMP_MGR: {
            struct upump_mgr *arg = va_arg(args, struct upump_mgr *);
            assert(arg == upump_mgr);
            transferred = true;
            return true;
        }
        case UPIPE_SET_URI: {
            const char *uri = va_arg(args, const char *);
            assert(!strcmp(uri, "toto"));
            got_uri = true;
            return true;
        }
        default:
            assert(0);
    }
}

/** helper phony pipe */
static void test_free(struct upipe *upipe)
{
    free(upipe);
}

/** helper phony pipe */
static struct upipe_mgr test_mgr = {
    .upipe_alloc = test_alloc,
    .upipe_input = NULL,
    .upipe_control = test_control,
    .upipe_free = test_free,

    .upipe_mgr_free = NULL
};

static void *thread(void *_upipe_xfer_mgr)
{
    struct upipe_mgr *upipe_xfer_mgr = (struct upipe_mgr *)_upipe_xfer_mgr;

    struct ev_loop *loop = ev_loop_new(0);
    upump_mgr = upump_ev_mgr_alloc(loop, UPUMP_POOL, UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);

    assert(upipe_xfer_mgr_attach(upipe_xfer_mgr, upump_mgr));
    upipe_mgr_release(upipe_xfer_mgr);

    ev_loop(loop, 0);

    upump_mgr_release(upump_mgr);
    ev_loop_destroy(loop);

    return NULL;
}

int main(int argc, char **argv)
{
    struct upipe *upipe_test = upipe_void_alloc(&test_mgr, NULL);
    assert(upipe_test != NULL);

    struct upipe_mgr *upipe_xfer_mgr =
        upipe_xfer_mgr_alloc(XFER_QUEUE, XFER_POOL);
    assert(upipe_xfer_mgr != NULL);

    pthread_t id;
    upipe_mgr_use(upipe_xfer_mgr);
    assert(pthread_create(&id, NULL, thread, upipe_xfer_mgr) == 0);

    struct upipe *upipe_handle = upipe_xfer_alloc(upipe_xfer_mgr, NULL,
                                                  upipe_test);
    /* from now on upipe_test shouldn't be accessed from this thread */
    assert(upipe_handle != NULL);
    upipe_set_uri(upipe_handle, "toto");
    upipe_release(upipe_handle);

    upipe_mgr_release(upipe_xfer_mgr);

    assert(!pthread_join(id, NULL));
    assert(transferred);

    return 0;
}
