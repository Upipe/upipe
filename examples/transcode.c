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

/** @file
 * @short Simple upipe/avformat/avcodec remuxer/transcoder
 */

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
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_block_flow.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/ubuf_pic.h>
#include <upipe/ubuf_pic_mem.h>
#include <upipe/uref_dump.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe/upipe.h>
#include <upipe-av/uref_av_flow.h>
#include <upipe-av/upipe_av.h>
#include <upipe-av/upipe_avformat_source.h>
#include <upipe-av/upipe_avformat_sink.h>
#include <upipe-av/upipe_avcodec_decode.h>
#include <upipe-av/upipe_avcodec_encode.h>
#include <upipe-swresample/upipe_swr.h>
#include <upipe-swscale/upipe_sws.h>
#include <upipe-filters/upipe_filter_format.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>

#include <ev.h>

#define UDICT_POOL_DEPTH    10
#define UREF_POOL_DEPTH     10
#define UBUF_POOL_DEPTH     10
#define UBUF_PREPEND        0
#define UBUF_APPEND         0
#define UBUF_ALIGN          32
#define UBUF_ALIGN_OFFSET   0
#define UPUMP_POOL          5
#define UPUMP_BLOCKER_POOL  5
#define READ_SIZE           4096
#define UPROBE_LOG_LEVEL UPROBE_LOG_NOTICE

struct es_conf {
    struct uchain uchain;
    uint64_t id;
    const char *codec;
    struct udict *options;
};

enum uprobe_log_level loglevel = UPROBE_LOG_LEVEL;
struct uref_mgr *uref_mgr;

struct upipe_mgr *upipe_avcdec_mgr;
struct upipe_mgr *upipe_avcenc_mgr;
struct upipe_mgr *upipe_ffmt_mgr;

static struct uprobe *logger;
static struct upipe *avfsrc;
static struct upipe *avfsink;
struct uchain eslist;

static void usage(const char *argv0) {
    fprintf(stderr, "Usage: %s [-d] [-m <mime>] [-f <format>] [-p <id> -c <codec> [-o <option=value>] ...] ... <source file> <sink file>\n", argv0);
    fprintf(stderr, "   -f: output format name\n");
    fprintf(stderr, "   -m: output mime type\n");
    fprintf(stderr, "   -p: add stream with id\n");
    fprintf(stderr, "   -c: stream encoder\n");
    fprintf(stderr, "   -o: encoder option (key=value)\n");
    exit(EXIT_FAILURE);
}

/* exit on error */
static inline void check_exit(bool cond, const char *str)
{
    if (cond) return;

    fprintf(stderr, "%s", str);
    exit(EXIT_FAILURE);
}

/* return es configuration from uchain */
static inline struct es_conf *es_conf_from_uchain(struct uchain *uchain)
{
    return container_of(uchain, struct es_conf, uchain);
}

/* return es configuration from id */
struct es_conf *es_conf_from_id(struct uchain *list, uint64_t id)
{
    struct uchain *uchain;
    ulist_foreach (list, uchain) {
        struct es_conf *conf = es_conf_from_uchain(uchain);
        if (conf->id == id) {
            return conf;
        }
    }
    return NULL;
}

/* iterate in es configuration options */
bool es_conf_iterate(struct es_conf *conf, const char **key,
                     const char **value, enum udict_type *type)
{
    if (!ubase_check(udict_iterate(conf->options, key, type)) ||
        *type == UDICT_TYPE_END) {
        return false;
    }
    return ubase_check(udict_get_string(conf->options, value, *type, *key));
}

/* allocate es configuration */
struct es_conf *es_conf_alloc(struct udict_mgr *mgr,
                              uint64_t id, struct uchain *list)
{
    struct es_conf *conf = malloc(sizeof(struct es_conf));
    if (unlikely(conf == NULL))
        return NULL;
    memset(conf, 0, sizeof(struct es_conf));
    uchain_init(&conf->uchain);
    if (list) {
        ulist_add(list, &conf->uchain);
    }

    conf->options = udict_alloc(mgr, 0);
    conf->id = id;
    return conf;
}

/* add option to es configuration */
bool es_conf_add_option(struct es_conf *conf, const char *key,
                                              const char *value)
{
    assert(conf);
    return ubase_check(udict_set_string(conf->options,
                       value, UDICT_TYPE_STRING, key));
}

/* add option to es configuration */
static inline bool es_conf_add_option_parse(struct es_conf *conf, char *str)
{
    const char *key = str;
    char *value = strchr(str, '=');
    if (!value) {
        value = str + strlen(str);
    } else {
        *value = '\0';
        value++;
    }
    return es_conf_add_option(conf, key, value);
}

