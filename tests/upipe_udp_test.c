/*
 * Copyright (C) 2012-2015 OpenHeadend S.A.R.L.
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

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_upump_mgr.h>
#include <upipe/uprobe_uclock.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/uclock.h>
#include <upipe/uclock_std.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_std.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe/upipe.h>
#include <upipe-modules/upipe_udp_source.h>
#include <upipe-modules/upipe_udp_sink.h>
#include <upipe/upipe_helper_upipe.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <ev.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UBUF_POOL_DEPTH 0
#define UPUMP_POOL 0
#define UPUMP_BLOCKER_POOL 0
#define READ_SIZE 4096
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG
#define BUF_SIZE 256
#define FORMAT "This is packet number %d"

/* FIXME: uncomment or remove */
/*static void usage(const char *argv0) {
    fprintf(stdout, "Usage: %s <connect address>:<connect port>@<bind address>:<bind port>/<options>\n", argv0);
    fprintf(stdout, "Options include:\n");
    fprintf(stdout, " /ifindex=X (binds to a specific network interface, by link number)\n");
    fprintf(stdout, " /ifaddr=XXX.XXX.XXX.XXX (binds to a specific network interface, by address)\n");
    fprintf(stdout, " /ttl=XX (time-to-live of the UDP packet)\n");
    fprintf(stdout, " /tos=XX (sets the IPv4 Type Of Service option)\n");
    fprintf(stdout, " /tcp (binds a TCP socket instead of UDP)\n");

    exit(EXIT_FAILURE);
}*/

int sockfd;
struct ubuf_mgr *ubuf_mgr;
struct uref_mgr *uref_mgr;
struct upump *write_pump;
struct addrinfo hints, *servinfo, *p;
struct upipe *upipe_udpsrc;
struct upipe *upipe_udpsink;
static int counter = 0;

/** definition of our uprobe */
static int catch(struct uprobe *uprobe, struct upipe *upipe,
                 int event, va_list args)
{
    switch (event) {
        default:
            assert(0);
            break;
        case UPROBE_READY:
        case UPROBE_DEAD:
        case UPROBE_NEW_FLOW_DEF:
        case UPROBE_SOURCE_END:
            break;
    }
    return UBASE_ERR_NONE;
}

/** helper phony pipe */
struct udpsrc_test {
	int counter;
    struct uref *flow;
    struct upipe upipe;
};

/** helper phony pipe */
UPIPE_HELPER_UPIPE(udpsrc_test, upipe, 0);

/** helper phony pipe */
static struct upipe *test_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe,
                                uint32_t signature, va_list args)
{
    struct udpsrc_test *udpsrc_test = malloc(sizeof(struct udpsrc_test));
    assert(udpsrc_test != NULL);
    udpsrc_test->flow = NULL;
    udpsrc_test->counter = 0;
	upipe_init(&udpsrc_test->upipe, mgr, uprobe);
    upipe_throw_ready(&udpsrc_test->upipe);
    return &udpsrc_test->upipe;
}

/** helper phony pipe */
static void test_input(struct upipe *upipe, struct uref *uref,
                       struct upump **upump_p)
{
	uint8_t buf[BUF_SIZE], str[BUF_SIZE];
	const uint8_t *rbuf;
	struct udpsrc_test *udpsrc_test = udpsrc_test_from_upipe(upipe);
    assert(uref != NULL);

    if ((rbuf = uref_block_peek(uref, 0, -1, buf))) {
        upipe_dbg_va(upipe, "Received string: %s", rbuf);
        snprintf((char *)str, sizeof(str), FORMAT, udpsrc_test->counter);
        assert(strncmp((char *)str, (char *)rbuf, BUF_SIZE) == 0);
        udpsrc_test->counter++;
        uref_block_peek_unmap(uref, 0, buf, rbuf);
    }

    uref_free(uref);
}

/** helper phony pipe */
static int test_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF:
            return UBASE_ERR_NONE;
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *urequest = va_arg(args, struct urequest *);
            return upipe_throw_provide_request(upipe, urequest);
        }
        case UPIPE_UNREGISTER_REQUEST:
            return UBASE_ERR_NONE;
        default:
            assert(0);
            return UBASE_ERR_UNHANDLED;
    }
}

/** helper phony pipe */
static void test_free(struct upipe *upipe)
{
    upipe_dbg_va(upipe, "releasing pipe %p", upipe);
    upipe_throw_dead(upipe);
    struct udpsrc_test *udpsrc_test = udpsrc_test_from_upipe(upipe);
    if (udpsrc_test->flow)
        uref_free(udpsrc_test->flow);
	upipe_clean(upipe);
    free(udpsrc_test);
}

/** helper phony pipe */
static struct upipe_mgr udpsrc_test_mgr = {
    .refcount = NULL,
    .signature = 0,
    .upipe_alloc = test_alloc,
    .upipe_input = test_input,
    .upipe_control = test_control
};

/* packet generator */
static void genpackets(struct upump *unused)
{
	int i;
	uint8_t buf[BUF_SIZE];
	memset(buf, 0, sizeof(buf));
	printf("Counter: %d\n", counter);
	if (counter > 100) {
		upump_stop(write_pump);
		upipe_set_uri(upipe_udpsrc, NULL);
		return;
	}
	for (i=0; i < 10; i++) {
		snprintf((char *)buf, BUF_SIZE, FORMAT, counter);
		counter++;
		sendto(sockfd, buf, BUF_SIZE, 0, p->ai_addr, p->ai_addrlen);
	}
}

