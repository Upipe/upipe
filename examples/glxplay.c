/*
 * Copyright (C) 2012-2014 OpenHeadend S.A.R.L.
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
 *
 */

/*
graph {flow: east}
( [demux] [dec_qsink] ) {border-style:dashed;}
( [dec_qsrc] [avcdec] [deint] [yuvrgb] [glx_qsink] ) {border-style:dashed;}
[] -- stream --> [demux]{rank: 0} -- encoded --> [dec_qsink] --> {flow: south; end:east} [dec_qsrc] --> {flow: west} [avcdec] -- yuv --> [deint] -- progressive --> [yuvrgb] -- rgb --> [glx_qsink] --> {flow: south; end: east} [glx_qsrc] --> {flow: west} [trickp] --> {flow: west} [glx]


    |
    | stream
    v
+ - - - - - - - - - - - - - - - - - - - - -+
'                                          '
' +---------+  encoded       +-----------+ '
' |  demux  | -------------> | dec_qsink | ' -+
' +---------+                +-----------+ '  |
'                                          '  |
+ - - - - - - - - - - - - - - - - - - - - -+  |
+ - - - - - - - - - - - - - - - - - - - - -+  |
'                                          '  |
' +---------+                +-----------+ '  |
' | avcdec  | <------------- | dec_qsrc  | ' <+
' +---------+                +-----------+ '
'   |                                      '
'   | yuv                                  '
'   |                                      '
'   |                                       - - - - - - - - - - - +
'   v                                                             '
' +---------+  progressive   +-----------+   rgb    +-----------+ '
' |  deint  | -------------> |  yuvrgb   | -------> | glx_qsink | ' -+
' +---------+                +-----------+          +-----------+ '  |
'                                                                 '  |
'                                                                 '  |
+ - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +  |
  +---------+                +-----------+          +-----------+    |
  |   glx   | <------------- |  trickp   |   <----- | glx_qsrc  |   <+
  +---------+                +-----------+          +-----------+
 */

#include <stdlib.h>
#include <strings.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>
#include <stdarg.h>

#undef NDEBUG

#include <libswscale/swscale.h>

#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_select_flows.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_uclock.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/uprobe_dejitter.h>
#include <upipe/uclock.h>
#include <upipe/uclock_std.h>
#include <upipe/umem.h>
#include <upipe/umem_pool.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/udict_dump.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_block.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/uref_pic.h>
#include <upipe/ubuf_pic_mem.h>
#include <upipe/upipe.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe-modules/upipe_transfer.h>
#include <upipe-modules/upipe_queue_source.h>
#include <upipe-modules/upipe_queue_sink.h>
#include <upipe-modules/upipe_file_source.h>
#include <upipe-modules/upipe_udp_source.h>
#include <upipe-modules/upipe_http_source.h>
#include <upipe-modules/upipe_null.h>
#include <upipe-modules/upipe_trickplay.h>
#include <upipe-pthread/uprobe_pthread_upump_mgr.h>
#include <upipe-ts/upipe_ts_demux.h>
#include <upipe-framers/upipe_mpgv_framer.h>
#include <upipe-framers/upipe_h264_framer.h>
#include <upipe-av/upipe_av.h>
#include <upipe-av/uref_av_flow.h>
#include <upipe-av/upipe_avformat_source.h>
#include <upipe-av/upipe_avcodec_decode.h>
#include <upipe-swscale/upipe_sws.h>
#include <upipe-gl/upipe_glx_sink.h>
#include <upipe-gl/uprobe_gl_sink_cube.h>
#include <upipe-filters/upipe_filter_blend.h>

#include <ev.h>
#include <pthread.h>

#define ALIVE() { printf("ALIVE %s %d\n", __func__, __LINE__); fflush(stdout);}
#define UPROBE_LOG_LEVEL UPROBE_LOG_NOTICE
#define GLX_QUEUE_LENGTH 2
#define DEC_QUEUE_LENGTH 50
#define UMEM_POOL 512
#define UDICT_POOL_DEPTH 500
#define UREF_POOL_DEPTH 500
#define UBUF_POOL_DEPTH 3000
#define UBUF_SHARED_POOL_DEPTH 50
#define UPUMP_POOL 10
#define UPUMP_BLOCKER_POOL 10
#define XFER_QUEUE          255
#define XFER_POOL           20
#define READ_SIZE 4096

/** @This defines a glxplayer context. */
struct upipe_glxplayer {
    /* configuration */
    enum uprobe_log_level loglevel;
    char *uri;
    bool upipe_ts;

    /* managers */
    struct upipe_mgr *upipe_filter_blend_mgr;
    struct upipe_mgr *upipe_sws_mgr;
    struct upipe_mgr *upipe_qsink_mgr;
    struct upipe_mgr *upipe_qsrc_mgr;
    struct upipe_mgr *upipe_glx_mgr;
    struct upipe_mgr *upipe_trickp_mgr;
    struct upipe_mgr *upipe_avcdec_mgr;
    struct upipe_mgr *upipe_null_mgr;

