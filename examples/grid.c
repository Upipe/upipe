/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
 *
 * Authors: Arnaud de Turckheim
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

#include <upipe-modules/upipe_void_source.h>
#include <upipe-modules/upipe_dup.h>
#include <upipe-modules/upipe_rtp_source.h>
#include <upipe-modules/upipe_grid.h>
#include <upipe-modules/upipe_null.h>
#include <upipe-modules/upipe_video_blank.h>
#include <upipe-modules/upipe_audio_blank.h>
#include <upipe-modules/upipe_rtp_h264.h>
#include <upipe-modules/upipe_rtp_prepend.h>
#include <upipe-modules/upipe_udp_sink.h>
#include <upipe-modules/upipe_setflowdef.h>
#include <upipe-modules/upipe_worker_sink.h>
#include <upipe-modules/upipe_worker_linear.h>
#include <upipe-modules/upipe_audio_copy.h>

#include <upipe-swresample/upipe_swr.h>
#include <upipe-swscale/upipe_sws.h>

#include <upipe-ts/upipe_ts_demux.h>
#include <upipe-ts/upipe_ts_mux.h>

#include <upipe-framers/upipe_auto_framer.h>

#include <upipe-filters/upipe_filter_decode.h>
#include <upipe-filters/upipe_filter_encode.h>
#include <upipe-filters/upipe_filter_format.h>

#include <upipe-pthread/uprobe_pthread_upump_mgr.h>
#include <upipe-pthread/upipe_pthread_transfer.h>

#include <upipe-av/upipe_av.h>
#include <upipe-av/upipe_av_pixfmt.h>
#include <upipe-av/upipe_av_samplefmt.h>
#include <upipe-av/upipe_avformat_source.h>
#include <upipe-av/upipe_avcodec_decode.h>
#include <upipe-av/upipe_avcodec_encode.h>

#include <upipe-x264/upipe_x264.h>

#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_uclock.h>
#include <upipe/uprobe_upump_mgr.h>
#include <upipe/uprobe_ubuf_mem_pool.h>
#include <upipe/uprobe_select_flows.h>
#include <upipe/uprobe_dejitter.h>

#include <upipe/uref_std.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_void_flow.h>
#include <upipe/uref_block_flow.h>

#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uclock_std.h>
#include <upipe/uclock.h>
#include <upipe/ustring.h>
#include <upipe/upipe.h>

#include <upump-ev/upump_ev.h>

#include <libswscale/swscale.h>

#include <getopt.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>

#define UPROBE_LOG_LEVEL                UPROBE_LOG_NOTICE
#define UPUMP_POOL                      5
#define UPUMP_BLOCKER_POOL              5
#define UDICT_POOL_DEPTH                500
#define UREF_POOL_DEPTH                 500
#define UBUF_POOL_DEPTH                 3000
#define UBUF_SHARED_POOL_DEPTH          50
#define TS_PAYLOAD_SIZE                 1316
#define PADDING_OCTETRATE               128000
#define XFER_QUEUE                      255
#define XFER_POOL                       20
#define QUEUE_LENGTH                    255
#define SWS_FLAGS                       (SWS_FULL_CHR_H_INP | SWS_BICUBIC)
#define DEFAULT_RATE                    48000
#define DEFAULT_FPS                     25
#define DEFAULT_HEIGHT                  1280
#define DEFAULT_WIDTH                   720
#define DEFAULT_DURATION                (UCLOCK_FREQ / DEFAULT_FPS)
#define DEFAULT_SAMPLES                 (DEFAULT_RATE / DEFAULT_FPS)

struct input {
    struct uchain uchain;
    struct uprobe uprobe_video;
    struct uprobe uprobe_audio;
    struct upipe *source;
    struct upipe *video;
    struct upipe *sound;
    const char *uri;
    unsigned int id;
};

UBASE_FROM_TO(input, uchain, uchain, uchain);
UBASE_FROM_TO(input, uprobe, uprobe_video, uprobe_video);
UBASE_FROM_TO(input, uprobe, uprobe_audio, uprobe_audio);

struct output {
    struct uchain uchain;
    struct upipe *source;
    struct upipe *sound_src;
    struct upipe *upipe;
    struct upipe *sound;
    const char *uri;
    unsigned int id;
};

UBASE_FROM_TO(output, uchain, uchain, uchain);

enum opt {
    OPT_INPUT   = 'i',
    OPT_OUTPUT  = 'o',
    OPT_VERBOSE = 'v',
    OPT_HELP = 'h',
};

static struct option options[] = {
    { "input", required_argument, NULL, OPT_INPUT },
    { "output", required_argument, NULL, OPT_OUTPUT },
    { "verbose", no_argument, NULL, OPT_VERBOSE },
    { NULL, no_argument, NULL, 0 },
};

static int log_level = UPROBE_LOG_LEVEL;
static struct uref_mgr *uref_mgr = NULL;
static struct upipe *upipe_voidsrc = NULL;
static struct upipe *upipe_dup = NULL;
static struct upipe *upipe_grid = NULL;
static struct uprobe *uprobe_main = NULL;
static struct uchain inputs;
static struct uchain outputs;
static int mtu = TS_PAYLOAD_SIZE;
static enum upipe_ts_conformance conformance = UPIPE_TS_CONFORMANCE_AUTO;
static unsigned input_id = 0;
static unsigned output_id = 0;

static void usage(const char *name, int exit_code)
{
    fprintf(stderr, "name [options]\n"
            "\t-h                   : print this help\n"
            "\t-v\n"
            "\t--verbose            : be more verbose\n"
            "\t-i <input>\n"
            "\t--input <input>      : create a new input\n"
            "\t-o <output>\n"
            "\t--output <output>    : create a new output\n"
            );
    exit(exit_code);
}