/* packet generator */
static void genpackets2(struct upump *upump)
{
    struct uref *uref;
    uint8_t *buf;
	int i, size = -1;

	printf("Counter: %d\n", counter);
	if (counter > 200) {
		upump_stop(write_pump);
		upipe_set_uri(upipe_udpsrc, NULL);
		return;
	}

	for (i=0; i < 10; i++) {
        uref = uref_block_alloc(uref_mgr, ubuf_mgr, BUF_SIZE);
        uref_block_write(uref, 0, &size, &buf);
        assert(size == BUF_SIZE);
        memset(buf, 0, size);
		snprintf((char *)buf, BUF_SIZE, FORMAT, counter);
        uref_block_unmap(uref, 0);
		counter++;
        upipe_input(upipe_udpsink, uref, NULL);
	}
}

int main(int argc, char *argv[])
{
	char udp_uri[512], port_str[8];
	int i, port;
	bool ret;

	/* env */
    struct ev_loop *loop = ev_default_loop(0);
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    assert(uref_mgr != NULL);
    ubuf_mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH,
                                                         umem_mgr, -1, 0);
    assert(ubuf_mgr != NULL);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop, UPUMP_POOL,
                                                     UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);
    struct uclock *uclock = uclock_std_alloc(0);
    assert(uclock != NULL);
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_LEVEL);
    assert(logger != NULL);
    logger = uprobe_uref_mgr_alloc(logger, uref_mgr);
    assert(logger != NULL);
    logger = uprobe_upump_mgr_alloc(logger, upump_mgr);
    assert(logger != NULL);
    logger = uprobe_uclock_alloc(logger, uclock);
    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_POOL_DEPTH);
    assert(logger != NULL);

    struct upipe *udpsrc_test = upipe_void_alloc(&udpsrc_test_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "udpsrc_test"));


	/* udpsrc */
    struct upipe_mgr *upipe_udpsrc_mgr = upipe_udpsrc_mgr_alloc();
    assert(upipe_udpsrc_mgr != NULL);
    upipe_udpsrc = upipe_void_alloc(upipe_udpsrc_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "udp source"));
    assert(upipe_udpsrc != NULL);
    ubase_assert(upipe_set_output(upipe_udpsrc, udpsrc_test));
    ubase_assert(upipe_set_output_size(upipe_udpsrc, READ_SIZE));
    ubase_assert(upipe_attach_uclock(upipe_udpsrc));
	srand(42);

    upipe_set_uri(upipe_udpsrc, "@127.0.0.1:42125");
    upipe_set_uri(upipe_udpsrc, NULL);

	for (i=0; i < 10; i++) {
		port = ((rand() % 40000) + 1024);
		snprintf(udp_uri, sizeof(udp_uri), "@127.0.0.1:%d", port);
		printf("Trying uri: %s ...\n", udp_uri);
		if (( ret = ubase_check(upipe_set_uri(upipe_udpsrc, udp_uri)) )) {
			break;
		}
	}
	assert(ret);

	/* open client socket */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	snprintf(port_str, sizeof(port_str), "%d", port);
	assert(getaddrinfo("127.0.0.1", port_str, &hints, &servinfo) == 0);
	for (p = servinfo; p != NULL; p = p->ai_next) {
		if (( sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
	        continue;
		}
		break;
	}
	assert(p);

    write_pump = upump_alloc_idler(upump_mgr, genpackets, NULL);
	assert(write_pump);
	upump_start(write_pump);

	/* fire */
    ev_loop(loop, 0);

	assert(udpsrc_test_from_upipe(udpsrc_test)->counter == 110);
    close(sockfd);
	upump_free(write_pump);

    /* now test upipe_udp_sink */
    struct uref *flow_def = uref_block_flow_alloc_def(uref_mgr, "bar");
    struct upipe_mgr *upipe_udpsink_mgr = upipe_udpsink_mgr_alloc();
    assert(upipe_udpsink_mgr != NULL);
    upipe_udpsink = upipe_void_alloc(upipe_udpsink_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "udp sink"));
    assert(upipe_udpsink != NULL);
    ubase_assert(upipe_set_flow_def(upipe_udpsink, flow_def));
    uref_free(flow_def);

    /* reset source uri */
	for (i=0; i < 10; i++) {
		port = ((rand() % 40000) + 1024);
		snprintf(udp_uri, sizeof(udp_uri), "@127.0.0.1:%d", port);
		printf("Trying uri: %s ...\n", udp_uri);
		if (( ret = ubase_check(upipe_set_uri(upipe_udpsrc, udp_uri)) )) {
			break;
		}
	}
	assert(ret);
    ubase_assert(upipe_udpsink_set_uri(upipe_udpsink, udp_uri+1, 0));

    /* redefine write pump */
    write_pump = upump_alloc_idler(upump_mgr, genpackets2, NULL);
    assert(write_pump);
    upump_start(write_pump);

    /* fire again */
    ev_loop(loop, 0);

	/* release */
    upump_free(write_pump);
    upipe_release(upipe_udpsrc);
    upipe_release(upipe_udpsink);
    test_free(udpsrc_test);
    upipe_mgr_release(upipe_udpsrc_mgr); /* nop */
    upump_mgr_release(upump_mgr);
    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uclock_release(uclock);
    uprobe_release(logger);
    uprobe_clean(&uprobe);

	freeaddrinfo(servinfo);

    ev_default_destroy();
    return 0;
}