    /* probes */
    struct uprobe *uprobe_logger;
    struct uprobe *uprobe_dejitter;
    struct uprobe *uprobe_selflow;
    struct uprobe *uprobe_selprog;
    struct uprobe uprobe_source_s;
    struct uprobe uprobe_demux_output_s;
    struct uprobe uprobe_dec_qsrc_s;
    struct uprobe uprobe_avcdec_s;
    struct uprobe uprobe_glx_qsrc_s;
    struct uprobe uprobe_glx_s;

    /* main thread state */
    struct upipe *upipe_src_xfer;
    struct upipe *upipe_glx_qsrc;
    bool trickp;
    struct upipe_mgr *src_xfer;
    pthread_t src_thread_id;
    struct upipe *upipe_trickp;
    bool paused;

    /* source thread state */
    struct upipe_mgr *dec_xfer;
    struct upipe *upipe_dec_qsink;
    struct upipe *upipe_dec_qsrc_handle;
    pthread_t dec_thread_id;

    /* avcdec thread state */
    struct upipe *upipe_glx_qsink;
};

/** @This starts the source thread.
 *
 * @param _glxplayer pointer to glxplayer structure
 * @return always NULL
 */
static void *upipe_glxplayer_source_thread(void *_glxplayer)
{
    struct upipe_glxplayer *glxplayer = (struct upipe_glxplayer *)_glxplayer;

    struct ev_loop *loop = ev_loop_new(0);
    struct upump_mgr *upump_mgr =
        upump_ev_mgr_alloc(loop, UPUMP_POOL, UPUMP_BLOCKER_POOL);
    upipe_xfer_mgr_attach(glxplayer->src_xfer, upump_mgr);
    uprobe_pthread_upump_mgr_set(glxplayer->uprobe_logger, upump_mgr);
    upump_mgr_release(upump_mgr);

    ev_loop(loop, 0);

    ev_loop_destroy(loop);
    printf("end of source thread\n");
    upipe_mgr_release(glxplayer->dec_xfer);
    pthread_join(glxplayer->dec_thread_id, NULL);

    return NULL;
}

/** @This starts the avcodec thread.
 *
 * @param _glxplayer pointer to glxplayer structure
 * @return always NULL
 */
static void *upipe_glxplayer_dec_thread(void *_glxplayer)
{
    struct upipe_glxplayer *glxplayer = (struct upipe_glxplayer *)_glxplayer;

    struct ev_loop *loop = ev_loop_new(0);
    struct upump_mgr *upump_mgr =
        upump_ev_mgr_alloc(loop, UPUMP_POOL, UPUMP_BLOCKER_POOL);
    upipe_xfer_mgr_attach(glxplayer->dec_xfer, upump_mgr);
    uprobe_pthread_upump_mgr_set(glxplayer->uprobe_logger, upump_mgr);
    upump_mgr_release(upump_mgr);

    ev_loop(loop, 0);

    ev_loop_destroy(loop);
    printf("end of avc thread\n");

    return NULL;
}

/** @internal @This catches events of the source.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return an error code
 */
static int upipe_glxplayer_catch_source(struct uprobe *uprobe,
                                        struct upipe *upipe,
                                        int event, va_list args)
{
    switch (event) {
        case UPROBE_SOURCE_END: {
            struct upipe_glxplayer *glxplayer =
                container_of(uprobe, struct upipe_glxplayer, uprobe_source_s);
            if (glxplayer->upipe_ts)
                upipe_set_uri(upipe, glxplayer->uri);
            return UBASE_ERR_NONE;
        }
        default:
            return uprobe_throw_next(uprobe, upipe, event, args);
    }
}

/** @internal @This catches events of the video output of the demux.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return an error code
 */
