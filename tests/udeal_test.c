/*
 * Copyright (C) 2012-2015 OpenHeadend S.A.R.L.
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
 * @short unit tests for udeals (using libev)
 */

#undef NDEBUG

#include <upipe/ubase.h>
#include <upipe/udeal.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

#include <ev.h>

#define UPUMP_POOL 1
#define UPUMP_BLOCKER_POOL 1
#define NB_LOOPS 1000
#define NB_TIMEOUTS 10

static const long nsec_timeouts[NB_TIMEOUTS] = {
    0, 1000000, 5000000, 0, 50000, 0, 0, 10000000, 5000, 0
};

struct thread {
    pthread_t id;
    unsigned int thread;
    unsigned int loop;
};

static unsigned int counter; /* intentionally unprotected */
static struct udeal udeal;
static unsigned int nb_loops = NB_LOOPS;

static void test_grab(struct upump *upump)
{
    struct thread *thread = upump_get_opaque(upump, struct thread *);
    unsigned int index;

    if (unlikely(thread->loop >= nb_loops)) {
        udeal_abort(&udeal, upump);
        return;
    }

    if (unlikely(!udeal_grab(&udeal)))
        return;

    index = thread->loop % NB_TIMEOUTS;
    if (likely(nsec_timeouts[index])) {
        struct timespec ts = { .tv_sec = 0, .tv_nsec = nsec_timeouts[index] };
        assert(!nanosleep(&ts, NULL));
    }
    counter++;
    thread->loop++;

    udeal_yield(&udeal, upump);

    index = thread->loop % NB_TIMEOUTS;
    if (likely(nsec_timeouts[index])) {
        struct timespec ts = { .tv_sec = 0, .tv_nsec = nsec_timeouts[index] };
        assert(!nanosleep(&ts, NULL));
    }

    thread->loop++;
    udeal_start(&udeal, upump);
}

static void *test_thread(void *_thread)
{
    struct thread *thread = (struct thread *)_thread;
    /* so that they both start at different places */
    thread->loop = thread->thread;

    struct ev_loop *loop = ev_loop_new(0);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop, UPUMP_POOL,
                                                     UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);

    struct upump *upump = udeal_upump_alloc(&udeal, upump_mgr, test_grab,
                                            thread, NULL);
    assert(upump != NULL);

    udeal_start(&udeal, upump);

    ev_loop(loop, 0);

    upump_free(upump);
    upump_mgr_release(upump_mgr);
    ev_loop_destroy(loop);

    return NULL;
}

int main(int argc, char **argv)
{
    if (argc > 1)
        nb_loops = atoi(argv[1]);

    assert(udeal_init(&udeal));

    struct thread threads[2];
    threads[0].thread = 0;
    threads[1].thread = 1;
    assert(pthread_create(&threads[0].id, NULL, test_thread, &threads[0]) == 0);
    assert(pthread_create(&threads[1].id, NULL, test_thread, &threads[1]) == 0);

    assert(!pthread_join(threads[0].id, NULL));
    assert(!pthread_join(threads[1].id, NULL));

    assert(counter == nb_loops);
    udeal_clean(&udeal);

    return 0;
}
