/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_log.h>
#include <upipe/uprobe_select_flows.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_upump_mgr.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/ubuf_pic.h>
#include <upipe/ubuf_pic_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_block_flow.h>
#include <upipe/upipe.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe-modules/upipe_file_sink.h>
#include <upipe-modules/upipe_file_source.h>
#include <upipe-modules/upipe_probe_uref.h>
#include <upipe-av/upipe_av.h>
#include <upipe-av/upipe_avcodec_decode.h>
#include <upipe-av/upipe_avcodec_encode.h>
#include <upipe-swscale/upipe_sws.h>
#include <upipe-filters/upipe_filter_blend.h>
#include <upipe-ts/upipe_ts_demux.h>
#include <upipe-framers/upipe_mpgv_framer.h>
#include <upipe-framers/upipe_h264_framer.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdint.h>
#include <sys/param.h>
#include <signal.h>
#include <pthread.h>

#include <ev.h>
#include <pthread.h>

#define UDICT_POOL_DEPTH    50
#define UREF_POOL_DEPTH     50
#define UBUF_POOL_DEPTH     50
#define UBUF_PREPEND        0
#define UBUF_APPEND         0
#define UBUF_ALIGN          32
#define UBUF_ALIGN_OFFSET   0
#define UPUMP_POOL          10
#define UPUMP_BLOCKER_POOL  10
#define INPUT_QUEUE_LENGTH  255
#define INPUT_BUFFERING     20480

#define UPROBE_LOG_LEVEL UPROBE_LOG_NOTICE

enum uprobe_log_level loglevel = UPROBE_LOG_LEVEL;

FILE *logstream;
struct uprobe *logger;
struct uprobe uprobe_uref;
struct uprobe uprobe_avcdec;
struct upipe_mgr *upipe_avcdec_mgr;
struct upipe_mgr *upipe_avcenc_mgr;
struct upipe_mgr *upipe_filter_blend_mgr;
struct upipe_mgr *upipe_sws_mgr;
struct upipe_mgr *upipe_fsink_mgr;
struct upipe_mgr *upipe_probe_uref_mgr;
struct ubuf_mgr *yuv_mgr;
struct ubuf_mgr *block_mgr;
struct uref_mgr *uref_mgr;

const char *srcpath, *dstpath;

struct upipe *upipe_source;

static void usage(const char *argv0) {
    fprintf(stderr, "Usage: %s [-d] [-q] <source>\n", argv0);
    fprintf(stderr, "   -d: force debug log level\n");
    fprintf(stderr, "   -q: quieter log\n");
    exit(EXIT_FAILURE);
}

/** catch probes from probe_uref */
static bool uref_catch(struct uprobe *uprobe, struct upipe *upipe,
                       enum uprobe_event event, va_list args)
{
    if (event <= UPROBE_LOCAL)
        return false;

    unsigned int signature = va_arg(args, unsigned int);
    if (signature != UPIPE_PROBE_UREF_SIGNATURE || event != UPROBE_PROBE_UREF)
        return false;

    if (upipe_source != NULL) {
        /* release the source to exit */
        upipe_release(upipe_source);
        upipe_source = NULL;
    } else {
        /* second (or after) frame, do not output them */
        upipe_set_output(upipe, NULL);
    }
    return true;
}

/** split callback */
static bool avcdec_catch(struct uprobe *uprobe, struct upipe *upipe,
                         enum uprobe_event event, va_list args)
{
    if (event != UPROBE_NEW_FLOW_DEF)
        return false;

    struct uref *flow_def = va_arg(args, struct uref *);

    uint64_t hsize, vsize, wanted_hsize;
    struct urational sar;
    bool progressive;
    if (unlikely(!uref_pic_flow_get_hsize(flow_def, &hsize) ||
                 !uref_pic_flow_get_vsize(flow_def, &vsize) ||
                 !uref_pic_flow_get_sar(flow_def, &sar))) {
        upipe_err_va(upipe, "incompatible flow def");
        upipe_release(upipe_source);
        return true;
    }
    wanted_hsize = hsize * sar.num / sar.den;
    progressive = uref_pic_get_progressive(flow_def);

    struct uref *flow_def2 = uref_dup(flow_def);
    upipe_use(upipe);

