/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
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

#include <upipe/ulog.h>
#include <upipe/ulog_std.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_print.h>
#include <upipe/uclock.h>
#include <upipe/uclock_std.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe/upipe.h>
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

#define UREF_POOL_DEPTH 10
#define UBUF_POOL_DEPTH 10
#define READ_SIZE 4096
#define ULOG_LEVEL ULOG_DEBUG

static void usage(const char *argv0) {
    fprintf(stdout, "Usage: %s [-d <delay>] [-a|-o] <source file> <sink file>\n", argv0);
    fprintf(stdout, "-a : append\n");
    fprintf(stdout, "-o : overwrite\n");
    exit(EXIT_FAILURE);
}

/** definition of our struct uprobe */
static bool catch(struct uprobe *uprobe, struct upipe *upipe,
                  enum uprobe_event event, va_list args)
{
    switch (event) {
        case UPROBE_AERROR:
        case UPROBE_UPUMP_ERROR:
        case UPROBE_WRITE_END:
        case UPROBE_NEED_UREF_MGR:
        case UPROBE_NEED_UPUMP_MGR:
        case UPROBE_LINEAR_NEED_UBUF_MGR:
        case UPROBE_SOURCE_NEED_FLOW_NAME:
        default:
            assert(0);
            break;
        case UPROBE_READY:
        case UPROBE_NEW_FLOW:
        case UPROBE_READ_END:
            break;
    }
    return true;
}

int main(int argc, char *argv[])
{
    const char *src_url, *sink_url;
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
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, -1, -1);
#if 0
    struct ubuf_mgr *ubuf_mgr = ubuf_block_mgr_alloc(UBUF_POOL_DEPTH,
                                                     UBUF_POOL_DEPTH, READ_SIZE,
                                                     -1, -1, -1, 0);
#endif
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop);
#if 0
    struct uclock *uclock = uclock_std_alloc(0);
#endif
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_print = uprobe_print_alloc(&uprobe, stdout, "test");
    assert(uprobe_print != NULL);

    assert(upipe_av_init(false));

#if 0
    struct upipe_mgr *upipe_avfsink_mgr = upipe_avfsink_mgr_alloc();
    assert(upipe_avfsink_mgr != NULL);
    struct upipe *upipe_avfsink = upipe_alloc(upipe_avfsink_mgr, uprobe_print,
            ulog_std_alloc(stdout, ULOG_LEVEL, "file sink"));
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
    struct upipe *upipe_avfsrc = upipe_alloc(upipe_avfsrc_mgr, uprobe_print,
            ulog_std_alloc(stdout, ULOG_LEVEL, "avformat source"));
    assert(upipe_avfsrc != NULL);
    assert(upipe_set_upump_mgr(upipe_avfsrc, upump_mgr));
    assert(upipe_set_uref_mgr(upipe_avfsrc, uref_mgr));
#if 0
    assert(upipe_linear_set_ubuf_mgr(upipe_avfsrc, ubuf_mgr));
    assert(upipe_linear_set_output(upipe_avfsrc, upipe_fsink));
    assert(upipe_source_set_read_size(upipe_avfsrc, READ_SIZE));
    assert(upipe_source_set_flow(upipe_avfsrc, "0"));
    if (delay)
        assert(upipe_set_uclock(upipe_avfsrc, uclock));
#endif
    assert(upipe_avfsrc_set_url(upipe_avfsrc, src_url));
#if 0
    uint64_t size;
    if (upipe_avfsrc_get_size(upipe_avfsrc, &size))
        fprintf(stdout, "source file has size %"PRIu64"\n", size);
    else
        fprintf(stdout, "source path is not a regular file\n");
#endif

    ev_loop(loop, 0);

    upipe_release(upipe_avfsrc);
    upipe_mgr_release(upipe_avfsrc_mgr); // nop

#if 0
    upipe_release(upipe_avfsink);
    upipe_mgr_release(upipe_avfsink_mgr); // nop
#endif

    upipe_av_clean();

    upump_mgr_release(upump_mgr);
    assert(urefcount_single(&uref_mgr->refcount));
    uref_mgr_release(uref_mgr);
#if 0
    assert(urefcount_single(&ubuf_mgr->refcount));
    ubuf_mgr_release(ubuf_mgr);
    assert(urefcount_single(&uclock->refcount));
    uclock_release(uclock);
#endif
    uprobe_print_free(uprobe_print);

    ev_default_destroy();
    return 0;
}