static void sig_cb(struct upump *upump)
{
    static int done = false;
    struct uchain *uchain;

    if (done)
        abort();
    done = true;

    ulist_foreach(&outputs, uchain) {
        struct output *output = output_from_uchain(uchain);
        upipe_release(output->source);
        output->source = NULL;
        upipe_release(output->sound_src);
        output->sound_src = NULL;
        upipe_release(output->upipe);
        output->upipe = NULL;
        upipe_release(output->sound);
        output->sound = NULL;
    }

    ulist_foreach(&inputs, uchain) {
        struct input *input = input_from_uchain(uchain);
        upipe_release(input->source);
        input->source = NULL;
        upipe_release(input->video);
        input->video = NULL;
        upipe_release(input->sound);
        input->sound = NULL;
    }
    upipe_release(upipe_voidsrc);
    upipe_voidsrc = NULL;
    upipe_release(upipe_dup);
    upipe_dup = NULL;
}

static struct input *input_get(unsigned int id)
{
    struct uchain *uchain;
    ulist_foreach(&inputs, uchain) {
        struct input *input = input_from_uchain(uchain);
        if (input->id == id)
            return input;
    }
    return NULL;
}

static struct input *input_from_upipe(struct upipe *upipe)
{
    struct uchain *uchain;
    ulist_foreach(&inputs, uchain) {
        struct input *input = input_from_uchain(uchain);
        if (input->video == upipe)
            return input;
    }
    return NULL;
}

static struct output *output_get(unsigned int id)
{
    struct uchain *uchain;
    ulist_foreach(&outputs, uchain) {
        struct output *output = output_from_uchain(uchain);
        if (output->id == id)
            return output;
    }
    return NULL;
}

static int catch_video(struct uprobe *uprobe, struct upipe *upipe,
                       int event, va_list args)
{
    struct input *input = input_from_uprobe_video(uprobe);
    const char *def;
    struct uref *flow_def;
    if (!uprobe_plumber(event, args, &flow_def, &def))
        return uprobe_throw_next(uprobe, upipe, event, args);

    /* decoder */
    struct upipe_mgr *fdec_mgr = upipe_fdec_mgr_alloc();
    struct upipe_mgr *avcdec_mgr = upipe_avcdec_mgr_alloc();
    upipe_fdec_mgr_set_avcdec_mgr(fdec_mgr, avcdec_mgr);
    upipe_mgr_release(avcdec_mgr);
    struct upipe *avcdec =
        upipe_void_alloc(
            fdec_mgr,
            uprobe_pfx_alloc_va(
                uprobe_use(uprobe_main),
                UPROBE_LOG_VERBOSE, "fdec pic %u", input->id));
    upipe_mgr_release(fdec_mgr);
    assert(avcdec);
    upipe_set_option(avcdec, "threads", "1"); /* auto */
    upipe_set_option(avcdec, "ec", "0");

    struct upipe_mgr *xfer =
        upipe_pthread_xfer_mgr_alloc(
            XFER_QUEUE, XFER_POOL,
            uprobe_use(uprobe_main),
            upump_ev_mgr_alloc_loop,
            UPUMP_POOL, UPUMP_BLOCKER_POOL, NULL, NULL, NULL);
    assert(xfer);
    struct upipe_mgr *worker_mgr = upipe_wlin_mgr_alloc(xfer);
    assert(worker_mgr);
    upipe_mgr_release(xfer);
    avcdec =
        upipe_wlin_alloc(
            worker_mgr,
            uprobe_pfx_alloc_va(uprobe_use(uprobe_main),
                                log_level, "avcdec_w %u", input->id),
            avcdec,
            uprobe_pfx_alloc_va(uprobe_use(uprobe_main),
                                log_level, "avcdec_wx %u", input->id),
            QUEUE_LENGTH, QUEUE_LENGTH);
    assert(avcdec);
    upipe_mgr_release(worker_mgr);

    ubase_assert(upipe_set_output(upipe, avcdec));

    ubase_assert(upipe_set_output(avcdec, input->video));
    upipe_release(avcdec);
    return UBASE_ERR_NONE;
}

