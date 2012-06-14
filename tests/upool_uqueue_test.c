/*****************************************************************************
 * upool_uqueue_test.c: unit tests for upools and uqueues (using libev)
 *****************************************************************************
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
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
 *****************************************************************************/

#undef NDEBUG

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/upool.h>
#include <upipe/uqueue.h>
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

#define UPOOL_MAX_DEPTH 10
#define UQUEUE_MAX_DEPTH 9
#define NB_LOOPS 1000

static const long nsec_timeouts[UPOOL_MAX_DEPTH] = {
    0, 1000000, 5000000, 0, 50000, 0, 0, 10000000, 5000, 0
};

struct elem {
    struct uchain uchain;
    struct upool *upool;
    struct timespec timeout;
    unsigned int loop;
    unsigned int thread;
};

struct thread {
    pthread_t id;
    unsigned int thread;
    struct elem elems[UPOOL_MAX_DEPTH];
    struct upool upool;
    struct upump_mgr *upump_mgr;
    struct upump *upump;
    unsigned int loop;
};

static urefcount refcount;
static struct uqueue uqueue;
static unsigned int nb_loops = NB_LOOPS;
static unsigned int loop[2] = {0, 0};

static void push_ready(struct upump *upump)
{
    struct thread *thread = upump_get_opaque(upump, struct thread *);
    upump_mgr_sink_unblock(thread->upump_mgr);
    upump_stop(thread->upump);
}

static void push(struct upump *upump)
{
    struct thread *thread = upump_get_opaque(upump, struct thread *);
    struct uchain *uchain = upool_pop(&thread->upool);
    assert(uchain != NULL);
    struct elem *elem = container_of(uchain, struct elem, uchain);

    if (likely(elem->timeout.tv_nsec))
        assert(!nanosleep(&elem->timeout, NULL));

    elem->loop = thread->loop++;
    elem->thread = thread->thread;
    if (unlikely(thread->loop >= nb_loops)) {
        /* make it stop */
        upump_stop(upump);
        urefcount_release(&refcount);
        uqueue_push(&uqueue, uchain);
    } else if (unlikely(!uqueue_push(&uqueue, uchain))) {
        upump_mgr_sink_block(thread->upump_mgr);
        upump_start(thread->upump);
    }
}

static void *push_thread(void *_thread)
{
    struct thread *thread = (struct thread *)_thread;
    thread->loop = 0;

    struct ev_loop *loop = ev_loop_new(0);
    thread->upump_mgr = upump_ev_mgr_alloc(loop);
    assert(thread->upump_mgr != NULL);

    thread->upump = uqueue_upump_alloc_push(&uqueue, thread->upump_mgr,
                                            push_ready, thread);
    assert(thread->upump != NULL);

    upool_init(&thread->upool, UPOOL_MAX_DEPTH);
    for (int i = 0; i < UPOOL_MAX_DEPTH; i++) {
        thread->elems[i].upool = &thread->upool;
        thread->elems[i].timeout.tv_sec = 0;
        thread->elems[i].timeout.tv_nsec = nsec_timeouts[i];
        upool_push(&thread->upool, &thread->elems[i].uchain);
    }

    struct upump *upump = upump_alloc_idler(thread->upump_mgr, push, thread,
                                            true);
    assert(upump != NULL);
    assert(upump_start(upump));

    ev_loop(loop, 0);

    upump_free(upump);
    upump_free(thread->upump);
    assert(urefcount_single(&thread->upump_mgr->refcount));
    upump_mgr_release(thread->upump_mgr);
    ev_loop_destroy(loop);

    /* defer cleaning the pool to the main thread because buffers may still
     * be used in the queue */

    return NULL;
}

static void pop(struct upump *upump)
{
    struct uchain *uchain = uqueue_pop(&uqueue);
    if (likely(uchain != NULL)) {
        struct elem *elem = container_of(uchain, struct elem, uchain);
        assert(elem->loop == loop[elem->thread]);
        loop[elem->thread] = elem->loop + 1;
        if (likely(elem->timeout.tv_nsec))
            assert(!nanosleep(&elem->timeout, NULL));
        upool_push(elem->upool, uchain);
    } else if (likely(urefcount_single(&refcount)))
        upump_stop(upump);
}

int main(int argc, char **argv)
{
    if (argc > 1)
        nb_loops = atoi(argv[1]);

    struct ev_loop *loop = ev_default_loop(0);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop);
    assert(upump_mgr != NULL);

    urefcount_init(&refcount);

    assert(uqueue_init(&uqueue, UQUEUE_MAX_DEPTH));
    struct upump *upump = uqueue_upump_alloc_pop(&uqueue, upump_mgr, pop, NULL);
    assert(upump != NULL);

    struct thread threads[2];
    threads[0].thread = 0;
    threads[1].thread = 1;
    urefcount_use(&refcount);
    assert(pthread_create(&threads[0].id, NULL, push_thread, &threads[0]) == 0);
    urefcount_use(&refcount);
    assert(pthread_create(&threads[1].id, NULL, push_thread, &threads[1]) == 0);

    assert(upump_start(upump));
    ev_loop(loop, 0);

    upump_free(upump);
    assert(urefcount_single(&upump_mgr->refcount));
    upump_mgr_release(upump_mgr);
    ev_default_destroy();

    upool_clean(&threads[0].upool);
    upool_clean(&threads[1].upool);
    uqueue_clean(&uqueue);

    urefcount_clean(&refcount);

    assert(!pthread_join(threads[0].id, NULL));
    assert(!pthread_join(threads[1].id, NULL));

    return 0;
}