static int upipe_glxplayer_catch_demux_output(struct uprobe *uprobe,
                                              struct upipe *upipe,
                                              int event, va_list args)
{
    struct upipe_glxplayer *glxplayer =
        container_of(uprobe, struct upipe_glxplayer, uprobe_demux_output_s);
    switch (event) {
        case UPROBE_NEED_OUTPUT: {
            struct uref *flow_def = va_arg(args, struct uref *);
            const char *def = "(none)";
            if (!ubase_check(uref_flow_get_def(flow_def, &def)) ||
                ubase_ncmp(def, "block.")) {
                upipe_warn_va(upipe, "flow def %s is not supported", def);
                return UBASE_ERR_UNHANDLED;
            }

            upipe_dbg_va(upipe, "add flow %s", def);

            /* prepare a queue to deport avcodec to a new thread */
            uprobe_throw(glxplayer->uprobe_logger, NULL, UPROBE_FREEZE_UPUMP_MGR);
            struct upipe *upipe_dec_qsrc =
                upipe_qsrc_alloc(glxplayer->upipe_qsrc_mgr,
                    uprobe_pfx_alloc_va(uprobe_use(&glxplayer->uprobe_dec_qsrc_s),
                                              glxplayer->loglevel, "dec qsrc"),
                    DEC_QUEUE_LENGTH);
            if (unlikely(upipe_dec_qsrc == NULL)) {
                return UBASE_ERR_ALLOC;
            }
            uprobe_throw(glxplayer->uprobe_logger, NULL, UPROBE_THAW_UPUMP_MGR);

            glxplayer->upipe_dec_qsink =
                upipe_qsink_alloc(glxplayer->upipe_qsink_mgr,
                    uprobe_pfx_alloc_va(
                            uprobe_use(glxplayer->uprobe_logger),
                            glxplayer->loglevel, "dec qsink"),
                    upipe_dec_qsrc);
            if (unlikely(glxplayer->upipe_dec_qsink == NULL)) {
                upipe_release(upipe_dec_qsrc);
                return UBASE_ERR_ALLOC;
            }
            upipe_set_output(upipe, glxplayer->upipe_dec_qsink);

            /* prepare to transfer the queue source */
            glxplayer->dec_xfer = upipe_xfer_mgr_alloc(XFER_QUEUE, XFER_POOL);
            if (unlikely(glxplayer->dec_xfer == NULL)) {
                upipe_release(upipe_dec_qsrc);
                return UBASE_ERR_ALLOC;
            }

            /* spawn a thread for the decoder */
            if (pthread_create(&glxplayer->dec_thread_id, NULL,
                               upipe_glxplayer_dec_thread, glxplayer)) {
                upipe_mgr_release(glxplayer->dec_xfer);
                upipe_release(upipe_dec_qsrc);
                return UBASE_ERR_ALLOC;
            }

            glxplayer->upipe_dec_qsrc_handle =
                upipe_xfer_alloc(glxplayer->dec_xfer,
                    uprobe_pfx_alloc(uprobe_use(glxplayer->uprobe_logger),
                                     glxplayer->loglevel, "dec qsrc xfer"),
                    upipe_dec_qsrc);
            if (unlikely(glxplayer->upipe_dec_qsrc_handle == NULL)) {
                upipe_mgr_release(glxplayer->dec_xfer);
                upipe_release(upipe_dec_qsrc);
                return UBASE_ERR_ALLOC;
            }
            upipe_attach_upump_mgr(glxplayer->upipe_dec_qsrc_handle);
            upipe_set_output(glxplayer->upipe_dec_qsink, glxplayer->upipe_dec_qsrc_handle);
            return UBASE_ERR_NONE;
        }
        case UPROBE_SOURCE_END: {
            upipe_flush(glxplayer->upipe_dec_qsink);
            upipe_release(glxplayer->upipe_dec_qsink);
            glxplayer->upipe_dec_qsink = NULL;

            /* set dec_qsrc output to null */
            struct upipe *null = upipe_void_alloc(glxplayer->upipe_null_mgr,
                    uprobe_pfx_alloc(uprobe_use(glxplayer->uprobe_logger),
                                     glxplayer->loglevel, "dec qsrc null"));
            if (likely(null != NULL)) {
                upipe_set_output(glxplayer->upipe_dec_qsrc_handle, null);
                upipe_release(null);
            }
            upipe_release(glxplayer->upipe_dec_qsrc_handle);
            return UBASE_ERR_NONE;
        }
        default:
            return uprobe_throw_next(uprobe, upipe, event, args);
    }
}

/** @internal @This catches events of the dec queue source.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return an error code
 */
static int upipe_glxplayer_catch_dec_qsrc(struct uprobe *uprobe,
                                          struct upipe *upipe,
                                          int event, va_list args)
{
    switch (event) {
        case UPROBE_SOURCE_END: {
            struct upipe_glxplayer *glxplayer =
                container_of(uprobe, struct upipe_glxplayer, uprobe_dec_qsrc_s);
            upipe_flush(glxplayer->upipe_glx_qsink);
            upipe_release(glxplayer->upipe_glx_qsink);
            return UBASE_ERR_NONE;
        }
        case UPROBE_NEED_OUTPUT: {
            struct upipe_glxplayer *glxplayer =
                container_of(uprobe, struct upipe_glxplayer, uprobe_dec_qsrc_s);
            struct upipe *avcdec = upipe_void_alloc_output(upipe,
                    glxplayer->upipe_avcdec_mgr,
                    uprobe_pfx_alloc_va(
                            uprobe_use(&glxplayer->uprobe_avcdec_s),
                        glxplayer->loglevel, "avcdec"));
            if (unlikely(avcdec == NULL))
                return UBASE_ERR_ALLOC;
            upipe_set_option(avcdec, "threads", "2");
            upipe_release(avcdec);
            return UBASE_ERR_NONE;
        }
        default:
            return uprobe_throw_next(uprobe, upipe, event, args);
    }
}

/** @internal @This catches events of the avcdec.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return an error code
 */