static int catch_audio(struct uprobe *uprobe, struct upipe *upipe,
                       int event, va_list args)
{
    struct input *input = input_from_uprobe_audio(uprobe);
    const char *def;
    struct uref *flow_def;

    if (!uprobe_plumber(event, args, &flow_def, &def))
        return uprobe_throw_next(uprobe, upipe, event, args);

    /* decoder */
    struct upipe_mgr *fdec_mgr = upipe_fdec_mgr_alloc();
    assert(fdec_mgr);
    struct upipe_mgr *avcdec_mgr = upipe_avcdec_mgr_alloc();
    assert(avcdec_mgr);
    upipe_fdec_mgr_set_avcdec_mgr(fdec_mgr, avcdec_mgr);
    upipe_mgr_release(avcdec_mgr);
    struct upipe *avcdec =
        upipe_void_alloc(
            fdec_mgr,
            uprobe_pfx_alloc_va(
                uprobe_use(uprobe_main),
                UPROBE_LOG_VERBOSE, "fdec snd %u", input->id));
    upipe_mgr_release(fdec_mgr);
    assert(avcdec);
    upipe_set_option(avcdec, "threads", "1"); /* auto */
    upipe_set_option(avcdec, "ec", "0");

    struct upipe_mgr *xfer =
        upipe_pthread_xfer_mgr_alloc(
            XFER_QUEUE, XFER_POOL,
            uprobe_use(uprobe_main),
            upump_ev_mgr_alloc_loop,
            UPUMP_POOL, UPUMP_BLOCKER_POOL, NULL, NULL, NULL);
    assert(xfer);
    struct upipe_mgr *worker_mgr = upipe_wlin_mgr_alloc(xfer);
    assert(worker_mgr);
    upipe_mgr_release(xfer);

    avcdec =
        upipe_wlin_alloc(
            worker_mgr,
            uprobe_pfx_alloc_va(uprobe_use(uprobe_main),
                                log_level, "avcdec_w %u", input->id),
            avcdec,
            uprobe_pfx_alloc_va(uprobe_use(uprobe_main),
                                log_level, "avcdec_wx %u", input->id),
            QUEUE_LENGTH, QUEUE_LENGTH);
    assert(avcdec);
    upipe_mgr_release(worker_mgr);

    ubase_assert(upipe_set_output(upipe, avcdec));

    struct uref *flow_def_dup = uref_sibling_alloc_control(flow_def);
    assert(flow_def_dup);
    ubase_assert(uref_flow_set_def(flow_def_dup, UREF_SOUND_FLOW_DEF));
    ubase_assert(uref_sound_flow_set_samples(flow_def_dup, DEFAULT_SAMPLES));
    struct upipe_mgr *upipe_audio_copy_mgr = upipe_audio_copy_mgr_alloc();
    assert(upipe_audio_copy_mgr);
    struct upipe *upipe_audio_copy =
        upipe_flow_chain_output(
            avcdec, upipe_audio_copy_mgr,
            uprobe_pfx_alloc_va(uprobe_use(uprobe_main),
                                UPROBE_LOG_VERBOSE, "frame %u", input->id),
            flow_def_dup);
    uref_free(flow_def_dup);
    upipe_mgr_release(upipe_audio_copy_mgr);
    assert(upipe_audio_copy);

    ubase_assert(upipe_set_output(upipe_audio_copy, input->sound));
    upipe_release(upipe_audio_copy);
    return UBASE_ERR_NONE;
}

static void cmd_none(char *arg)
{
}

static void cmd_connect(char *arg)
{
    while (true) {
        while (isspace(*arg))
            arg++;

        if (!strlen(arg))
            return;

        char *endptr;
        long int in_id = strtol(arg, &endptr, 10);
        if (((in_id == LONG_MIN || in_id == LONG_MAX) && errno == ERANGE) ||
            endptr == arg) {
            uprobe_warn(uprobe_main, NULL, "invalid input");
            return;
        }

        arg = endptr;
        while (isspace(*endptr))
            endptr++;
        if (arg == endptr) {
            uprobe_warn(uprobe_main, NULL, "invalid output");
            return;
        }

        long int out_id = strtol(arg, &endptr, 10);
        if (((out_id == LONG_MIN || out_id == LONG_MAX) && errno == ERANGE) ||
            endptr == arg) {
            uprobe_warn(uprobe_main, NULL, "invalid output");
            return;
        }

        uprobe_notice_va(uprobe_main, NULL, "connect %li -> %li",
                         in_id, out_id);
        struct input *input = input_get(in_id);
        struct output *output = output_get(out_id);
        if (!input && in_id >= 0) {
            uprobe_warn_va(uprobe_main, NULL, "no input for %li", in_id);
            return;
        }
        if (!output) {
            uprobe_warn_va(uprobe_main, NULL, "no output for %li", out_id);
            return;
        }

        ubase_assert(upipe_grid_out_set_input(output->upipe,
                                               input ? input->video : NULL));
        ubase_assert(upipe_grid_out_set_input(output->sound,
                                               input ? input->sound : NULL));
    }
}

static void cmd_list(char *arg)
{
    struct uchain *uchain;

    printf("inputs:\n");
    ulist_foreach(&inputs, uchain) {
        struct input *input = input_from_uchain(uchain);
        printf("\t%u\n", input->id);
    }

    printf("outputs:\n");
    ulist_foreach(&outputs, uchain) {
        struct output *output = output_from_uchain(uchain);
        printf("\t%u", output->id);
        struct upipe *in;
        upipe_grid_out_get_input(output->upipe, &in);
        if (in) {
            struct input *input = input_from_upipe(in);
            printf(" <- %u", input->id);
        }
        printf("\n");
    }
}

static struct input *input_new(const char *uri)
{
    struct input *input = malloc(sizeof (*input));

    input->id = input_id++;
    input->uri = uri;

    struct uprobe *uprobe_dejitter =
        uprobe_dejitter_alloc(uprobe_use(uprobe_main), false, 0);
    assert(uprobe_dejitter);
    uprobe_dejitter_set(uprobe_dejitter, true, 0);
    uprobe_init(&input->uprobe_video, catch_video,
                uprobe_use(uprobe_dejitter));
    uprobe_init(&input->uprobe_audio, catch_audio,
                uprobe_use(uprobe_dejitter));
    uprobe_release(uprobe_dejitter);

    struct upipe_mgr *upipe_rtpsrc_mgr = upipe_rtpsrc_mgr_alloc();
    assert(upipe_rtpsrc_mgr);
    input->source =
        upipe_void_alloc(upipe_rtpsrc_mgr,
                         uprobe_pfx_alloc_va(uprobe_use(uprobe_main),
                                             log_level, "rtpsrc %u",
                                             input->id));
    assert(input->source);
    upipe_mgr_release(upipe_rtpsrc_mgr);
    ubase_assert(upipe_set_uri(input->source, input->uri));
    upipe_attach_uclock(input->source);

