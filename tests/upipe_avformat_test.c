/*
 * Copyright (C) 2012-2014 OpenHeadend S.A.R.L.
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
#include <upipe-av/upipe_avformat_sink.h>

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
#define UPUMP_POOL 1
#define UPUMP_BLOCKER_POOL 1
#define READ_SIZE 4096
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

static struct uprobe *logger;
static struct upipe *upipe_avfsrc;
static struct upipe *upipe_avfsink;

static void usage(const char *argv0) {
    fprintf(stdout, "Usage: %s <source file> <sink file>\n", argv0);
    exit(EXIT_FAILURE);
}

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
        case UPROBE_CLOCK_REF:
        case UPROBE_CLOCK_TS:
        case UPROBE_NEW_FLOW_DEF:
            break;
        case UPROBE_SPLIT_UPDATE: {
            struct uref *flow_def = NULL;
            while (ubase_check(upipe_split_iterate(upipe, &flow_def)) &&
                   flow_def != NULL) {
                const char *def;
                ubase_assert(uref_flow_get_def(flow_def, &def));
                if (ubase_ncmp(def, "block.")) {
                    upipe_warn_va(upipe,
                                  "flow def %s is not supported by unit test",
                                  def);
                    break;
                }

                uint64_t id;
                ubase_assert(uref_flow_get_id(flow_def, &id));

                struct upipe *upipe_avfsrc_output =
                    upipe_flow_alloc_sub(upipe_avfsrc,
                        uprobe_pfx_alloc_va(uprobe_use(logger),
                                            UPROBE_LOG_LEVEL,
                                            "src %"PRIu64, id), flow_def);
                assert(upipe_avfsrc_output != NULL);

                struct upipe *upipe_sink =
                    upipe_void_alloc_output_sub(upipe_avfsrc_output,
                        upipe_avfsink,
                        uprobe_pfx_alloc_va(uprobe_use(logger),
                                            UPROBE_LOG_LEVEL,
                                            "sink %"PRIu64, id));
                assert(upipe_sink != NULL);
                upipe_release(upipe_sink);
            }
            return UBASE_ERR_NONE;
        }
        case UPROBE_SOURCE_END:
            upipe_release(upipe);
            return UBASE_ERR_NONE;
    }
    return UBASE_ERR_NONE;
}

int main(int argc, char *argv[])
{
    const char *src_url, *sink_url;

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
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop, UPUMP_POOL,
                                                     UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);
#if 0
    struct uclock *uclock = uclock_std_alloc(0);
    assert(uclock != NULL);
#endif
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    logger = uprobe_stdio_alloc(&uprobe, stdout, UPROBE_LOG_LEVEL);
    assert(logger != NULL);
    logger = uprobe_uref_mgr_alloc(logger, uref_mgr);
    assert(logger != NULL);
    logger = uprobe_upump_mgr_alloc(logger, upump_mgr);
    assert(logger != NULL);
#if 0
    if (delay) {
        logger = uprobe_uclock_alloc(logger, uclock);
        assert(logger != NULL);
    }
#endif
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_POOL_DEPTH);
    assert(logger != NULL);

    assert(upipe_av_init(false, uprobe_use(logger)));

    struct upipe_mgr *upipe_avfsink_mgr = upipe_avfsink_mgr_alloc();
    assert(upipe_avfsink_mgr != NULL);
    upipe_avfsink = upipe_void_alloc(upipe_avfsink_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "avfsink"));
    assert(upipe_avfsink != NULL);
#if 0
    if (delay) {
        ubase_assert(upipe_attach_uclock(upipe_avfsink));
        ubase_assert(upipe_sink_set_delay(upipe_avfsink, delay));
    }
#endif
    ubase_assert(upipe_set_uri(upipe_avfsink, sink_url));

    struct upipe_mgr *upipe_avfsrc_mgr = upipe_avfsrc_mgr_alloc();
    assert(upipe_avfsrc_mgr != NULL);
    upipe_avfsrc = upipe_void_alloc(upipe_avfsrc_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "avfsrc"));
    assert(upipe_avfsrc != NULL);
#if 0
    if (delay)
        ubase_assert(upipe_attach_uclock(upipe_avfsrc));
#endif
    ubase_assert(upipe_set_uri(upipe_avfsrc, src_url));

    ev_loop(loop, 0);

    upipe_mgr_release(upipe_avfsrc_mgr); // nop

    uint64_t duration;
    ubase_assert(upipe_avfsink_get_duration(upipe_avfsink, &duration));
    printf("duration: %"PRIu64"\n", duration);

    upipe_release(upipe_avfsink);
    upipe_mgr_release(upipe_avfsink_mgr); // nop

    upipe_av_clean();

    upump_mgr_release(upump_mgr);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
#if 0
    uclock_release(uclock);
#endif
    uprobe_release(logger);
    uprobe_clean(&uprobe);

    ev_default_destroy();
    return 0;
}