static int upipe_glxplayer_catch_avcdec(struct uprobe *uprobe,
                                        struct upipe *upipe,
                                        int event, va_list args)
{
    switch (event) {
        case UPROBE_NEED_OUTPUT: {
            struct upipe_glxplayer *glxplayer =
                container_of(uprobe, struct upipe_glxplayer, uprobe_avcdec_s);
            struct uref *flow_def = va_arg(args, struct uref *);
            struct upipe *deint =
                upipe_void_alloc_output(upipe,
                        glxplayer->upipe_filter_blend_mgr,
                        uprobe_pfx_alloc(uprobe_use(glxplayer->uprobe_logger),
                                         glxplayer->loglevel, "deint"));
            if (unlikely(deint == NULL))
                return UBASE_ERR_ALLOC;

            struct uref *output_flow = uref_dup(flow_def);
            if (unlikely(output_flow == NULL))
                return UBASE_ERR_ALLOC;
            uref_pic_flow_clear_format(output_flow);
            if (unlikely(!ubase_check(uref_pic_flow_set_macropixel(output_flow, 1)) ||
                         !ubase_check(uref_pic_flow_set_planes(output_flow, 0)) ||
                         !ubase_check(uref_pic_flow_add_plane(output_flow, 1, 1, 3,
                                                  "r8g8b8")))) {
                uref_free(output_flow);
                return UBASE_ERR_ALLOC;
            }

            struct upipe *yuvrgb = upipe_flow_alloc_output(deint,
                    glxplayer->upipe_sws_mgr,
                    uprobe_pfx_alloc_va(uprobe_use(glxplayer->uprobe_logger),
                                        glxplayer->loglevel, "rgb"),
                    output_flow);
            assert(yuvrgb != NULL);
            uref_free(output_flow);
            upipe_release(deint);

            glxplayer->upipe_glx_qsink =
                upipe_qsink_alloc(glxplayer->upipe_qsink_mgr,
                    uprobe_pfx_alloc(uprobe_use(glxplayer->uprobe_logger),
                    glxplayer->loglevel, "glx qsink"),
                    glxplayer->upipe_glx_qsrc);
            if (unlikely(glxplayer->upipe_glx_qsink == NULL))
                return UBASE_ERR_ALLOC;
            upipe_set_output(yuvrgb, glxplayer->upipe_glx_qsink);
            upipe_release(yuvrgb);
            return UBASE_ERR_NONE;
        }
        default:
            return uprobe_throw_next(uprobe, upipe, event, args);
    }
}

/** @internal @This catches events of the qsrc of the glx (main) thread.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return an error code
 */
static int upipe_glxplayer_catch_glx_qsrc(struct uprobe *uprobe,
                                          struct upipe *upipe,
                                          int event, va_list args)
{
    switch (event) {
        case UPROBE_SOURCE_END: {
            struct upipe_glxplayer *glxplayer =
                container_of(uprobe, struct upipe_glxplayer,
                             uprobe_glx_qsrc_s);
            upipe_release(upipe);
            pthread_join(glxplayer->src_thread_id, NULL);
            if (glxplayer->trickp)
                upipe_release(glxplayer->upipe_trickp);
            free(glxplayer->uri);
            return UBASE_ERR_NONE;
        }
        case UPROBE_NEED_OUTPUT: {
            struct upipe_glxplayer *glxplayer =
                container_of(uprobe, struct upipe_glxplayer,
                             uprobe_glx_qsrc_s);
            struct upipe *trickp_pic;
            if (glxplayer->trickp) {
                glxplayer->upipe_trickp =
                    upipe_void_alloc(glxplayer->upipe_trickp_mgr,
                         uprobe_pfx_alloc(uprobe_use(glxplayer->uprobe_logger),
                                          glxplayer->loglevel, "trickp"));
                if (unlikely(glxplayer->upipe_trickp == NULL))
                    return UBASE_ERR_ALLOC;
                upipe_attach_uclock(glxplayer->upipe_trickp);
                trickp_pic = upipe_void_alloc_output_sub(upipe,
                        glxplayer->upipe_trickp,
                        uprobe_pfx_alloc(uprobe_use(glxplayer->uprobe_logger),
                                         glxplayer->loglevel, "trickp pic"));
                if (unlikely(trickp_pic == NULL))
                    return UBASE_ERR_ALLOC;
            } else
                trickp_pic = upipe;

            /* glx sink */
            struct upipe *glx_sink = upipe_void_alloc_output(trickp_pic,
                    glxplayer->upipe_glx_mgr,
                    uprobe_gl_sink_cube_alloc(
                         uprobe_pfx_alloc(
                             uprobe_use(&glxplayer->uprobe_glx_s),
                             glxplayer->loglevel, "glx")));
            if (glxplayer->trickp)
                upipe_release(trickp_pic);
            if (unlikely(glx_sink == NULL))
                return UBASE_ERR_ALLOC;
            upipe_glx_sink_init(glx_sink, 0, 0, 800, 480);
            upipe_attach_uclock(glx_sink);
            upipe_release(glx_sink);
            return UBASE_ERR_NONE;
        }
        default:
            return uprobe_throw_next(uprobe, upipe, event, args);
    }
}