    /* ts demux */
    struct upipe_mgr *upipe_ts_demux_mgr = upipe_ts_demux_mgr_alloc();
    struct upipe_mgr *upipe_autof_mgr = upipe_autof_mgr_alloc();
    upipe_ts_demux_mgr_set_autof_mgr(upipe_ts_demux_mgr, upipe_autof_mgr);
    upipe_mgr_release(upipe_autof_mgr);
    struct upipe *ts_demux = upipe_void_alloc_output(input->source,
            upipe_ts_demux_mgr,
            uprobe_pfx_alloc(
                uprobe_selflow_alloc(uprobe_use(uprobe_main),
                    uprobe_selflow_alloc(
                        uprobe_selflow_alloc(
                            uprobe_use(uprobe_dejitter),
                            uprobe_use(&input->uprobe_video),
                            UPROBE_SELFLOW_PIC, "auto"),
                        uprobe_use(&input->uprobe_audio),
                        UPROBE_SELFLOW_SOUND, "auto"),
                    UPROBE_SELFLOW_VOID, "auto"),
                UPROBE_LOG_VERBOSE, "ts demux"));
    upipe_release(ts_demux);
    upipe_mgr_release(upipe_ts_demux_mgr);

    input->video =
        upipe_grid_alloc_input(
            upipe_grid,
            uprobe_pfx_alloc_va(uprobe_use(uprobe_main),
                                UPROBE_LOG_VERBOSE, "in pic %u", input->id));
    assert(input->video);

    input->sound =
        upipe_grid_alloc_input(
            upipe_grid,
            uprobe_pfx_alloc_va(uprobe_use(uprobe_main),
                                UPROBE_LOG_VERBOSE, "in snd %u", input->id));
    assert(input->sound);

    ulist_add(&inputs, input_to_uchain(input));
    return input;
}

static struct output *output_new(const char *uri)
{
    struct output *output = malloc(sizeof (*output));

    output->id = output_id++;
    output->uri = uri;

    /* video */
    output->source =
        upipe_void_alloc_sub(upipe_dup,
                             uprobe_pfx_alloc_va(uprobe_use(uprobe_main),
                                                 log_level, "dup %u",
                                                 output->id));
    assert(output->source);
    output->upipe =
        upipe_grid_alloc_output(
            upipe_grid,
            uprobe_pfx_alloc_va(uprobe_use(uprobe_main),
                                UPROBE_LOG_VERBOSE, "out pic %u",
                                output->id));
    assert(output->upipe);
    ubase_assert(upipe_set_output(output->source, output->upipe));

    struct uref *vblk_flow_def = uref_pic_flow_alloc_def(uref_mgr, 1);
    assert(vblk_flow_def);
    ubase_assert(uref_pic_flow_set_hsize(vblk_flow_def, DEFAULT_HEIGHT));
    ubase_assert(uref_pic_flow_set_vsize(vblk_flow_def, DEFAULT_WIDTH));
    ubase_assert(uref_pic_flow_add_plane(vblk_flow_def, 1, 1, 1, "y8"));
    ubase_assert(uref_pic_flow_add_plane(vblk_flow_def, 2, 2, 1, "u8"));
    ubase_assert(uref_pic_flow_add_plane(vblk_flow_def, 2, 2, 1, "v8"));
    ubase_assert(uref_pic_flow_set_macropixel(vblk_flow_def, 1));
    struct urational fps = { .num = DEFAULT_FPS, .den = 1 };
    ubase_assert(uref_pic_flow_set_fps(vblk_flow_def, fps));
    struct upipe_mgr *upipe_vblk_mgr = upipe_vblk_mgr_alloc();
    assert(upipe_vblk_mgr);
    struct upipe *upipe_vblk =
        upipe_flow_alloc_output(output->upipe, upipe_vblk_mgr,
            uprobe_pfx_alloc_va(uprobe_use(uprobe_main), log_level,
                                "vblk %u", output->id),
            vblk_flow_def);
    upipe_mgr_release(upipe_vblk_mgr);
    assert(upipe_vblk);

    struct upipe_mgr *ffmt_mgr = upipe_ffmt_mgr_alloc();
    struct upipe_mgr *sws_mgr = upipe_sws_mgr_alloc();
    upipe_ffmt_mgr_set_sws_mgr(ffmt_mgr, sws_mgr);
    upipe_mgr_release(sws_mgr);
    struct upipe_mgr *swr_mgr = upipe_swr_mgr_alloc();
    upipe_ffmt_mgr_set_swr_mgr(ffmt_mgr, swr_mgr);
    upipe_mgr_release(swr_mgr);
    struct upipe *ffmt = upipe_flow_alloc(ffmt_mgr,
            uprobe_pfx_alloc(
                uprobe_use(uprobe_main),
                UPROBE_LOG_VERBOSE, "ffmt"),
            vblk_flow_def);
    assert(ffmt != NULL);
    upipe_sws_set_flags(ffmt, SWS_FLAGS);
    upipe_mgr_release(ffmt_mgr);

    struct upipe_mgr *fenc_mgr = upipe_fenc_mgr_alloc();
    struct upipe_mgr *x264_mgr = upipe_x264_mgr_alloc();
    upipe_fenc_mgr_set_x264_mgr(fenc_mgr, x264_mgr);
    upipe_mgr_release(x264_mgr);