    if (!progressive) {
        uref_pic_set_progressive(flow_def2);
        struct upipe *deint = upipe_void_alloc_output(upipe,
                upipe_filter_blend_mgr,
                uprobe_pfx_adhoc_alloc(logger, loglevel, "deint"));
        upipe_release(upipe);
        upipe_set_ubuf_mgr(deint, yuv_mgr);
        upipe = deint;
    }

    if (wanted_hsize != hsize) {
        uref_pic_flow_set_hsize(flow_def2, wanted_hsize);
        struct upipe *sws = upipe_flow_alloc_output(upipe, upipe_sws_mgr,
                uprobe_pfx_adhoc_alloc_va(logger, loglevel, "sws"), flow_def2);
        upipe_release(upipe);
        if (sws == NULL) {
            upipe_err_va(upipe, "incompatible flow def");
            uref_free(flow_def2);
            upipe_release(upipe_source);
            return true;
        }
        upipe_set_ubuf_mgr(sws, yuv_mgr);
        upipe = sws;
    }

    uref_pic_flow_clear_format(flow_def2);
    uref_flow_set_def(flow_def2, "block.mjpeg.pic.");
    struct upipe *jpegenc = upipe_flow_alloc_output(upipe, upipe_avcenc_mgr,
            uprobe_pfx_adhoc_alloc_va(logger, loglevel, "jpeg"), flow_def2);
    upipe_release(upipe);
    upipe_avcenc_set_option(jpegenc, "qmax", "2");
    upipe_set_ubuf_mgr(jpegenc, block_mgr);
    upipe = jpegenc;

    struct upipe *urefprobe = upipe_void_alloc_output(upipe,
            upipe_probe_uref_mgr,
            uprobe_pfx_adhoc_alloc_va(&uprobe_uref, loglevel, "urefprobe"));
    upipe_release(upipe);
    upipe = urefprobe;

    struct upipe *fsink = upipe_void_alloc_output(upipe, upipe_fsink_mgr,
            uprobe_pfx_adhoc_alloc_va(logger,
            ((loglevel > UPROBE_LOG_DEBUG) ? UPROBE_LOG_WARNING : loglevel),
            "jpegsink"));
    upipe_release(upipe);
    upipe_fsink_set_path(fsink, dstpath, UPIPE_FSINK_OVERWRITE);
    upipe = fsink;

    uref_free(flow_def2);
    upipe_release(upipe);
}

/** split callback */
static bool split_catch(struct uprobe *uprobe, struct upipe *upipe,
                        enum uprobe_event event, va_list args)
{
    if (event != UPROBE_NEW_FLOW_DEF)
        return false;

    struct upipe *avcdec = upipe_void_alloc_output(upipe, upipe_avcdec_mgr,
            uprobe_pfx_adhoc_alloc_va(&uprobe_avcdec, loglevel, "avcdec"));
    if (avcdec == NULL) {
        upipe_err_va(upipe, "incompatible flow def");
        upipe_release(upipe_source);
        return true;
    }
    upipe_set_ubuf_mgr(avcdec, yuv_mgr);
    upipe_release(avcdec);
    return true;
}