/* free configuration list */
static void es_conf_clean(struct uchain *list)
{
    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach (list, uchain, uchain_tmp) {
        ulist_delete(uchain);
        struct es_conf *conf = es_conf_from_uchain(uchain);
        udict_free(conf->options);
        free(conf);
    }
}

/* main uprobe */
static int catch(struct uprobe *uprobe, struct upipe *upipe,
                 int event, va_list args)
{
    switch (event) {
        default:
            break;

        case UPROBE_SOURCE_END:
            upipe_release(upipe);
            return UBASE_ERR_NONE;
    }
    return uprobe_throw_next(uprobe, upipe, event, args);
}

/* catch demux events */
static int catch_demux(struct uprobe *uprobe, struct upipe *upipe,
                       int event, va_list args)
{
    if (event != UPROBE_SPLIT_UPDATE) {
        return uprobe_throw_next(uprobe, upipe, event, args);
    }

    /* iterate through flow list */
    struct uref *flow_def = NULL;
    while (ubase_check(upipe_split_iterate(upipe, &flow_def)) &&
           flow_def != NULL) {
        const char *def = "(none)";
        uref_flow_get_def(flow_def, &def);
        if (ubase_ncmp(def, "block.")) {
            upipe_warn_va(upipe, "flow def %s is not supported", def);
            break;
        }

        /* flow */
        uint64_t id = 0;
        uref_flow_get_id(flow_def, &id);
        upipe_notice_va(upipe, "New flow %"PRIu64" (%s)", id, def);
        uref_dump(flow_def, upipe->uprobe);

        /* demux output */
        struct upipe *avfsrc_output =
            upipe_flow_alloc_sub(avfsrc,
                    uprobe_pfx_alloc_va(uprobe_use(logger),
                        loglevel, "src %"PRIu64, id), flow_def);
        assert(avfsrc_output != NULL);

        struct upipe *incoming = avfsrc_output;

        /* transcode stream if specified by user */
        struct es_conf *conf = es_conf_from_id(&eslist, id);
        if (conf && conf->codec) {
            /* decoder */
            struct upipe *decoder = upipe_void_alloc_output(avfsrc_output,
                upipe_avcdec_mgr,
                uprobe_pfx_alloc_va(uprobe_use(logger),
                                    loglevel, "dec %"PRIu64, id));
            upipe_release(decoder);
            incoming = decoder;
            
            /* stream type */
            char *ffmt_def = NULL;
            if (strstr(def, ".sound.")) {
                ffmt_def = "sound.";
            } else if (strstr(def, "pic.")) {
                ffmt_def = "pic.";
            } else {
                upipe_err_va(upipe, "stream type unsupported %"PRIu64" (%s)",
                             id, def);
                exit(EXIT_FAILURE);
            }
            /* format conversion */
            struct uref *ffmt_flow = uref_alloc_control(uref_mgr);
            uref_flow_set_def(ffmt_flow, ffmt_def);
            struct upipe *ffmt = upipe_flow_alloc(upipe_ffmt_mgr,
                uprobe_pfx_alloc_va(uprobe_use(logger),
                    UPROBE_LOG_VERBOSE, "ffmt %"PRIu64, id), ffmt_flow);
            assert(ffmt);
            uref_free(ffmt_flow);
            upipe_set_output(incoming, ffmt);
            upipe_release(ffmt);
            incoming = ffmt;

            /* encoder */
            struct uref *flow = uref_block_flow_alloc_def(uref_mgr, "");
            uref_avcenc_set_codec_name(flow, conf->codec);
            struct upipe *encoder = upipe_flow_alloc_output(incoming,
                upipe_avcenc_mgr,
                uprobe_pfx_alloc_va(uprobe_use(logger),
                                    loglevel, "enc %"PRIu64, id), flow);
            upipe_release(encoder);
            uref_free(flow);
            if (strstr(def, ".pic.")) {
                upipe_set_option(encoder, "threads", "0");
            }

            /* encoder options */
            const char *key = NULL, *value = NULL;
            enum udict_type type = UDICT_TYPE_END;
            while (es_conf_iterate(conf, &key, &value, &type)) {
                upipe_dbg_va(encoder, "%s option: %s=%s",
                        conf->codec, key, value);
                if (!ubase_check(upipe_set_option(encoder, key, value)))
                    upipe_warn_va(encoder, "option %s unknown", key);
            }

            incoming = encoder;
        }

        /* mux input */
        struct upipe *sink =
            upipe_void_alloc_output_sub(incoming, avfsink,
                    uprobe_pfx_alloc_va(uprobe_use(logger), loglevel,
                        "sink %"PRIu64, id));
        if (unlikely(!sink)) {
            upipe_err_va(upipe,
                    "could not allocate mux input for %"PRIu64" (%s)",
                    id, def);
            upipe_release(avfsrc_output);
            break;
        }
        upipe_release(sink);
    }

    return true;
}