    uref_flow_set_def(vblk_flow_def, "block.h264.");
    assert(fenc_mgr);
    struct upipe *venc =
        upipe_flow_alloc(fenc_mgr,
            uprobe_pfx_alloc_va(
                uprobe_use(uprobe_main), log_level,
                "venc %u", output->id),
            vblk_flow_def);
    uref_free(vblk_flow_def);
    upipe_mgr_release(fenc_mgr);
    assert(venc);
    ubase_assert(upipe_x264_set_profile(venc, "baseline"));
    ubase_assert(upipe_x264_set_default_preset(venc,
                                               "ultrafast", NULL));
    ubase_assert(upipe_set_option(venc, "threads", "1"));
    ubase_assert(upipe_set_option(venc, "bitrate", "1536"));
    ubase_assert(upipe_set_option(venc, "vbv-maxrate", "1536"));
    ubase_assert(upipe_set_option(venc, "vbv-bufsize", "1536"));
    ubase_assert(upipe_set_option(venc, "repeat-headers", "1"));
    ubase_assert(upipe_set_option(venc, "nal-hrd", "vbr"));
    ubase_assert(upipe_set_option(venc, "keyint", "25"));

    ubase_assert(upipe_set_output(ffmt, venc));
    upipe_release(venc);

    struct upipe_mgr *enc_xfer_mgr =
        upipe_pthread_xfer_mgr_alloc(
             XFER_QUEUE, XFER_POOL,
             uprobe_use(uprobe_main),
             upump_ev_mgr_alloc_loop,
             UPUMP_POOL, UPUMP_BLOCKER_POOL, NULL, NULL, NULL);
    assert(enc_xfer_mgr);
    struct upipe_mgr *worker_enc_mgr =
        upipe_wlin_mgr_alloc(enc_xfer_mgr);
    assert(worker_enc_mgr);
    upipe_mgr_release(enc_xfer_mgr);
    venc =
        upipe_wlin_alloc(
            worker_enc_mgr,
            uprobe_pfx_alloc_va(uprobe_use(uprobe_main),
                                log_level, "wenc %u", output->id),
            ffmt,
            uprobe_pfx_alloc_va(uprobe_use(uprobe_main),
                                log_level, "wenc_x %u", output->id),
            QUEUE_LENGTH, QUEUE_LENGTH);
    assert(venc);
    ubase_assert(upipe_set_output(upipe_vblk, venc));
    upipe_release(upipe_vblk);
    upipe_mgr_release(worker_enc_mgr);

    /* audio */
    output->sound_src =
        upipe_void_alloc_sub(upipe_dup,
                             uprobe_pfx_alloc_va(uprobe_use(uprobe_main),
                                                 log_level, "dup snd %u",
                                                 output->id));
    assert(output->sound_src);

    output->sound =
        upipe_grid_alloc_output(
            upipe_grid,
            uprobe_pfx_alloc_va(uprobe_use(uprobe_main),
                                log_level, "out snd %u",
                                output->id));
    assert(output->sound);
    ubase_assert(upipe_set_output(output->sound_src, output->sound));

    struct uref *ablk_flow_def =
        uref_sound_flow_alloc_def(uref_mgr, "f32.", 2, 2 * 2);
    assert(ablk_flow_def);
    ubase_assert(uref_sound_flow_add_plane(ablk_flow_def, "l"));
    ubase_assert(uref_sound_flow_add_plane(ablk_flow_def, "r"));
    ubase_assert(uref_sound_flow_set_rate(ablk_flow_def, DEFAULT_RATE));
    ubase_assert(uref_sound_flow_set_samples(ablk_flow_def, DEFAULT_SAMPLES));

    struct upipe_mgr *upipe_ablk_mgr = upipe_ablk_mgr_alloc();
    assert(upipe_ablk_mgr);
    struct upipe *upipe_ablk =
        upipe_flow_alloc_output(output->sound, upipe_ablk_mgr,
            uprobe_pfx_alloc_va(uprobe_use(uprobe_main), log_level,
                                "ablk %u", output->id),
            ablk_flow_def);
    assert(upipe_ablk);
    upipe_mgr_release(upipe_ablk_mgr);

    /* audio encoder */
    ffmt_mgr = upipe_ffmt_mgr_alloc();
    assert(ffmt_mgr);
    swr_mgr = upipe_swr_mgr_alloc();
    assert(swr_mgr);
    upipe_ffmt_mgr_set_swr_mgr(ffmt_mgr, swr_mgr);

    ffmt =
        upipe_flow_alloc(
            ffmt_mgr,
            uprobe_pfx_alloc_va(uprobe_use(uprobe_main),
                                log_level, "ffmt %u", output->id),
            ablk_flow_def);
    assert(ffmt);
    uref_free(ablk_flow_def);
    upipe_mgr_release(ffmt_mgr);
    upipe_sws_set_flags(ffmt, SWS_FLAGS);

    struct uref *sound_flow_def_enc = uref_block_flow_alloc_def(uref_mgr, NULL);
    assert(sound_flow_def_enc);
    uref_avcenc_set_codec_name(sound_flow_def_enc, "mp2");
    fenc_mgr = upipe_fenc_mgr_alloc();
    assert(fenc_mgr);
    struct upipe_mgr *avcenc_mgr = upipe_avcenc_mgr_alloc();
    assert(avcenc_mgr);
    ubase_assert(upipe_fenc_mgr_set_avcenc_mgr(fenc_mgr, avcenc_mgr));
    upipe_mgr_release(avcenc_mgr);
    struct upipe *sound_enc = upipe_flow_alloc(fenc_mgr,
            uprobe_pfx_alloc_va(
                uprobe_use(uprobe_main),
                log_level, "fenc snd %u", output->id),
            sound_flow_def_enc);
    uref_free(sound_flow_def_enc);
    upipe_mgr_release(fenc_mgr);
    assert(sound_enc != NULL);
    ubase_assert(upipe_set_output(ffmt, sound_enc));
    upipe_release(sound_enc);