int main(int argc, char **argv)
{
    int opt;

    /* parse options */
    while ((opt = getopt(argc, argv, "dq")) != -1) {
        switch (opt) {
            case 'd':
                if (loglevel > 0) loglevel--;
                break;
            case 'q':
                if (loglevel < UPROBE_LOG_ERROR) loglevel++;
                break;
            default:
                usage(argv[0]);
        }
    }
    if (optind >= argc - 1) {
        usage(argv[0]);
    }
    srcpath = argv[optind++];
    dstpath = argv[optind++];

    /* choose log fd */
    logstream = stderr;

    /* setup environnement */
    struct ev_loop *loop = ev_default_loop(0);
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    block_mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH,
                                         umem_mgr, -1, -1, -1, 0);
    yuv_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH,
                                     umem_mgr, 1,
                                     UBUF_PREPEND, UBUF_APPEND,
                                     UBUF_PREPEND, UBUF_APPEND,
                                     UBUF_ALIGN, UBUF_ALIGN_OFFSET);
    /* planar YUV (I420) */
    ubuf_pic_mem_mgr_add_plane(yuv_mgr, "y8", 1, 1, 1);
    ubuf_pic_mem_mgr_add_plane(yuv_mgr, "u8", 2, 2, 1);
    ubuf_pic_mem_mgr_add_plane(yuv_mgr, "v8", 2, 2, 1);

    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop,
                            UPUMP_POOL, UPUMP_BLOCKER_POOL);

    /* default probe */
    logger = uprobe_stdio_alloc(NULL, logstream, loglevel);
    logger = uprobe_log_alloc(logger, UPROBE_LOG_DEBUG);
    logger = uprobe_uref_mgr_alloc(logger, uref_mgr);
    logger = uprobe_upump_mgr_alloc(logger, upump_mgr);
    uref_mgr_release(uref_mgr);
    upump_mgr_release(upump_mgr);

    /* split probe */
    struct uprobe uprobe_catch;
    uprobe_init(&uprobe_catch, split_catch, logger);
    struct uprobe *uprobe_selflow = uprobe_selflow_alloc(logger,
                                                         &uprobe_catch,
                                                         UPROBE_SELFLOW_PIC,
                                                         "auto");
    struct uprobe *uprobe_selprog = uprobe_selflow_alloc(logger,
                                                         uprobe_selflow,
                                                         UPROBE_SELFLOW_VOID,
                                                         "auto");

    /* other probes */
    uprobe_init(&uprobe_avcdec, avcdec_catch, logger);
    uprobe_init(&uprobe_uref, uref_catch, logger);

    /* upipe-av */
    upipe_av_init(true, logger);

    /* global pipe managers */
    upipe_avcdec_mgr = upipe_avcdec_mgr_alloc();
    upipe_avcenc_mgr = upipe_avcenc_mgr_alloc();
    upipe_sws_mgr = upipe_sws_mgr_alloc();
    upipe_filter_blend_mgr = upipe_filter_blend_mgr_alloc();
    upipe_fsink_mgr = upipe_fsink_mgr_alloc();
    upipe_probe_uref_mgr = upipe_probe_uref_mgr_alloc();

    /* file source */
    struct upipe_mgr *upipe_fsrc_mgr = upipe_fsrc_mgr_alloc();
    upipe_source = upipe_void_alloc(upipe_fsrc_mgr,
            uprobe_pfx_adhoc_alloc(logger, loglevel, "fsrc"));
    upipe_mgr_release(upipe_fsrc_mgr);
    upipe_set_upump_mgr(upipe_source, upump_mgr);
    upipe_set_ubuf_mgr(upipe_source, block_mgr);
    if (!upipe_set_uri(upipe_source, srcpath))
        exit(EXIT_FAILURE);

    /* upipe-ts */
    struct upipe_mgr *upipe_ts_demux_mgr = upipe_ts_demux_mgr_alloc();
    struct upipe_mgr *upipe_mpgvf_mgr = upipe_mpgvf_mgr_alloc();
    upipe_ts_demux_mgr_set_mpgvf_mgr(upipe_ts_demux_mgr, upipe_mpgvf_mgr);
    upipe_mgr_release(upipe_mpgvf_mgr);
    struct upipe_mgr *upipe_h264f_mgr = upipe_h264f_mgr_alloc();
    upipe_ts_demux_mgr_set_h264f_mgr(upipe_ts_demux_mgr, upipe_h264f_mgr);
    upipe_mgr_release(upipe_h264f_mgr);
    struct upipe *ts_demux = upipe_void_alloc_output(upipe_source,
            upipe_ts_demux_mgr,
            uprobe_pfx_adhoc_alloc(uprobe_selprog, loglevel, "tsdemux"));
    upipe_mgr_release(upipe_ts_demux_mgr);
    upipe_release(ts_demux);

    /* fire loop ! */
    ev_loop(loop, 0);

    /* release everyhing */
    if (upipe_source != NULL)
        upipe_release(upipe_source);
    uprobe_selflow_free(uprobe_selprog);
    uprobe_selflow_free(uprobe_selflow);
    logger = uprobe_upump_mgr_free(logger);
    logger = uprobe_uref_mgr_free(logger);
    logger = uprobe_log_free(logger);
    uprobe_stdio_free(logger);

    upipe_mgr_release(upipe_avcdec_mgr);
    upipe_mgr_release(upipe_avcenc_mgr);
    upipe_mgr_release(upipe_sws_mgr);
    upipe_mgr_release(upipe_filter_blend_mgr);
    upipe_mgr_release(upipe_fsink_mgr);
    upipe_mgr_release(upipe_probe_uref_mgr);

    ubuf_mgr_release(block_mgr);
    ubuf_mgr_release(yuv_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);

    ev_default_destroy();
    upipe_av_clean();

    return 0;
}