/** @internal @This catches events of the glx pipe.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return an error code
 */
static int upipe_glxplayer_catch_glx(struct uprobe *uprobe,
                                     struct upipe *upipe,
                                     int event, va_list args)
{
    switch (event) {
        case UPROBE_GLX_SINK_KEYPRESS: {
            struct upipe_glxplayer *glxplayer =
                container_of(uprobe, struct upipe_glxplayer,
                             uprobe_glx_s);
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_GLX_SINK_SIGNATURE);
            unsigned long key = va_arg(args, unsigned long);

            switch (key) {
                case 27:
                case 'q': {
                    upipe_notice_va(upipe, "exit key pressed (%d), exiting",
                                    key);
                    upipe_release(glxplayer->upipe_src_xfer);
                    upipe_mgr_release(glxplayer->src_xfer);
                    break;
                }
                case ' ': {
                    if (glxplayer->trickp) {
                        if ( (glxplayer->paused = !glxplayer->paused) ) {
                            upipe_notice(upipe, "Playback paused");
                            struct urational rate = { .num = 0, .den = 0 };
                            upipe_trickp_set_rate(glxplayer->upipe_trickp, rate);
                        } else {
                            upipe_notice(upipe, "Playback resumed");
                            struct urational rate = { .num = 1, .den = 1 };
                            upipe_trickp_set_rate(glxplayer->upipe_trickp, rate);
                        }
                    }
                    break;
                }
                default:
                    upipe_dbg_va(upipe, "key pressed (%d)", key);
                    break;
            }
            return UBASE_ERR_NONE;
        }
        case UPROBE_GLX_SINK_KEYRELEASE:
            return UBASE_ERR_NONE;
        default:
            break;
    }
    return uprobe_throw_next(uprobe, upipe, event, args);
}

/** @This allocates and initializes a glxplayer context.
 *
 * @param loglevel minimum log level to print
 * @return pointer to glxplayer context, or NULL in case of error
 */
struct upipe_glxplayer *upipe_glxplayer_alloc(enum uprobe_log_level loglevel)
{
    struct upipe_glxplayer *glxplayer = malloc(sizeof(struct upipe_glxplayer));
    if (unlikely(glxplayer == NULL))
        return NULL;
    glxplayer->loglevel = loglevel;
    glxplayer->uri = NULL;
    glxplayer->upipe_ts = false;
    glxplayer->paused = false;

    struct umem_mgr *umem_mgr = umem_pool_mgr_alloc_simple(UMEM_POOL);
    if (unlikely(umem_mgr == NULL))
        goto fail_umem_mgr;

    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    if (unlikely(udict_mgr == NULL))
        goto fail_udict_mgr;

    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    if (unlikely(uref_mgr == NULL))
        goto fail_uref_mgr;

    struct uclock *uclock = uclock_std_alloc(0);
    if (unlikely(uclock == NULL))
        goto fail_probe_logger;

    /* probes common to all threads */
    glxplayer->uprobe_logger =
        uprobe_pthread_upump_mgr_alloc(
            uprobe_ubuf_mem_alloc(
                uprobe_uclock_alloc(
                    uprobe_uref_mgr_alloc(
                         uprobe_stdio_alloc(NULL, stderr, glxplayer->loglevel),
                         uref_mgr), uclock),
                umem_mgr, UBUF_POOL_DEPTH, UBUF_SHARED_POOL_DEPTH));
    uclock_release(uclock);
    if (unlikely(glxplayer->uprobe_logger == NULL))
        goto fail_probe_logger;

    /* upipe-av */
    if (unlikely(!upipe_av_init(false, uprobe_use(glxplayer->uprobe_logger))))
        goto fail_av;

    /* pipes managers */
    glxplayer->upipe_filter_blend_mgr = upipe_filter_blend_mgr_alloc();
    glxplayer->upipe_sws_mgr = upipe_sws_mgr_alloc();
    glxplayer->upipe_qsink_mgr = upipe_qsink_mgr_alloc();
    glxplayer->upipe_qsrc_mgr = upipe_qsrc_mgr_alloc();
    glxplayer->upipe_glx_mgr = upipe_glx_sink_mgr_alloc();
    glxplayer->upipe_trickp_mgr = upipe_trickp_mgr_alloc();
    glxplayer->upipe_avcdec_mgr = upipe_avcdec_mgr_alloc();
    glxplayer->upipe_null_mgr = upipe_null_mgr_alloc();
    if (unlikely(glxplayer->upipe_filter_blend_mgr == NULL ||
                 glxplayer->upipe_sws_mgr == NULL ||
                 glxplayer->upipe_qsink_mgr == NULL ||
                 glxplayer->upipe_qsrc_mgr == NULL ||
                 glxplayer->upipe_glx_mgr == NULL ||
                 glxplayer->upipe_trickp_mgr == NULL ||
                 glxplayer->upipe_avcdec_mgr == NULL ||
                 glxplayer->upipe_null_mgr == NULL))
        goto fail_pipe_mgrs;

