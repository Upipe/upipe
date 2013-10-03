/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
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
 *
 */

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_log.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uclock.h>
#include <upipe/uclock_std.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_block_flow.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/ubuf_pic_mem.h>
#include <upipe/upipe.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe-modules/upipe_file_sink.h>
#include <upipe-av/upipe_av.h>
#include <upipe-av/upipe_avcodec_encode.h>
#include <upipe-blackmagic/upipe_blackmagic_source.h>
#include <upipe-swscale/upipe_sws.h>

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <ev.h>

#define UPROBE_LOG_LEVEL UPROBE_LOG_NOTICE
#define QUEUE_LENGTH 50
#define UDICT_POOL_DEPTH 10
#define UREF_POOL_DEPTH 10
#define UBUF_POOL_DEPTH 10
#define UPUMP_POOL 10
#define UPUMP_BLOCKER_POOL 10
#define UBUF_PREPEND        0
#define UBUF_APPEND         0
#define UBUF_ALIGN          32
#define UBUF_ALIGN_OFFSET   0

enum upipe_fsink_mode mode = UPIPE_FSINK_OVERWRITE;
enum uprobe_log_level loglevel = UPROBE_LOG_LEVEL;
struct uprobe *logger;

struct uref_mgr *uref_mgr;
struct ubuf_mgr *yuv_mgr;
struct ubuf_mgr *uyvy_mgr;
struct ubuf_mgr *block_mgr;
struct upump_mgr *upump_mgr;

const char *codec = "mpeg2video";
const char *sink_path = NULL;

/* catch uprobes */
static bool catch(struct uprobe *uprobe, struct upipe *upipe,
                         enum uprobe_event event, va_list args)
{
    switch(event) {
        default:
            return false;
    }
}

void usage(const char *argv0)
{
    printf("Usage: %s [-c codec] file.video\n", argv0);
    exit(-1);
}

int main(int argc, char **argv)
{
    int opt;
    struct uref *flow;

    /* parse options */
    while ((opt = getopt(argc, argv, "dc:")) != -1) {
        switch (opt) {
            case 'd':
                if (loglevel > 0) loglevel--;
                break;
            case 'c':
                codec = optarg;
                break;
            default:
                usage(argv[0]);
        }
    }
    if (argc - optind < 1) {
        usage(argv[0]);
    }

    sink_path = argv[optind++];

    /* upipe env */
    struct ev_loop *loop = ev_default_loop(0);
    upump_mgr = upump_ev_mgr_alloc(loop, UPUMP_POOL, UPUMP_BLOCKER_POOL);
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    block_mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH,
                                        UBUF_POOL_DEPTH, umem_mgr,
                                        -1, -1, -1, 0);
    yuv_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                      UBUF_PREPEND, UBUF_APPEND,
                                      UBUF_PREPEND, UBUF_APPEND,
                                      UBUF_ALIGN, UBUF_ALIGN_OFFSET);
    /* planar YUV (I420) */
    ubuf_pic_mem_mgr_add_plane(yuv_mgr, "y8", 1, 1, 1);
    ubuf_pic_mem_mgr_add_plane(yuv_mgr, "u8", 2, 2, 1);
    ubuf_pic_mem_mgr_add_plane(yuv_mgr, "v8", 2, 2, 1);

    uyvy_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 2,
                                      UBUF_PREPEND, UBUF_APPEND,
                                      UBUF_PREPEND, UBUF_APPEND,
                                      UBUF_ALIGN, UBUF_ALIGN_OFFSET);
    /* uyvy (packed 422) */
    ubuf_pic_mem_mgr_add_plane(uyvy_mgr, "uyvy422", 1, 1, 4);

    /* log probes */
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(NULL, stdout, loglevel);
    logger = uprobe_log_alloc(uprobe_stdio, loglevel);
    logger = uprobe_uref_mgr_alloc(logger, uref_mgr);

    /* generic probe */
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, logger);

    /* uclock */
    struct uclock *uclock = uclock_std_alloc(UCLOCK_FLAG_REALTIME);

    /* upipe-av */
    upipe_av_init(false, logger);

    /* managers */
    struct upipe_mgr *upipe_avcenc_mgr = upipe_avcenc_mgr_alloc();
    struct upipe_mgr *upipe_bmd_src_mgr = upipe_bmd_src_mgr_alloc();
    struct upipe_mgr *upipe_fsink_mgr = upipe_fsink_mgr_alloc();
    struct upipe_mgr *upipe_sws_mgr = upipe_sws_mgr_alloc();

    /* source */
    struct upipe *bmdsrc = upipe_void_alloc(upipe_bmd_src_mgr,
        uprobe_pfx_adhoc_alloc(logger, loglevel, "bmdsrc"));
    upipe_set_ubuf_mgr(bmdsrc, uyvy_mgr);
    upipe_set_uclock(bmdsrc, uclock);
    upipe_set_upump_mgr(bmdsrc, upump_mgr);

    /* convert picture */
    flow = uref_pic_flow_alloc_def(uref_mgr, 2);
    struct upipe *sws = upipe_flow_alloc(upipe_sws_mgr,
        uprobe_pfx_adhoc_alloc(logger, loglevel, "sws"), flow);
    uref_free(flow);
    upipe_set_ubuf_mgr(sws, yuv_mgr);
    upipe_get_flow_def(sws, &flow);
    upipe_set_output(bmdsrc, sws);
    upipe_release(sws);

    /* encode */
    struct upipe *avcenc = upipe_flow_alloc(upipe_avcenc_mgr,
        uprobe_pfx_adhoc_alloc(logger, loglevel, "avcenc"), flow);
    upipe_set_ubuf_mgr(avcenc, block_mgr);
    upipe_avcenc_set_codec_by_name(avcenc, codec);
    upipe_set_output(sws, avcenc);
    upipe_release(avcenc);
    
    /* store */
    flow = uref_block_flow_alloc_def(uref_mgr, "foo");
    struct upipe *fsink = upipe_flow_alloc(upipe_fsink_mgr,
            uprobe_pfx_adhoc_alloc(logger, loglevel, "fsink"), flow);
    uref_free(flow);
    upipe_set_upump_mgr(fsink, upump_mgr);
    upipe_fsink_set_path(fsink, sink_path, mode);
    upipe_set_output(avcenc, fsink);
    upipe_release(fsink);

    ev_loop(loop, 0);
}
