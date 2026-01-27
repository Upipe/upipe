/*
 * Copyright (C) 2012-2017 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short unit tests for udeals (using libev)
 */

#undef NDEBUG

#include "upipe/ubase.h"
#include "upipe/udeal.h"
#include "upipe/upump.h"
#include "upump-ev/upump_ev.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

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

    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc_loop(UPUMP_POOL,
                                                          UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);

    struct upump *upump = udeal_upump_alloc(&udeal, upump_mgr, test_grab,
                                            thread, NULL);
    assert(upump != NULL);

    udeal_start(&udeal, upump);

    upump_mgr_run(upump_mgr, NULL);

    upump_free(upump);
    upump_mgr_release(upump_mgr);

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
