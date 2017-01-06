/*
 * Copyright (C) 2012-2016 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *          Christophe Massiot
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
 * @short unit tests for file source and sink pipes
  */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/uprobe_upump_mgr.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_dump.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe/upipe.h>
#include <upipe-modules/upipe_file_sink.h>
#include <upipe-modules/upipe_multicat_sink.h>
#include <upipe-modules/upipe_multicat_source.h>
#include <upipe-modules/upipe_genaux.h>

#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/param.h>
#include <fcntl.h>
#include <signal.h>
#include <assert.h>

#include <ev.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH  0
#define UBUF_POOL_DEPTH  0
#define UPUMP_POOL 0
#define UPUMP_BLOCKER_POOL 0
#define READ_SIZE  4096
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG
#define UREF_PER_SLICE 10
#define SLICES_NUM 10

static struct uref_mgr *uref_mgr;
static struct ubuf_mgr *ubuf_mgr;
static struct upipe *multicat_sink;
static struct upump *idler;
static uint64_t rotate = 0;
static uint64_t rotate_offset = 0;
static uint64_t gen_systime = 0;

static void sig_handler(int sig)
{
//    fprintf("Exit on signal %d\n", sig);
    exit(1);
}

static void usage(const char *argv0) {
    fprintf(stdout, "Usage: %s [-r <rotate> [-O <rotate offset>]] <dest dir> <suffix>\n", argv0);
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
        case UPROBE_SOURCE_END:
        case UPROBE_NEW_FLOW_DEF:
        case UPROBE_ERROR:
            break;
    }
    return UBASE_ERR_NONE;
}

/** packet generator */
static void genpacket_idler(struct upump *upump)
{
    if (gen_systime >= SLICES_NUM * rotate + rotate_offset) {
        upump_stop(idler);
        return;
    }
    int size = -1;
    uint8_t *buf;

    struct uref *uref = uref_block_alloc(uref_mgr, ubuf_mgr, sizeof(uint64_t));
    ubase_assert(uref_block_write(uref, 0, &size, &buf));
    assert(size == sizeof(uint64_t));

    upipe_genaux_hton64(buf, gen_systime);
    uref_clock_set_cr_sys(uref, gen_systime);

    uref_block_unmap(uref, 0);
    upipe_input(multicat_sink, uref, NULL);
    gen_systime += rotate/UREF_PER_SLICE;
}

/** helper phony pipe */
static struct upipe *test_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe,
                                uint32_t signature, va_list args)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_init(upipe, mgr, uprobe);
    return upipe;
}

/** helper phony pipe */
static void test_input(struct upipe *upipe, struct uref *uref,
                       struct upump **upump_p)
{
    assert(uref != NULL);
    upipe_dbg(upipe, "===> received input uref");
    uref_dump(uref, upipe->uprobe);

    static uint64_t systime = 0;
    uint64_t cr_sys;
    uref_clock_get_cr_sys(uref, &cr_sys);
    assert(cr_sys == systime);

    int size = -1;
    const uint8_t *buf;
    ubase_assert(uref_block_read(uref, 0, &size, &buf));
    assert(size == sizeof(uint64_t));
    cr_sys = upipe_genaux_ntoh64(buf);
    assert(cr_sys == systime);
    ubase_assert(uref_block_unmap(uref, 0));
    uref_free(uref);
    systime += rotate/UREF_PER_SLICE;
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
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe */
static struct upipe_mgr test_mgr = {
    .refcount = NULL,
    .signature = 0,

