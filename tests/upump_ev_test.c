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
 * @short unit tests for upump manager with ev event loop
 */

#undef NDEBUG

#include <upipe/upump.h>
#include <upipe/upump_blocker.h>
#include <upump-ev/upump_ev.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>

#include <ev.h>

#define UPUMP_POOL 1
#define UPUMP_BLOCKER_POOL 1

static uint64_t timeout = UINT64_C(27000000); /* 1 s */
static const char *padding = "This is an initialized bit of space used to pad sufficiently !";
/* This is an arbitrarily large number that is just supposed to be bigger than
 * the buffer space of a pipe. */
#define MIN_READ (128*1024)

static struct ev_loop *loop;
static int pipefd[2];
static struct upump_mgr *mgr;
static struct upump *write_idler;
static struct upump *read_timer;
static struct upump *write_watcher;
static struct upump *read_watcher;
static struct upump_blocker *blocker = NULL;
static ssize_t bytes_written = 0, bytes_read = 0;

static void blocker_cb(struct upump_blocker *blocker)
{
    upump_blocker_free(blocker);
}

static void write_idler_cb(struct upump *upump)
{
    ssize_t ret = write(pipefd[1], padding, strlen(padding) + 1);
    if (ret == -1 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
        printf("write idler blocked\n");
        blocker = upump_blocker_alloc(write_idler, blocker_cb, NULL, NULL);
        assert(blocker != NULL);
        upump_start(write_watcher);
        upump_start(read_timer);
    } else {
        assert(ret != -1);
        bytes_written += ret;
    }
}

static void write_watcher_cb(struct upump *unused)
{
    printf("write watcher passed\n");
    upump_blocker_free(blocker);
    upump_stop(write_watcher);
}

static void read_timer_cb(struct upump *unused)
{
    printf("read timer passed\n");
    upump_start(read_watcher);
    /* The timer is automatically stopped */
}

static void read_watcher_cb(struct upump *unused)
{
    char buffer[strlen(padding) + 1];
    ssize_t ret = read(pipefd[0], buffer, strlen(padding) + 1);
    assert(ret != -1);
    bytes_read += ret;
    if (bytes_read > MIN_READ) {
        printf("read watcher passed\n");
        upump_stop(write_idler);
        upump_stop(read_watcher);
    }
}

int main(int argc, char **argv)
{
    long flags;
    loop = ev_default_loop(0);
    mgr = upump_ev_mgr_alloc(loop, UPUMP_POOL, UPUMP_BLOCKER_POOL);
    assert(mgr != NULL);

    /* Create a pipe with non-blocking write */
    assert(pipe(pipefd) != -1);
    flags = fcntl(pipefd[1], F_GETFL);
    assert(flags != -1);
    flags |= O_NONBLOCK;
    assert(fcntl(pipefd[1], F_SETFL, flags) != -1);

    /* Create watchers */
    write_idler = upump_alloc_idler(mgr, write_idler_cb, NULL, NULL);
    assert(write_idler != NULL);
    write_watcher = upump_alloc_fd_write(mgr, write_watcher_cb, NULL, NULL,
                                         pipefd[1]);
    assert(write_watcher != NULL);
    read_timer = upump_alloc_timer(mgr, read_timer_cb, NULL, NULL, timeout, 0);
    assert(read_timer != NULL);
    read_watcher = upump_alloc_fd_read(mgr, read_watcher_cb, NULL, NULL,
                                       pipefd[0]);
    assert(read_watcher != NULL);

    /* Start tests */
    upump_start(write_idler);
    ev_loop(loop, 0);
    assert(bytes_read);
    assert(bytes_read == bytes_written);

    /* Clean up */
    upump_free(write_idler);
    upump_free(write_watcher);
    upump_free(read_timer);
    upump_free(read_watcher);
    upump_mgr_release(mgr);

    ev_default_destroy();
    return 0;
}