    struct upipe_mgr *ffmt_xfer_mgr =
        upipe_pthread_xfer_mgr_alloc(
             XFER_QUEUE, XFER_POOL,
             uprobe_use(uprobe_main),
             upump_ev_mgr_alloc_loop,
             UPUMP_POOL, UPUMP_BLOCKER_POOL, NULL, NULL, NULL);
    assert(ffmt_xfer_mgr);
    struct upipe_mgr *ffmt_worker_mgr =
        upipe_wlin_mgr_alloc(ffmt_xfer_mgr);
    assert(ffmt_worker_mgr);
    upipe_mgr_release(ffmt_xfer_mgr);
    ffmt =
        upipe_wlin_alloc(
            ffmt_worker_mgr,
            uprobe_pfx_alloc_va(uprobe_use(uprobe_main),
                                log_level, "wffmt %u", output->id),
            ffmt,
            uprobe_pfx_alloc_va(uprobe_use(uprobe_main),
                                log_level, "wffmt_x %u", output->id),
            QUEUE_LENGTH, QUEUE_LENGTH);
    assert(ffmt);
    ubase_assert(upipe_set_output(upipe_ablk, ffmt));
    upipe_release(upipe_ablk);
    upipe_mgr_release(ffmt_worker_mgr);

    /* ts mux */
    struct upipe_mgr *upipe_ts_mux_mgr = upipe_ts_mux_mgr_alloc();
    assert(upipe_ts_mux_mgr);
    struct upipe *upipe_ts_mux =
        upipe_void_alloc(
            upipe_ts_mux_mgr,
            uprobe_pfx_alloc_va(uprobe_use(uprobe_main),
                                UPROBE_LOG_VERBOSE, "mux %u", output->id));
    assert(upipe_ts_mux);
    upipe_mgr_release(upipe_ts_mux_mgr);

    upipe_ts_mux_set_mode(upipe_ts_mux, UPIPE_TS_MUX_MODE_CAPPED);
    upipe_set_output_size(upipe_ts_mux, mtu);
    upipe_ts_mux_set_padding_octetrate(upipe_ts_mux, PADDING_OCTETRATE);
    upipe_attach_uclock(upipe_ts_mux);
    if (conformance != UPIPE_TS_CONFORMANCE_AUTO)
        upipe_ts_mux_set_conformance(upipe_ts_mux, conformance);
    struct uref *flow_def = uref_alloc_control(uref_mgr);
    uref_flow_set_def(flow_def, "void.");
    upipe_set_flow_def(upipe_ts_mux, flow_def);
    uref_free(flow_def);

    struct upipe_mgr *upipe_rtp_prepend_mgr = upipe_rtp_prepend_mgr_alloc();
    assert(upipe_rtp_prepend_mgr);
    struct upipe *upipe_rtp_prepend =
        upipe_void_alloc_output(
            upipe_ts_mux, upipe_rtp_prepend_mgr,
            uprobe_pfx_alloc_va(
                uprobe_use(uprobe_main), log_level,
                "rtpp %u", output->id));
    upipe_mgr_release(upipe_rtp_prepend_mgr);
    assert(upipe_rtp_prepend);

    struct upipe_mgr *upipe_udpsink_mgr = upipe_udpsink_mgr_alloc();
    assert(upipe_udpsink_mgr);
    struct upipe *upipe_udpsink =
        upipe_void_chain_output(
            upipe_rtp_prepend, upipe_udpsink_mgr,
            uprobe_pfx_alloc_va(
                uprobe_use(uprobe_main), log_level,
                "udp %u", output->id));
    upipe_mgr_release(upipe_udpsink_mgr);
    assert(upipe_udpsink);
    ubase_assert(upipe_attach_uclock(upipe_udpsink));
    ubase_assert(upipe_set_uri(upipe_udpsink, output->uri));
    upipe_release(upipe_udpsink);

    flow_def = uref_alloc_control(uref_mgr);
    uref_flow_set_def(flow_def, "void.");
    upipe_ts_mux = upipe_void_chain_sub(upipe_ts_mux,
            uprobe_pfx_alloc_va(uprobe_use(uprobe_main), UPROBE_LOG_VERBOSE,
                                "mux %u prog", output->id));
    assert(upipe_ts_mux);
    uref_flow_set_id(flow_def, 1);
    uref_ts_flow_set_pid(flow_def, 256);
    upipe_set_flow_def(upipe_ts_mux, flow_def);
    uref_free(flow_def);

    struct upipe_mgr *upipe_setflowdef_mgr = upipe_setflowdef_mgr_alloc();
    assert(upipe_setflowdef_mgr);
    struct upipe *upipe_setflowdef =
        upipe_void_alloc_output(
            venc, upipe_setflowdef_mgr,
            uprobe_pfx_alloc_va(uprobe_use(uprobe_main),
                                UPROBE_LOG_VERBOSE, "setflowdef pic %u",
                                output->id));
    assert(upipe_setflowdef);
    upipe_release(venc);

    struct upipe *sound_setflowdef =
        upipe_void_alloc_output(
            ffmt, upipe_setflowdef_mgr,
            uprobe_pfx_alloc_va(uprobe_use(uprobe_main),
                                log_level, "sfd snd %u", output->id));
    assert(sound_setflowdef);
    upipe_release(ffmt);

    upipe_mgr_release(upipe_setflowdef_mgr);

    struct uref *uref = uref_alloc_control(uref_mgr);
    assert(uref != NULL);
    uref_ts_flow_set_pid(uref, 257);
    upipe_setflowdef_set_dict(upipe_setflowdef, uref);
    uref_ts_flow_set_pid(uref, 258);
    upipe_setflowdef_set_dict(sound_setflowdef, uref);
    uref_free(uref);