    .upipe_alloc = test_alloc,
    .upipe_input = test_input,
    .upipe_control = test_control
};

int main(int argc, char *argv[])
{
    const char *dirpath, *suffix;
    struct uref *flow;
    char filepath[MAXPATHLEN];
    int i, j, fd, ret, opt;

    signal (SIGINT, sig_handler);

    while ((opt = getopt(argc, argv, "r:O:")) != -1) {
        switch (opt) {
            case 'r':
                rotate = strtoull(optarg, NULL, 0);
                break;
            case 'O':
                gen_systime = rotate_offset = strtoull(optarg, NULL, 0);
                break;
            default:
                usage(argv[0]);
        }
    }
    if (optind >= argc -1)
        usage(argv[0]);
    dirpath = argv[optind++];
    suffix = argv[optind++];

    // setup env
    struct ev_loop *loop = ev_default_loop(0);
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    assert(uref_mgr != NULL);
    ubuf_mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH,
                                                         UBUF_POOL_DEPTH,
                                                         umem_mgr, 0, 0, -1, 0);
    assert(ubuf_mgr != NULL);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop, UPUMP_POOL,
                                                     UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_LEVEL);
    assert(logger != NULL);
    logger = uprobe_uref_mgr_alloc(logger, uref_mgr);
    assert(logger != NULL);
    logger = uprobe_upump_mgr_alloc(logger, upump_mgr);
    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_POOL_DEPTH);

    // write junk to the first file to test set_mode/OVERWRITE
    snprintf(filepath, MAXPATHLEN, "%s%u%s", dirpath, 0, suffix);
    fd = open(filepath, O_TRUNC|O_CREAT|O_WRONLY, 0644);
    memset(filepath, 42, MAXPATHLEN);
    assert(write(fd, filepath, MAXPATHLEN) == MAXPATHLEN);
    close(fd);

    // send flow definition
    flow = uref_block_flow_alloc_def(uref_mgr, "");
    assert(flow);

    // multicat_sink
    struct upipe_mgr *upipe_multicat_sink_mgr = upipe_multicat_sink_mgr_alloc();
    struct upipe_mgr *upipe_fsink_mgr = upipe_fsink_mgr_alloc();
    assert(upipe_fsink_mgr != NULL);
    multicat_sink = upipe_void_alloc(upipe_multicat_sink_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "multicat sink"));
    assert(multicat_sink != NULL);
    ubase_assert(upipe_set_flow_def(multicat_sink, flow));
    uref_free(flow);
    ubase_assert(upipe_multicat_sink_set_fsink_mgr(multicat_sink, upipe_fsink_mgr));
    if (rotate) {
        ubase_assert(upipe_multicat_sink_set_rotate(multicat_sink, rotate, rotate_offset));
    } else {
        upipe_multicat_sink_get_rotate(multicat_sink, &rotate, &rotate_offset);
    }
    ubase_assert(upipe_multicat_sink_set_mode(multicat_sink, UPIPE_FSINK_OVERWRITE));
    ubase_assert(upipe_multicat_sink_set_path(multicat_sink, dirpath, suffix));

    // idler - packet generator
    idler = upump_alloc_idler(upump_mgr, genpacket_idler, NULL, NULL);
    assert(idler);

    // fire !
    upump_start(idler);
    ev_loop(loop, 0);
    upump_free(idler);
    upipe_release(multicat_sink);
    upipe_mgr_release(upipe_fsink_mgr); // nop
    upipe_mgr_release(upipe_multicat_sink_mgr); // nop

    // check resulting files
    uint64_t systime = rotate_offset, val;
    for (i=0; i < SLICES_NUM; i++){
        snprintf(filepath, MAXPATHLEN, "%s%"PRId64"%s", dirpath, (systime/rotate), suffix);
        printf("Opening %s ... ", filepath);
        assert(fd = open(filepath, O_RDONLY));
        for (j = 0; j < UREF_PER_SLICE; j++) {
            uint8_t buf[8];
            ret = read(fd, buf, sizeof(uint64_t));
            assert(ret == sizeof(uint64_t));
            val = upipe_genaux_ntoh64(buf);
            if (val != systime) {
                printf("%d %d - %"PRIu64" != %"PRIu64"\n", i, j, val, systime);
            }
            assert(val == systime);
            systime += rotate/UREF_PER_SLICE;
        }
        printf("Ok.\n");
        close(fd);
    }

    // check resulting files with msrc
    struct upipe_mgr *upipe_msrc_mgr = upipe_msrc_mgr_alloc();
    struct upipe *msrc = upipe_void_alloc(upipe_msrc_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "multicat source"));
    assert(msrc != NULL);
    upipe_mgr_release(upipe_msrc_mgr);
    flow = uref_alloc_control(uref_mgr);
    assert(flow != NULL);
    ubase_assert(uref_msrc_flow_set_path(flow, dirpath));
    ubase_assert(uref_msrc_flow_set_data(flow, suffix));
    ubase_assert(uref_msrc_flow_set_aux(flow, suffix));
    ubase_assert(uref_msrc_flow_set_rotate(flow, rotate));
    ubase_assert(uref_msrc_flow_set_offset(flow, rotate_offset));
    ubase_assert(upipe_set_flow_def(msrc, flow));
    uref_free(flow);
    ubase_assert(upipe_set_output_size(msrc, sizeof(uint64_t)));

    struct upipe *test = upipe_void_alloc(&test_mgr, uprobe_use(logger));
    assert(test != NULL);
    ubase_assert(upipe_set_output(msrc, test));

    // fire !
    ubase_assert(upipe_src_set_position(msrc, 0));
    ev_loop(loop, 0);

    // release everything
    upipe_release(msrc);
    test_free(test);
    upump_mgr_release(upump_mgr);
    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe);

    ev_default_destroy();

    return 0;
}
