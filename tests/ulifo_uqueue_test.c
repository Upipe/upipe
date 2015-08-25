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
 * @short unit tests for ulifos and uqueues (using libev)
 */

#undef NDEBUG

#include <upipe/ubase.h>
#include <upipe/uatomic.h>
#include <upipe/ulifo.h>
#include <upipe/uqueue.h>
#include <upipe/upump.h>
#include <upipe/upump_blocker.h>
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

#define ULIFO_MAX_DEPTH 10
#define UQUEUE_MAX_DEPTH 6
#define UPUMP_POOL 1
#define UPUMP_BLOCKER_POOL 1
#define NB_LOOPS 1000

struct elem {
    struct uchain uchain;
    struct timespec timeout;
    unsigned int loop;
    unsigned int thread;
};

struct thread {
    pthread_t id;
    unsigned int thread;
    struct upump_mgr *upump_mgr;
    struct upump *upump;
    struct upump_blocker *blocker;
    unsigned int loop;
};

static uatomic_uint32_t refcount;
static struct ulifo ulifo;
static struct uqueue uqueue;
struct elem elems[ULIFO_MAX_DEPTH];
static unsigned int nb_loops = NB_LOOPS;
static unsigned int loop[2] = {0, 0};

static void push_ready(struct upump *upump)
{
    struct thread *thread = upump_get_opaque(upump, struct thread *);
    upump_blocker_free(thread->blocker);
    upump_stop(thread->upump);
}

static void push(struct upump *upump)
{
    struct thread *thread = upump_get_opaque(upump, struct thread *);
    struct uchain *uchain = ulifo_pop(&ulifo, struct uchain *);
    assert(uchain != NULL);
    struct elem *elem = container_of(uchain, struct elem, uchain);

    if (likely(elem->timeout.tv_nsec))
        assert(!nanosleep(&elem->timeout, NULL));

    elem->loop = thread->loop++;
    elem->thread = thread->thread;
    if (unlikely(!uqueue_push(&uqueue, uchain))) {
        ulifo_push(&ulifo, uchain);
        thread->loop--;
        thread->blocker = upump_blocker_alloc(upump, NULL, NULL);
        upump_start(thread->upump);
    } else if (unlikely(thread->loop >= nb_loops)) {
        /* make it stop */
        upump_stop(upump);
        uatomic_fetch_sub(&refcount, 1);
        /* trigger a spurious write event so that we unblock the reader */
        ueventfd_write(&uqueue.event_pop);
    }
}

static void *push_thread(void *_thread)
{
    struct thread *thread = (struct thread *)_thread;
    thread->loop = 0;

    struct ev_loop *loop = ev_loop_new(0);
    thread->upump_mgr = upump_ev_mgr_alloc(loop, UPUMP_POOL,
                                           UPUMP_BLOCKER_POOL);
    assert(thread->upump_mgr != NULL);

    thread->upump = uqueue_upump_alloc_push(&uqueue, thread->upump_mgr,
                                            push_ready, thread);
    assert(thread->upump != NULL);
    thread->blocker = NULL;

    struct upump *upump = upump_alloc_idler(thread->upump_mgr, push, thread);
    assert(upump != NULL);
    upump_start(upump);

    ev_loop(loop, 0);

    upump_free(upump);
    upump_free(thread->upump);
    upump_mgr_release(thread->upump_mgr);
    ev_loop_destroy(loop);

    return NULL;
}

static void pop(struct upump *upump)
{
    struct uchain *uchain = uqueue_pop(&uqueue, struct uchain *);
    if (likely(uchain != NULL)) {
        struct elem *elem = container_of(uchain, struct elem, uchain);
        assert(elem->loop == loop[elem->thread]);
        loop[elem->thread] = elem->loop + 1;
        if (likely(elem->timeout.tv_nsec))
            assert(!nanosleep(&elem->timeout, NULL));
        ulifo_push(&ulifo, uchain);
    }
    if (unlikely(uatomic_load(&refcount) == 1))
        upump_stop(upump);
}

int main(int argc, char **argv)
{
    static const long nsec_timeouts[ULIFO_MAX_DEPTH] = {
        0, 1000000, 5000000, 0, 50000, 0, 0, 10000000, 5000, 0
    };
    uint8_t ulifo_buffer[ulifo_sizeof(ULIFO_MAX_DEPTH)];
    uint8_t uqueue_buffer[uqueue_sizeof(UQUEUE_MAX_DEPTH)];

    if (argc > 1)
        nb_loops = atoi(argv[1]);

    struct ev_loop *loop = ev_default_loop(0);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop, UPUMP_POOL,
                                                     UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);

    uatomic_init(&refcount, 1);

    ulifo_init(&ulifo, ULIFO_MAX_DEPTH, ulifo_buffer);
    for (int i = 0; i < ULIFO_MAX_DEPTH; i++) {
        elems[i].timeout.tv_sec = 0;
        elems[i].timeout.tv_nsec = nsec_timeouts[i];
        ulifo_push(&ulifo, &elems[i].uchain);
    }

    assert(uqueue_init(&uqueue, UQUEUE_MAX_DEPTH, uqueue_buffer));
    struct upump *upump = uqueue_upump_alloc_pop(&uqueue, upump_mgr, pop, NULL);
    assert(upump != NULL);

    struct thread threads[2];
    threads[0].thread = 0;
    threads[1].thread = 1;
    uatomic_fetch_add(&refcount, 1);
    assert(pthread_create(&threads[0].id, NULL, push_thread, &threads[0]) == 0);
    uatomic_fetch_add(&refcount, 1);
    assert(pthread_create(&threads[1].id, NULL, push_thread, &threads[1]) == 0);

    upump_start(upump);
    ev_loop(loop, 0);

    upump_free(upump);
    upump_mgr_release(upump_mgr);
    ev_default_destroy();

    ulifo_clean(&ulifo);
    uqueue_clean(&uqueue);

    uatomic_clean(&refcount);

    assert(!pthread_join(threads[0].id, NULL));
    assert(!pthread_join(threads[1].id, NULL));

    return 0;
}