    struct upipe *upipe_mux_input =
        upipe_void_alloc_sub(
            upipe_ts_mux,
            uprobe_pfx_alloc_va(uprobe_use(uprobe_main),
                                UPROBE_LOG_VERBOSE,
                                "mux_input pic %u", output->id));
    assert(upipe_mux_input);

    struct upipe *sound_mux_input =
        upipe_void_alloc_sub(
            upipe_ts_mux,
            uprobe_pfx_alloc_va(uprobe_use(uprobe_main),
                                log_level, "mux snd %u", output->id));
    assert(sound_mux_input);

    struct upipe_mgr *sink_xfer_mgr =
        upipe_pthread_xfer_mgr_alloc(
            XFER_QUEUE, XFER_POOL,
            uprobe_use(uprobe_main),
            upump_ev_mgr_alloc_loop,
            UPUMP_POOL, UPUMP_BLOCKER_POOL, NULL, NULL, NULL);
    assert(sink_xfer_mgr);
    struct upipe_mgr *wsink_mgr = upipe_wsink_mgr_alloc(sink_xfer_mgr);
    upipe_mgr_release(sink_xfer_mgr);
    assert(wsink_mgr);

    upipe_ts_mux = upipe_wsink_alloc(wsink_mgr,
        uprobe_pfx_alloc_va(uprobe_use(uprobe_main),
                            log_level, "wsink %u", output->id),
        upipe_ts_mux,
        uprobe_pfx_alloc_va(uprobe_use(uprobe_main),
                            log_level, "wsink_x %u", output->id),
        QUEUE_LENGTH);
    assert(upipe_ts_mux);
    upipe_release(upipe_ts_mux);

    upipe_mux_input = upipe_wsink_chain_output(
            upipe_setflowdef, wsink_mgr,
            uprobe_pfx_alloc_va(
                uprobe_use(uprobe_main),
                log_level, "wsink pic %u", output->id),
            upipe_mux_input,
            uprobe_pfx_alloc_va(
                uprobe_use(uprobe_main),
                log_level, "wsink_x pic %u", output->id),
            QUEUE_LENGTH);
    assert(upipe_mux_input);
    upipe_release(upipe_mux_input);

    sound_mux_input = upipe_wsink_chain_output(
            sound_setflowdef, wsink_mgr,
            uprobe_pfx_alloc_va(
                uprobe_use(uprobe_main),
                log_level, "wsink snd %u", output->id),
            sound_mux_input,
            uprobe_pfx_alloc_va(
                uprobe_use(uprobe_main),
                log_level, "wsink_x snd %u", output->id),
            QUEUE_LENGTH);
    assert(sound_mux_input);
    upipe_release(sound_mux_input);

    upipe_mgr_release(wsink_mgr);

    ulist_add(&outputs, output_to_uchain(output));
    return output;
}

static void cmd_input(char *arg)
{
    struct ustring str =
        ustring_shift_truncate_while(ustring_from_str(arg), " \t\n");
    str.at[str.len] = 0;
    arg = str.at;
    assert(input_new(arg));
}

static void cmd_output(char *arg)
{
    struct ustring str =
        ustring_shift_truncate_while(ustring_from_str(arg), " \t\n");
    str.at[str.len] = '\0';
    arg = str.at;

    assert(output_new(arg));
}

static void stdin_cb(struct upump *upump)
{
    static struct {
        const char *name;
        void (*func)(char *arg);
    } cmds[] = {
        { "", cmd_none },
        { "list", cmd_list },
        { "connect", cmd_connect },
        { "input", cmd_input },
        { "output", cmd_output },
    };
    char buffer[265];
    int rsize = read(STDIN_FILENO, buffer, sizeof (buffer));
    if (rsize < 0) {
        uprobe_err(uprobe_main, NULL, "fail to read from stdin");
        return;
    }

    if (rsize >= sizeof (buffer)) {
        uprobe_warn(uprobe_main, NULL, "command line is too long");
        return;
    }
    buffer[rsize] = 0;

    const struct ustring str =
        ustring_shift_while(ustring_from_str(buffer), " \t\n");
    const char *line = str.at;

    for (size_t i = 0; i < UBASE_ARRAY_SIZE(cmds); i++) {
        if (!strncmp(line, cmds[i].name, strlen(cmds[i].name)) &&
            (isspace(line[strlen(cmds[i].name)]) ||
             line[strlen(cmds[i].name)] == '\n' ||
             line[strlen(cmds[i].name)] == '\0'))
            return cmds[i].func(buffer + strlen(cmds[i].name));
    }

    uprobe_warn_va(uprobe_main, NULL, "unknown command %s", buffer);
}

struct grid_entry {
    enum grid_entry_type { INPUT, OUTPUT } type;
    struct uchain uchain;
    const char *uri;
};

UBASE_FROM_TO(grid_entry, uchain, uchain, uchain);

