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
 * @short unit tests for avformat source and sink pipes
 */

#undef NDEBUG

#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_log.h>
#include <upipe/uclock.h>
#include <upipe/uclock_std.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_block.h>
#include <upipe/uref_flow.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe/upipe.h>
#include <upipe-av/uref_av_flow.h>
#include <upipe-av/upipe_av.h>
#include <upipe-av/upipe_avformat_source.h>
//#include <upipe-av/upipe_avformat_sink.h>

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
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

struct ubuf_mgr *ubuf_mgr;
static struct uprobe *log;
static struct upipe *upipe_avfsrc;
static struct ulist upipe_avfsrc_outputs;

struct test_output {
    struct uchain uchain;
    struct upipe *upipe_avfsrc_output;
};

static void usage(const char *argv0) {
    fprintf(stdout, "Usage: %s [-d <delay>] [-a|-o] <source file> <sink file>\n", argv0);
    fprintf(stdout, "-a : append\n");
    fprintf(stdout, "-o : overwrite\n");
    exit(EXIT_FAILURE);
}

/** helper phony pipe to test upipe_avformat */
struct avformat_test {
    bool got_flow_def;
    unsigned int nb_packets;
    size_t octets;
    struct upipe upipe;
};

/** helper phony pipe to test upipe_avfsrc */
static struct upipe *avformat_test_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe)
{
    struct avformat_test *avformat_test = malloc(sizeof(struct avformat_test));
    assert(avformat_test != NULL);
    upipe_init(&avformat_test->upipe, mgr, uprobe);
    avformat_test->got_flow_def = false;
    avformat_test->nb_packets = 0;
    avformat_test->octets = 0;
    upipe_throw_ready(&avformat_test->upipe);
    return &avformat_test->upipe;
}

/** helper phony pipe to test upipe_avfsrc */
static void avformat_test_input(struct upipe *upipe, struct uref *uref,
                                struct upump *upump)
{
    struct avformat_test *avformat_test =
        container_of(upipe, struct avformat_test, upipe);
    assert(uref != NULL);
    const char *def;
    if (uref_flow_get_def(uref, &def)) {
        assert(!avformat_test->got_flow_def);
        avformat_test->got_flow_def = true;
        upipe_dbg_va(upipe, "got flow definition %s", def);
        uref_free(uref);
        return;
    }

    assert(avformat_test->got_flow_def);
    size_t size;
    assert(uref_block_size(uref, &size));
    uref_free(uref);
    avformat_test->nb_packets++;
    avformat_test->octets += size;
}

/** helper phony pipe to test upipe_avfsrc */
static void avformat_test_free(struct upipe *upipe)
{
    struct avformat_test *avformat_test =
        container_of(upipe, struct avformat_test, upipe);
    upipe_dbg_va(upipe, "got %u packets totalizing %zu octets",
                 avformat_test->nb_packets, avformat_test->octets);
    upipe_throw_dead(upipe);
    upipe_clean(upipe);
    free(avformat_test);
}

/** helper phony pipe to test upipe_avfsrc */
static struct upipe_mgr avformat_test_mgr = {
    .upipe_alloc = avformat_test_alloc,
    .upipe_input = avformat_test_input,
    .upipe_control = NULL,
    .upipe_free = avformat_test_free,

    .upipe_mgr_free = NULL
};

/** definition of our uprobe */
static bool catch(struct uprobe *uprobe, struct upipe *upipe,
                  enum uprobe_event event, va_list args)
{
    switch (event) {
        default:
            assert(0);
            break;
        case UPROBE_READY:
        case UPROBE_DEAD:
        case UPROBE_READ_END:
        case UPROBE_CLOCK_REF:
        case UPROBE_CLOCK_TS:
        case UPROBE_SPLIT_DEL_FLOW:
            break;
        case UPROBE_SPLIT_ADD_FLOW: {
            uint64_t flow_id = va_arg(args, uint64_t);
            struct uref *flow_def = va_arg(args, struct uref *);
            const char *def;
            assert(uref_flow_get_def(flow_def, &def));
            if (ubase_ncmp(def, "block.")) {
                upipe_warn_va(upipe,
                              "flow def %s is not supported by unit test", def);
                break;
            }

            uint64_t id;
            assert(uref_av_flow_get_id(flow_def, &id));

            struct test_output *output = malloc(sizeof(struct test_output));
            assert(output != NULL);
            uchain_init(&output->uchain);
            ulist_add(&upipe_avfsrc_outputs, &output->uchain);
            output->upipe_avfsrc_output = upipe_alloc_output(upipe_avfsrc,
                    uprobe_pfx_adhoc_alloc_va(log, UPROBE_LOG_LEVEL,
                                              "output %"PRIu64, id));
            assert(output->upipe_avfsrc_output != NULL);
            struct upipe *upipe_sink = upipe_alloc(&avformat_test_mgr,
                    uprobe_pfx_adhoc_alloc_va(log, UPROBE_LOG_LEVEL,
                                              "sink %"PRIu64, id));
            assert(upipe_sink != NULL);
            assert(upipe_set_flow_def(output->upipe_avfsrc_output, flow_def));
            assert(upipe_set_ubuf_mgr(output->upipe_avfsrc_output, ubuf_mgr));
            assert(upipe_set_output(output->upipe_avfsrc_output, upipe_sink));
            upipe_release(upipe_sink);
            break;
        }
    }
    return true;
}

