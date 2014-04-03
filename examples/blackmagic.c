/*
 * Copyright (C) 2012-2014 OpenHeadend S.A.R.L.
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
#include <upipe/uprobe_output.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/uprobe_upump_mgr.h>
#include <upipe/uprobe_uclock.h>
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
#include <upipe-modules/upipe_null.h>
#include <upipe-av/upipe_av.h>
#include <upipe-av/upipe_avcodec_encode.h>
#include <upipe-av/upipe_avformat_sink.h>
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
struct upump_mgr *upump_mgr;

const char *video_codec = "mpeg2video";
const char *audio_codec = NULL;
const char *sink_uri = NULL;

/* catch uprobes */
static int catch(struct uprobe *uprobe, struct upipe *upipe,
                 int event, va_list args)
{
    switch(event) {
        default:
            return false;
    }
}

void usage(const char *argv0)
{
    printf("Usage: %s [-d] [-f format] [-m mime] [-c video_codec] [-a audio_codec] sink_uri\n", argv0);
    exit(-1);
}

int main(int argc, char **argv)
{
    int opt;
    struct uref *flow;
    const char *mime = NULL, *format = NULL;

    /* parse options */
    while ((opt = getopt(argc, argv, "dm:f:c:v:a:")) != -1) {
        switch (opt) {
            case 'd':
                if (loglevel > 0) loglevel--;
                break;
            case 'm':
                mime = optarg;
                break;
            case 'f':
                format = optarg;
                break;

            case 'c':
            case 'v':
                video_codec = optarg;
                break;
            case 'a':
                audio_codec = optarg;
                break;
                
            default:
                usage(argv[0]);
        }
    }
    if (argc - optind < 1) {
        usage(argv[0]);
    }

    sink_uri = argv[optind++];

    /* upipe env */
    struct ev_loop *loop = ev_default_loop(0);
    upump_mgr = upump_ev_mgr_alloc(loop, UPUMP_POOL, UPUMP_BLOCKER_POOL);
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);

    /* uclock */
    struct uclock *uclock = uclock_std_alloc(UCLOCK_FLAG_REALTIME);

    /* main probe */
    logger = uprobe_stdio_alloc(NULL, stdout, loglevel);
    logger = uprobe_uref_mgr_alloc(logger, uref_mgr);
    logger = uprobe_upump_mgr_alloc(logger, upump_mgr);
    logger = uprobe_uclock_alloc(logger, uclock);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_POOL_DEPTH);

    /* generic probe */
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, logger);

    /* upipe-av */
    upipe_av_init(false, logger);

    /* managers */
    struct upipe_mgr *upipe_avcenc_mgr = upipe_avcenc_mgr_alloc();
    struct upipe_mgr *upipe_bmd_src_mgr = upipe_bmd_src_mgr_alloc();
    struct upipe_mgr *upipe_fsink_mgr = upipe_fsink_mgr_alloc();
    struct upipe_mgr *upipe_sws_mgr = upipe_sws_mgr_alloc();
    struct upipe_mgr *upipe_null_mgr = upipe_null_mgr_alloc();
    struct upipe_mgr *upipe_avfsink_mgr = upipe_avfsink_mgr_alloc();

    /* /dev/null */
    struct upipe *devnull = upipe_void_alloc(upipe_null_mgr,
        uprobe_pfx_alloc(logger, loglevel, "devnull"));

    /* avformat sink */
    struct upipe *avfsink = upipe_void_alloc(upipe_avfsink_mgr,
        uprobe_pfx_alloc(uprobe_use(logger), loglevel, "avfsink"));
    upipe_attach_uclock(avfsink);

    upipe_avfsink_set_mime(avfsink, mime);
    upipe_avfsink_set_format(avfsink, format);
    if (unlikely(!ubase_check(upipe_set_uri(avfsink, sink_uri)))) {
        fprintf(stderr, "error: could not open dest uri\n");
        exit(EXIT_FAILURE);
    }

    /* blackmagic source */
    struct upipe *bmdsrc = upipe_bmd_src_alloc(upipe_bmd_src_mgr,
        uprobe_pfx_alloc(uprobe_output_alloc(uprobe_use(logger)),
                         loglevel, "bmdsrc"),
        uprobe_pfx_alloc(uprobe_output_alloc(uprobe_use(logger)),
                         loglevel, "bmdvideo"),
        uprobe_pfx_alloc(uprobe_output_alloc(uprobe_use(logger)),
                         loglevel, "bmdaudio"),
        uprobe_pfx_alloc(uprobe_output_alloc(uprobe_use(logger)),
                         loglevel, "bmdsubpic"));
    assert(bmdsrc);
    upipe_attach_uclock(bmdsrc);
    upipe_set_uri(bmdsrc, "0");
   
    /* video source subpipe */
    struct upipe *bmdvideo = NULL;
    upipe_bmd_src_get_pic_sub(bmdsrc, &bmdvideo);
    assert(bmdvideo);
    upipe_use(bmdvideo);

    /* convert picture */
    flow = uref_pic_flow_alloc_def(uref_mgr, 1);
    uref_pic_flow_add_plane(flow, 1, 1, 1, "y8");
    uref_pic_flow_add_plane(flow, 2, 2, 1, "u8");
    uref_pic_flow_add_plane(flow, 2, 2, 1, "v8");
    struct upipe *sws = upipe_flow_alloc_output(bmdvideo, upipe_sws_mgr,
        uprobe_pfx_alloc(uprobe_output_alloc(uprobe_use(logger)),
            loglevel, "sws"), flow);
    assert(sws);
    upipe_release(sws);
    uref_free(flow);

    /* encode */
    flow = uref_block_flow_alloc_def(uref_mgr, "");
    uref_avcenc_set_codec_name(flow, video_codec);
    struct upipe *avcenc = upipe_flow_alloc_output(sws, upipe_avcenc_mgr,
        uprobe_pfx_alloc(uprobe_output_alloc(uprobe_use(logger)),
            loglevel, "avcenc"), flow);
    assert(avcenc);
    upipe_release(avcenc);
    uref_free(flow);
    
    /* mux input */
    struct upipe *sink = upipe_void_alloc_output_sub(avcenc, avfsink,
        uprobe_pfx_alloc(uprobe_use(logger), loglevel, "videosink"));
    assert(sink);
    upipe_release(sink);
 
    /* audio source subpipe */
    struct upipe *bmdaudio = NULL;
    upipe_bmd_src_get_sound_sub(bmdsrc, &bmdaudio);
    assert(bmdaudio);
    upipe_use(bmdaudio);

    if (audio_codec) {
        /* encode */
        flow = uref_block_flow_alloc_def(uref_mgr, "");
        uref_avcenc_set_codec_name(flow, audio_codec);
        struct upipe *audioenc = upipe_flow_alloc_output(bmdaudio,
            upipe_avcenc_mgr,
            uprobe_pfx_alloc(uprobe_output_alloc(uprobe_use(logger)),
                loglevel, "audioenc"), flow);
        assert(audioenc);
        upipe_release(audioenc);
        uref_free(flow);
        
        /* mux input */
        struct upipe *sink = upipe_void_alloc_output_sub(audioenc, avfsink,
            uprobe_pfx_alloc(uprobe_use(logger), loglevel, "audiosink"));
        assert(sink);
        upipe_release(sink);
    }
    else {
        upipe_set_output(bmdaudio, devnull);
        upipe_release(devnull);
    }

    ev_loop(loop, 0);
}