    /* probe specific to the source pipe */
    uprobe_init(&glxplayer->uprobe_source_s, upipe_glxplayer_catch_source,
                uprobe_use(glxplayer->uprobe_logger));

    /* probes specific to the demux pipe */
    glxplayer->uprobe_dejitter =
        uprobe_dejitter_alloc(uprobe_use(glxplayer->uprobe_logger), false, 0);
    if (unlikely(glxplayer->uprobe_dejitter == NULL))
        goto fail_dejitter;

    uprobe_init(&glxplayer->uprobe_demux_output_s,
                upipe_glxplayer_catch_demux_output,
                uprobe_use(glxplayer->uprobe_dejitter));

    glxplayer->uprobe_selflow =
        uprobe_selflow_alloc(uprobe_use(glxplayer->uprobe_dejitter),
                             uprobe_use(&glxplayer->uprobe_demux_output_s),
                             UPROBE_SELFLOW_PIC, "auto");
    if (unlikely(glxplayer->uprobe_selflow == NULL))
        goto fail_selflow;
    glxplayer->uprobe_selprog =
        uprobe_selflow_alloc(uprobe_use(glxplayer->uprobe_logger),
                             uprobe_use(glxplayer->uprobe_selflow),
                             UPROBE_SELFLOW_VOID, "auto");
    if (unlikely(glxplayer->uprobe_selprog == NULL))
        goto fail_selprog;

    /* probe specific to the dec queue source */
    uprobe_init(&glxplayer->uprobe_dec_qsrc_s,
                upipe_glxplayer_catch_dec_qsrc,
                uprobe_use(glxplayer->uprobe_logger));

    /* probe specific to the avcdec */
    uprobe_init(&glxplayer->uprobe_avcdec_s,
                upipe_glxplayer_catch_avcdec,
                uprobe_use(glxplayer->uprobe_logger));

    /* probe specific to the glx queue source */
    uprobe_init(&glxplayer->uprobe_glx_qsrc_s,
                upipe_glxplayer_catch_glx_qsrc,
                uprobe_use(glxplayer->uprobe_logger));

    /* probe specific to the glx pipe */
    uprobe_init(&glxplayer->uprobe_glx_s,
                upipe_glxplayer_catch_glx,
                uprobe_use(glxplayer->uprobe_logger));

    uref_mgr_release(uref_mgr);
    umem_mgr_release(umem_mgr);
    udict_mgr_release(udict_mgr);
    return glxplayer;

fail_selprog:
    uprobe_release(glxplayer->uprobe_selflow);
fail_selflow:
    uprobe_clean(&glxplayer->uprobe_demux_output_s);
    uprobe_release(glxplayer->uprobe_dejitter);
fail_dejitter:
    uprobe_clean(&glxplayer->uprobe_source_s);
fail_pipe_mgrs:
    if (glxplayer->upipe_filter_blend_mgr != NULL)
        upipe_mgr_release(glxplayer->upipe_filter_blend_mgr);
    if (glxplayer->upipe_sws_mgr != NULL)
        upipe_mgr_release(glxplayer->upipe_sws_mgr);
    if (glxplayer->upipe_qsink_mgr != NULL)
        upipe_mgr_release(glxplayer->upipe_qsink_mgr);
    if (glxplayer->upipe_qsrc_mgr != NULL)
        upipe_mgr_release(glxplayer->upipe_qsrc_mgr);
    if (glxplayer->upipe_glx_mgr != NULL)
        upipe_mgr_release(glxplayer->upipe_glx_mgr);
    if (glxplayer->upipe_trickp_mgr != NULL)
        upipe_mgr_release(glxplayer->upipe_trickp_mgr);
    if (glxplayer->upipe_avcdec_mgr != NULL)
        upipe_mgr_release(glxplayer->upipe_avcdec_mgr);
    if (glxplayer->upipe_null_mgr != NULL)
        upipe_mgr_release(glxplayer->upipe_null_mgr);
    upipe_av_clean();
fail_av:
    uprobe_release(glxplayer->uprobe_logger);
fail_probe_logger:
    uref_mgr_release(uref_mgr);
fail_uref_mgr:
    udict_mgr_release(udict_mgr);
fail_udict_mgr:
    umem_mgr_release(umem_mgr);
fail_umem_mgr:
    free(glxplayer);
    return NULL;
}

/** @This starts playing the given URI.
 *
 * @param glxplayer glxplayer context
 * @param upump_mgr upump manager of the main thread
 * @param uri URI to play
 * @param upipe_ts set to true to use Upipe's TS demux, false for libavformat
 * @return false in case of error
 */