int main(int argc, char *argv[])
{
    const char *src_url, *sink_url;
    ulist_init(&upipe_avfsrc_outputs);
#if 0
    uint64_t delay = 0;
    int opt;
    while ((opt = getopt(argc, argv, "d:ao")) != -1) {
        switch (opt) {
            case 'd':
                delay = atoi(optarg);
                break;
            case 'a':
                mode = UPIPE_FSINK_APPEND;
                break;
            case 'o':
                mode = UPIPE_FSINK_OVERWRITE;
                break;
            default:
                usage(argv[0]);
        }
    }
#endif
    if (optind >= argc -1)
        usage(argv[0]);
    src_url = argv[optind++];
    sink_url = argv[optind++];

    struct ev_loop *loop = ev_default_loop(0);
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    assert(uref_mgr != NULL);
    ubuf_mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH,
                                        UBUF_POOL_DEPTH, umem_mgr,
                                        -1, -1, -1, 0);
    assert(ubuf_mgr != NULL);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop);
    assert(upump_mgr != NULL);
#if 0
    struct uclock *uclock = uclock_std_alloc(0);
    assert(uclock != NULL);
#endif
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_LEVEL);
    assert(uprobe_stdio != NULL);
    log = uprobe_log_alloc(uprobe_stdio, UPROBE_LOG_LEVEL);
    assert(log != NULL);

    assert(upipe_av_init(false));

#if 0
    struct upipe_mgr *upipe_avfsink_mgr = upipe_avfsink_mgr_alloc();
    assert(upipe_avfsink_mgr != NULL);
    struct upipe *upipe_avfsink = upipe_alloc(upipe_avfsink_mgr,
            uprobe_pfx_adhoc_alloc(log, UPROBE_LOG_LEVEL, "file sink"));
    assert(upipe_avfsink != NULL);
    assert(upipe_set_upump_mgr(upipe_avfsink, upump_mgr));
    if (delay) {
        assert(upipe_set_uclock(upipe_avfsink, uclock));
        assert(upipe_sink_set_delay(upipe_avfsink, delay));
    }
    assert(upipe_avfsink_set_path(upipe_avfsink, sink_file, mode));
#endif

    struct upipe_mgr *upipe_avfsrc_mgr = upipe_avfsrc_mgr_alloc();
    assert(upipe_avfsrc_mgr != NULL);
    upipe_avfsrc = upipe_alloc(upipe_avfsrc_mgr,
            uprobe_pfx_adhoc_alloc(log, UPROBE_LOG_LEVEL,
                                   "avformat source"));
    assert(upipe_avfsrc != NULL);
    assert(upipe_set_upump_mgr(upipe_avfsrc, upump_mgr));
    assert(upipe_set_uref_mgr(upipe_avfsrc, uref_mgr));
#if 0
    if (delay)
        assert(upipe_set_uclock(upipe_avfsrc, uclock));
#endif
    assert(upipe_avfsrc_set_url(upipe_avfsrc, src_url));

    ev_loop(loop, 0);

    struct uchain *uchain;
    ulist_delete_foreach(&upipe_avfsrc_outputs, uchain) {
        struct test_output *output = container_of(uchain, struct test_output,
                                                  uchain);
        ulist_delete(&upipe_avfsrc_outputs, uchain);
        upipe_release(output->upipe_avfsrc_output);
        free(output);
    }

    upipe_release(upipe_avfsrc);
    upipe_mgr_release(upipe_avfsrc_mgr); // nop

#if 0
    upipe_release(upipe_avfsink);
    upipe_mgr_release(upipe_avfsink_mgr); // nop
#endif

    upipe_av_clean();

    upump_mgr_release(upump_mgr);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    ubuf_mgr_release(ubuf_mgr);
#if 0
    uclock_release(uclock);
#endif
    uprobe_log_free(log);
    uprobe_stdio_free(uprobe_stdio);

    ev_default_destroy();
    return 0;
}
