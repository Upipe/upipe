/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
            Benjamin Cohen
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
 * @short unit tests for udp source pipe
 */

#undef NDEBUG

#include <upipe/ulog.h>
#include <upipe/ulog_std.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_print.h>
#include <upipe/uclock.h>
#include <upipe/uclock_std.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/udict_dump.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe/upipe.h>
#include <upipe-modules/upipe_udp_source.h>
#include <upipe/upipe_helper_upipe.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>

#include <ev.h>

#define UDICT_POOL_DEPTH 10
#define UREF_POOL_DEPTH 10
#define UBUF_POOL_DEPTH 10
#define READ_SIZE 4096
#define ULOG_LEVEL ULOG_DEBUG

static void usage(const char *argv0) {
    fprintf(stdout, "Usage: %s <connect address>:<connect port>@<bind address>:<bind port>/<options>\n", argv0);
    fprintf(stdout, "Options include:\n");
    fprintf(stdout, " /ifindex=X (binds to a specific network interface, by link number)\n");
    fprintf(stdout, " /ifaddr=XXX.XXX.XXX.XXX (binds to a specific network interface, by address)\n");
    fprintf(stdout, " /ttl=XX (time-to-live of the UDP packet)\n");
    fprintf(stdout, " /tos=XX (sets the IPv4 Type Of Service option)\n");
    fprintf(stdout, " /tcp (binds a TCP socket instead of UDP)\n");

    exit(EXIT_FAILURE);
}

/** definition of our uprobe */
static bool catch(struct uprobe *uprobe, struct upipe *upipe,
                  enum uprobe_event event, va_list args)
{
    switch (event) {
        case UPROBE_AERROR:
        case UPROBE_UPUMP_ERROR:
        case UPROBE_WRITE_END:
        case UPROBE_NEW_FLOW:
        case UPROBE_NEED_UREF_MGR:
        case UPROBE_NEED_UPUMP_MGR:
        case UPROBE_LINEAR_NEED_UBUF_MGR:
        case UPROBE_SOURCE_NEED_FLOW_NAME:
        default:
            assert(0);
            break;
        case UPROBE_READY:
        case UPROBE_READ_END:
            break;
    }
    return true;
}

/** phony pipe to test upipe_udpsrc */
struct udpsrc_test {
    struct uref *flow;
    struct upipe upipe;
};

/** helper phony pipe to test upipe_udpsrc */
UPIPE_HELPER_UPIPE(udpsrc_test, upipe);

/** helper phony pipe to test upipe_udpsrc */
static struct upipe *udpsrc_test_alloc(struct upipe_mgr *mgr)
{
    struct udpsrc_test *udpsrc_test = malloc(sizeof(struct udpsrc_test));
    if (unlikely(!udpsrc_test)) return NULL;
    udpsrc_test->flow = NULL;
    udpsrc_test->upipe.mgr = mgr;
    return &udpsrc_test->upipe;
}

/** helper phony pipe to test upipe_udpsrc */
static bool udpsrc_test_control(struct upipe *upipe, enum upipe_command command, va_list args)
{
    if (likely(command == UPIPE_INPUT)) {
        struct uref *uref = va_arg(args, struct uref*);
        assert(uref != NULL);
        ulog_debug(upipe->ulog, "===> received input uref");
        udict_dump(uref->udict, upipe->ulog);
        uref_free(uref);
        return true;
    }
    switch (command) {
        default:
            return false;
    }
}

/** helper phony pipe to test upipe_udpsrc */
static void udpsrc_test_release(struct upipe *upipe)
{
    ulog_debug(upipe->ulog, "releasing pipe %p", upipe);
    struct udpsrc_test *udpsrc_test = udpsrc_test_from_upipe(upipe);
    if (udpsrc_test->flow) uref_free(udpsrc_test->flow);
    free(udpsrc_test);
}

/** helper phony pipe to test upipe_dup */
static struct upipe_mgr udpsrc_test_mgr = {
    .upipe_alloc = udpsrc_test_alloc,
    .upipe_control = udpsrc_test_control,
    .upipe_release = udpsrc_test_release,
    .upipe_use = NULL,

    .upipe_mgr_release = NULL
};


int main(int argc, char *argv[])
{
    if (argc < 2) {
        usage(argv[0]);
    }
    char *udp_uri = argv[1];

    struct ev_loop *loop = ev_default_loop(0);
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    assert(uref_mgr != NULL);
    struct ubuf_mgr *ubuf_mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH,
                                                         UBUF_POOL_DEPTH,
                                                         umem_mgr, -1, -1,
                                                         -1, 0);
    assert(ubuf_mgr != NULL);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop);
    assert(upump_mgr != NULL);
    struct uclock *uclock = uclock_std_alloc(0);
    assert(uclock != NULL);
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_print = uprobe_print_alloc(&uprobe, stdout, "test");
    assert(uprobe_print != NULL);

     struct upipe *udpsrc_test = upipe_alloc(&udpsrc_test_mgr, uprobe_print, ulog_std_alloc(stdout, ULOG_LEVEL, "udpsrc_test"));

   
    struct upipe_mgr *upipe_udpsrc_mgr = upipe_udpsrc_mgr_alloc();
    assert(upipe_udpsrc_mgr != NULL);
    struct upipe *upipe_udpsrc = upipe_alloc(upipe_udpsrc_mgr, uprobe_print,
            ulog_std_alloc(stdout, ULOG_LEVEL, "udp source"));
    assert(upipe_udpsrc != NULL);
    assert(upipe_set_upump_mgr(upipe_udpsrc, upump_mgr));
    assert(upipe_set_uref_mgr(upipe_udpsrc, uref_mgr));
    assert(upipe_linear_set_ubuf_mgr(upipe_udpsrc, ubuf_mgr));
    assert(upipe_linear_set_output(upipe_udpsrc, udpsrc_test));
    assert(upipe_source_set_read_size(upipe_udpsrc, READ_SIZE));
    assert(upipe_source_set_flow_name(upipe_udpsrc, "udp0"));
    assert(upipe_set_uclock(upipe_udpsrc, uclock));
    assert(upipe_udpsrc_set_uri(upipe_udpsrc, udp_uri));

    ev_loop(loop, 0);

    upipe_release(upipe_udpsrc);
    upipe_mgr_release(upipe_udpsrc_mgr); // nop

    upump_mgr_release(upump_mgr);
    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uclock_release(uclock);
    uprobe_print_free(uprobe_print);

    ev_default_destroy();
    return 0;
}