bool upipe_glxplayer_play(struct upipe_glxplayer *glxplayer,
                          struct upump_mgr *upump_mgr, const char *uri,
                          bool upipe_ts)
{
    struct upipe *upipe_src;
    uprobe_pthread_upump_mgr_set(glxplayer->uprobe_logger, upump_mgr);
    uprobe_throw(glxplayer->uprobe_logger, NULL, UPROBE_FREEZE_UPUMP_MGR);
    if (!upipe_ts) {
        /* use avformat source (and internal demuxer) */
        struct upipe_mgr *upipe_avfsrc_mgr = upipe_avfsrc_mgr_alloc();
        if (unlikely(upipe_avfsrc_mgr == NULL))
            return false;

        upipe_src = upipe_void_alloc(upipe_avfsrc_mgr,
                    uprobe_pfx_alloc(uprobe_use(glxplayer->uprobe_selflow),
                                     glxplayer->loglevel, "avfsrc"));
        upipe_mgr_release(upipe_avfsrc_mgr);
        if (unlikely(upipe_src == NULL))
            return false;
        if (unlikely(!ubase_check(upipe_attach_uclock(upipe_src)) ||
                     !ubase_check(upipe_set_uri(upipe_src, uri)))) {
            upipe_release(upipe_src);
            return false;
        }
        glxplayer->trickp = true;
    } else {
        /* try file source */
        struct upipe_mgr *upipe_fsrc_mgr = upipe_fsrc_mgr_alloc();
        if (unlikely(upipe_fsrc_mgr == NULL))
            return false;

        upipe_src = upipe_void_alloc(upipe_fsrc_mgr,
                    uprobe_pfx_alloc(uprobe_use(&glxplayer->uprobe_source_s),
                                     glxplayer->loglevel, "fsrc"));
        upipe_mgr_release(upipe_fsrc_mgr);
        if (unlikely(upipe_src == NULL))
            return false;
        if (ubase_check(upipe_set_uri(upipe_src, uri))) {
            glxplayer->trickp = true;
        } else {
            upipe_release(upipe_src);
            glxplayer->trickp = false;

            /* try udp source */
            struct upipe_mgr *upipe_udpsrc_mgr = upipe_udpsrc_mgr_alloc();
            if (unlikely(upipe_udpsrc_mgr == NULL))
                return false;

            upipe_src = upipe_void_alloc(upipe_udpsrc_mgr,
                    uprobe_pfx_alloc(uprobe_use(&glxplayer->uprobe_source_s),
                                     glxplayer->loglevel, "udpsrc"));
            upipe_mgr_release(upipe_udpsrc_mgr);
            if (unlikely(upipe_src == NULL))
                return false;
            if (ubase_check(upipe_set_uri(upipe_src, uri))) {
                upipe_attach_uclock(upipe_src);
            } else {
                upipe_release(upipe_src);

                /* try http source */
                struct upipe_mgr *upipe_http_src_mgr =
                    upipe_http_src_mgr_alloc();
                if (unlikely(upipe_http_src_mgr == NULL))
                    return false;

                upipe_src = upipe_void_alloc(upipe_http_src_mgr,
                    uprobe_pfx_alloc(uprobe_use(&glxplayer->uprobe_source_s),
                                     glxplayer->loglevel, "httpsrc"));
                upipe_mgr_release(upipe_http_src_mgr);
                if (unlikely(upipe_src == NULL))
                    return false;
                if (!ubase_check(upipe_set_uri(upipe_src, uri))) {
                    upipe_release(upipe_src);
                    return false;
                }
            }
        }

        /* upipe-ts demuxer */
        struct upipe_mgr *upipe_ts_demux_mgr = upipe_ts_demux_mgr_alloc();
        if (unlikely(upipe_ts_demux_mgr == NULL)) {
            upipe_release(upipe_src);
            return false;
        }
        struct upipe_mgr *upipe_mpgvf_mgr = upipe_mpgvf_mgr_alloc();
        if (upipe_mpgvf_mgr != NULL) {
            upipe_ts_demux_mgr_set_mpgvf_mgr(upipe_ts_demux_mgr,
                                             upipe_mpgvf_mgr);
            upipe_mgr_release(upipe_mpgvf_mgr);
        }
        struct upipe_mgr *upipe_h264f_mgr = upipe_h264f_mgr_alloc();
        if (upipe_h264f_mgr != NULL) {
            upipe_ts_demux_mgr_set_h264f_mgr(upipe_ts_demux_mgr,
                                             upipe_h264f_mgr);
            upipe_mgr_release(upipe_h264f_mgr);
        }

        struct upipe *upipe_ts_demux =
            upipe_void_alloc_output(upipe_src, upipe_ts_demux_mgr,
                uprobe_pfx_alloc(uprobe_use(glxplayer->uprobe_selprog),
                                 glxplayer->loglevel, "ts demux"));
        if (unlikely(upipe_ts_demux == NULL)) {
            upipe_mgr_release(upipe_ts_demux_mgr);
            upipe_release(upipe_src);
            return false;
        }
        upipe_release(upipe_ts_demux);
        upipe_mgr_release(upipe_ts_demux_mgr);
    }

    if (!glxplayer->trickp)
        uprobe_dejitter_set(glxplayer->uprobe_dejitter, true, 0);

    /* prepare a queue source for the decoded video frames */
    uprobe_throw(glxplayer->uprobe_logger, NULL, UPROBE_THAW_UPUMP_MGR);
    glxplayer->upipe_glx_qsrc = upipe_qsrc_alloc(glxplayer->upipe_qsrc_mgr,
            uprobe_pfx_alloc(
                    uprobe_use(&glxplayer->uprobe_glx_qsrc_s),
                glxplayer->loglevel, "glx qsrc"), GLX_QUEUE_LENGTH);
    if (unlikely(glxplayer->upipe_glx_qsrc == NULL)) {
        upipe_release(upipe_src);
        return false;
    }
    upipe_attach_upump_mgr(glxplayer->upipe_glx_qsrc);

    /* prepare to transfer the source to a new thread */
    glxplayer->src_xfer = upipe_xfer_mgr_alloc(XFER_QUEUE, XFER_POOL);
    if (unlikely(glxplayer->src_xfer == NULL)) {
        upipe_release(upipe_src);
        upipe_release(glxplayer->upipe_glx_qsrc);
        return false;
    }

    /* spawn a thread for the source */
    if (pthread_create(&glxplayer->src_thread_id, NULL,
                       upipe_glxplayer_source_thread, glxplayer)) {
        upipe_mgr_release(glxplayer->src_xfer);
        upipe_release(glxplayer->upipe_glx_qsrc);
        return false;
    }

    glxplayer->upipe_src_xfer = upipe_xfer_alloc(glxplayer->src_xfer,
                uprobe_pfx_alloc(uprobe_use(glxplayer->uprobe_logger),
                                 glxplayer->loglevel, "source xfer"),
                upipe_src);
    if (unlikely(glxplayer->upipe_src_xfer == NULL)) {
        upipe_mgr_release(glxplayer->src_xfer);
        upipe_release(upipe_src);
        upipe_release(glxplayer->upipe_glx_qsrc);
        return false;
    }
    /* from now on upipe_src should not be accessed */
    upipe_attach_upump_mgr(glxplayer->upipe_src_xfer);

    glxplayer->upipe_ts = upipe_ts;
    glxplayer->uri = strdup(uri);
    return true;
}