int main(int argc, char *argv[])
{
    int opt;
    const char *src_url, *sink_url;
    const char *mime = NULL, *format = NULL;
    struct es_conf *es_cur = NULL;

    /* upipe env (udict, umem) */
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);

    /* int es list */
    ulist_init(&eslist);

    /* parse options */
    while ((opt = getopt(argc, argv, "dm:f:p:c:o:")) != -1) {
        switch(opt) {
            case 'd':
                if (loglevel > 0) loglevel--;
                break;
            case 'm':
                mime = optarg;
                break;
            case 'f':
                format = optarg;
                break;

            case 'p': {
                uint64_t pid = strtoull(optarg, NULL, 0);
                es_cur = es_conf_alloc(udict_mgr, pid, &eslist);
                break;
            }
            case 'c': {
                check_exit(es_cur, "no stream id specified\n");
                es_cur->codec = optarg;
                break;
            }
            case 'o': {
                check_exit(es_cur, "no stream id specified\n");
                es_conf_add_option_parse(es_cur, optarg);
                break;
            }

            default:
                usage(argv[0]);
                break;
        }
    }
    if (argc - optind < 2) {
        usage(argv[0]);
    }
    src_url = argv[optind++];
    sink_url = argv[optind++];

    /* upipe env */
    struct ev_loop *loop = ev_default_loop(0);
    uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);

    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop, UPUMP_POOL,
                                                     UPUMP_BLOCKER_POOL);
    struct uclock *uclock = uclock_std_alloc(0);
    struct uprobe uprobe, uprobe_demux_s;
    uprobe_init(&uprobe, catch, NULL);
    logger = uprobe_stdio_alloc(uprobe_use(&uprobe), stdout, loglevel);
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
    uprobe_init(&uprobe_demux_s, catch_demux, uprobe_use(logger));

    upipe_av_init(false, uprobe_use(logger));

    /* pipe managers */
    struct upipe_mgr *upipe_avfsink_mgr = upipe_avfsink_mgr_alloc();
    struct upipe_mgr *upipe_avfsrc_mgr = upipe_avfsrc_mgr_alloc();
    struct upipe_mgr *upipe_swr_mgr = upipe_swr_mgr_alloc();
    struct upipe_mgr *upipe_sws_mgr = upipe_sws_mgr_alloc();
    upipe_avcdec_mgr = upipe_avcdec_mgr_alloc();
    upipe_avcenc_mgr = upipe_avcenc_mgr_alloc();
    upipe_ffmt_mgr = upipe_ffmt_mgr_alloc();
    upipe_ffmt_mgr_set_sws_mgr(upipe_ffmt_mgr, upipe_sws_mgr);
    upipe_ffmt_mgr_set_swr_mgr(upipe_ffmt_mgr, upipe_swr_mgr);

    /* avformat sink */
    avfsink = upipe_void_alloc(upipe_avfsink_mgr,
        uprobe_pfx_alloc(uprobe_use(logger), loglevel, "avfsink"));
    upipe_attach_uclock(avfsink);

    upipe_avfsink_set_mime(avfsink, mime);
    upipe_avfsink_set_format(avfsink, format);
    if (unlikely(!ubase_check(upipe_set_uri(avfsink, sink_url)))) {
        fprintf(stderr, "error: could not open dest uri\n");
        exit(EXIT_FAILURE);
    }

    /* avformat source */
    avfsrc = upipe_void_alloc(upipe_avfsrc_mgr,
        uprobe_pfx_alloc(uprobe_use(&uprobe_demux_s), loglevel, "avfsrc"));
    upipe_attach_uclock(avfsrc);
    upipe_set_uri(avfsrc, src_url);

    /* fire */
    ev_loop(loop, 0);

    upipe_mgr_release(upipe_avfsrc_mgr); /* nop */

    upipe_release(avfsink);
    upipe_mgr_release(upipe_avfsink_mgr); /* nop */

    upipe_mgr_release(upipe_ffmt_mgr);
    upipe_mgr_release(upipe_sws_mgr); /* nop */
    upipe_mgr_release(upipe_swr_mgr); /* nop */

    upipe_av_clean();

    es_conf_clean(&eslist);

    upump_mgr_release(upump_mgr);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uclock_release(uclock);
    uprobe_release(logger);
    uprobe_clean(&uprobe);
    uprobe_clean(&uprobe_demux_s);

    ev_default_destroy();
    return 0;
}