int main(int argc, char *argv[])
{
    struct uchain grid_entries;
    struct uchain *uchain, *uchain_tmp;
    int c;

    ulist_init(&grid_entries);
    ulist_init(&inputs);
    ulist_init(&outputs);

    while (1) {
        switch ((c = getopt_long(argc, argv, "vhi:o:", options, NULL))) {
            case OPT_INPUT:
            case OPT_OUTPUT: {
                struct grid_entry *e = malloc(sizeof (*e));
                assert(e);
                e->type = c == OPT_INPUT ? INPUT : OUTPUT;
                e->uri = optarg;
                ulist_add(&grid_entries, &e->uchain);
                continue;
            }

            case OPT_VERBOSE:
                switch (log_level) {
                    case UPROBE_LOG_DEBUG:
                        log_level = UPROBE_LOG_VERBOSE;
                        break;

                    default:
                        log_level = UPROBE_LOG_DEBUG;
                        break;
                }
                continue;

            case OPT_HELP:
                usage(argv[0], 0);
                break;

            case -1:
                break;

            default:
                usage(argv[0], -1);
        }
        break;
    }

    struct upump_mgr *upump_mgr =
        upump_ev_mgr_alloc_default(UPUMP_POOL, UPUMP_BLOCKER_POOL);
    assert(upump_mgr);

    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr);

    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr);

    uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    assert(uref_mgr);

    struct uclock *uclock = uclock_std_alloc(0);
    assert(uclock);

    uprobe_main = uprobe_stdio_alloc(NULL, stderr, log_level);
    assert(uprobe_main);
    uprobe_main = uprobe_ubuf_mem_pool_alloc(uprobe_main, umem_mgr,
                                        UBUF_POOL_DEPTH,
                                        UBUF_SHARED_POOL_DEPTH);
    assert(uprobe_main);
    uprobe_main = uprobe_uref_mgr_alloc(uprobe_main, uref_mgr);
    assert(uprobe_main);
    uprobe_main = uprobe_uclock_alloc(uprobe_main, uclock);
    assert(uprobe_main);
    uprobe_main = uprobe_upump_mgr_alloc(uprobe_main, upump_mgr);
    assert(uprobe_main);
    uprobe_main = uprobe_pthread_upump_mgr_alloc(uprobe_main);
    assert(uprobe_main);
    ubase_assert(uprobe_pthread_upump_mgr_set(uprobe_main, upump_mgr));

    /* upipe-av */
    upipe_av_init(false, uprobe_pfx_alloc(uprobe_use(uprobe_main),
                                          UPROBE_LOG_VERBOSE, "av"));

    struct upipe_mgr *upipe_voidsrc_mgr = upipe_voidsrc_mgr_alloc();
    assert(upipe_voidsrc_mgr);
    struct uref *flow_def = uref_void_flow_alloc_def(uref_mgr);
    assert(flow_def);
    ubase_assert(uref_clock_set_duration(flow_def, DEFAULT_DURATION));
    upipe_voidsrc =
        upipe_flow_alloc(upipe_voidsrc_mgr,
                         uprobe_pfx_alloc(uprobe_use(uprobe_main),
                                          log_level, "voidsrc"),
                         flow_def);
    uref_free(flow_def);
    upipe_mgr_release(upipe_voidsrc_mgr);
    assert(upipe_voidsrc);

    struct upipe_mgr *upipe_dup_mgr = upipe_dup_mgr_alloc();
    assert(upipe_dup_mgr);
    upipe_mgr_release(upipe_dup_mgr);

    upipe_dup =
        upipe_void_alloc_output(upipe_voidsrc, upipe_dup_mgr,
                                uprobe_pfx_alloc(uprobe_use(uprobe_main),
                                                 log_level, "dup"));
    assert(upipe_dup);


    struct upipe_mgr *upipe_grid_mgr = upipe_grid_mgr_alloc();
    assert(upipe_grid_mgr);
    upipe_grid =
        upipe_void_alloc(upipe_grid_mgr,
                         uprobe_pfx_alloc(uprobe_use(uprobe_main),
                                          UPROBE_LOG_VERBOSE, "grid"));
    upipe_mgr_release(upipe_grid_mgr);
    assert(upipe_grid);

    ulist_delete_foreach(&grid_entries, uchain, uchain_tmp) {
        struct grid_entry *e = grid_entry_from_uchain(uchain);
        if (e->type == INPUT)
            assert(input_new(e->uri));
        else if (e->type == OUTPUT)
            assert(output_new(e->uri));
        else
            abort();

        ulist_delete(uchain);
        free(e);
    }

    struct upump *sigint_pump =
        upump_alloc_signal(upump_mgr, sig_cb,
                           (void *)SIGINT, NULL, SIGINT);
    upump_set_status(sigint_pump, false);
    upump_start(sigint_pump);
    struct upump *sigterm_pump =
        upump_alloc_signal(upump_mgr, sig_cb,
                           (void *)SIGTERM, NULL, SIGTERM);
    upump_set_status(sigterm_pump, false);
    upump_start(sigterm_pump);
    struct upump *stdin_pump =
        upump_alloc_fd_read(upump_mgr, stdin_cb, NULL, NULL, STDIN_FILENO);
    upump_set_status(stdin_pump, false);
    upump_start(stdin_pump);

    upump_mgr_run(upump_mgr, NULL);

    upump_free(sigint_pump);
    upump_free(sigterm_pump);
    upump_free(stdin_pump);

    ulist_delete_foreach(&outputs, uchain, uchain_tmp) {
        struct output *output = output_from_uchain(uchain);
        upipe_release(output->source);
        upipe_release(output->sound_src);
        upipe_release(output->upipe);
        upipe_release(output->sound);
        ulist_delete(uchain);
        free(output);
    }
    ulist_delete_foreach(&inputs, uchain, uchain_tmp) {
        struct input *input = input_from_uchain(uchain);
        upipe_release(input->source);
        upipe_release(input->video);
        upipe_release(input->sound);
        uprobe_clean(&input->uprobe_video);
        uprobe_clean(&input->uprobe_audio);
        ulist_delete(input_to_uchain(input));
        free(input);
    }

    upipe_release(upipe_grid);
    upipe_av_clean();
    uprobe_release(uprobe_main);
    uclock_release(uclock);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    upump_mgr_release(upump_mgr);
    return 0;
}