/** @This frees a glxplayer resource.
 *
 * @param glxplayer glxplayer context
 */
void upipe_glxplayer_free(struct upipe_glxplayer *glxplayer)
{
    uprobe_clean(&glxplayer->uprobe_dec_qsrc_s);
    uprobe_clean(&glxplayer->uprobe_avcdec_s);
    uprobe_clean(&glxplayer->uprobe_glx_qsrc_s);
    uprobe_clean(&glxplayer->uprobe_glx_s);
    uprobe_release(glxplayer->uprobe_selprog);
    uprobe_release(glxplayer->uprobe_selflow);
    uprobe_clean(&glxplayer->uprobe_demux_output_s);
    uprobe_release(glxplayer->uprobe_dejitter);
    uprobe_clean(&glxplayer->uprobe_source_s);
    upipe_mgr_release(glxplayer->upipe_filter_blend_mgr);
    upipe_mgr_release(glxplayer->upipe_sws_mgr);
    upipe_mgr_release(glxplayer->upipe_qsink_mgr);
    upipe_mgr_release(glxplayer->upipe_qsrc_mgr);
    upipe_mgr_release(glxplayer->upipe_glx_mgr);
    upipe_mgr_release(glxplayer->upipe_trickp_mgr);
    upipe_mgr_release(glxplayer->upipe_avcdec_mgr);
    upipe_mgr_release(glxplayer->upipe_null_mgr);
    upipe_av_clean();
    uprobe_release(glxplayer->uprobe_logger);
    free(glxplayer);
}

int main(int argc, char** argv)
{
    enum uprobe_log_level loglevel = UPROBE_LOG_LEVEL;
    bool upipe_ts = false;
    int opt;
    // parse options
    while ((opt = getopt(argc, argv, "dt")) != -1) {
        switch (opt) {
            case 'd':
                loglevel--;
                break;
            case 't':
                upipe_ts = true;
                break;
            default:
                break;
        }
    }
    if (optind >= argc) {
        printf("Usage: %s [-d] [-t] filename\n", argv[0]);
        exit(-1);
    }

    const char *uri = argv[optind++];

    // upipe env
    struct ev_loop *loop = ev_default_loop(0);
    struct upump_mgr *upump_mgr =
        upump_ev_mgr_alloc(loop, UPUMP_POOL, UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);
    struct upipe_glxplayer *glxplayer = upipe_glxplayer_alloc(loglevel);
    assert(glxplayer != NULL);

    upipe_glxplayer_play(glxplayer, upump_mgr, uri, upipe_ts);
    upump_mgr_release(upump_mgr);

    ev_loop(loop, 0);

    upipe_glxplayer_free(glxplayer);
    ev_default_destroy();

    return 0; 
}

