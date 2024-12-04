/*
 * Copyright (C) 2022 EasyTools
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

#include <upipe/ubase.h>
#include <upipe/uuri.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict_inline.h>
#include <upipe/uref_std.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_dump.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_upump_mgr.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/uprobe_select_flows.h>
#include <upipe/uprobe_uclock.h>
#include <upipe/uprobe_dejitter.h>
#include <upipe/uref_block.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_pic.h>
#include <upipe/uclock.h>
#include <upipe/uclock_std.h>
#include <upipe-modules/upipe_file_source.h>
#include <upipe-modules/upipe_rtp_source.h>
#include <upipe-modules/upipe_udp_source.h>
#include <upipe-modules/upipe_http_source.h>
#include <upipe-modules/upipe_file_sink.h>
#include <upipe-modules/upipe_probe_uref.h>
#include <upipe-modules/upipe_null.h>
#include <upipe-modules/upipe_setflowdef.h>
#include <upipe-ts/upipe_ts_sync.h>
#include <upipe-ts/upipe_ts_split.h>
#include <upipe-ts/upipe_ts_decaps.h>
#include <upipe-ts/upipe_ts_pes_decaps.h>
#include <upipe-framers/upipe_auto_framer.h>
#include <upipe-framers/upipe_mpga_framer.h>
#include <upipe-framers/upipe_h264_framer.h>
#include <upipe-framers/upipe_h265_framer.h>
#include <upipe-framers/upipe_dvbsub_framer.h>
#include <upipe-filters/upipe_filter_decode.h>
#include <upipe-av/upipe_av.h>
#include <upipe-av/upipe_avcodec_decode.h>
#include <upump-ev/upump_ev.h>

#include <stdlib.h>
#include <getopt.h>

#define USTRING_STR(USTRING, STRING, ...)                           \
    do {                                                            \
        char STRING[USTRING.len + 1];                               \
        ustring_cpy(USTRING, STRING, sizeof(STRING));               \
        __VA_ARGS__;                                                \
    } while (0)

#define UURI_AUTHORITY_STR(UURI, STRING, ...)                               \
    do {                                                                    \
        size_t len = 0;                                                     \
        uuri_authority_len(&UURI.authority, &len);                          \
        char STRING[len + 1];                                               \
        uuri_authority_to_buffer(&UURI.authority, STRING, sizeof (STRING)); \
        __VA_ARGS__;                                                        \
    } while (0)

static const char *source = NULL;
static const char *framer = "(none)";
static enum uprobe_log_level uprobe_log_level = UPROBE_LOG_DEBUG;
static struct uref_mgr *uref_mgr = NULL;
static struct uclock *uclock = NULL;
static struct uprobe *main_probe = NULL;
static unsigned additional_framer = 0;
static bool decode = false;
static bool dump_date = false;
static bool dump_size = false;
static bool dump_random = false;
static bool dump_hex = false;
static int dump_hex_size = -1;

enum {
    OPT_VERBOSE,
    OPT_QUIET,
    OPT_HELP,
    OPT_TS,
    OPT_FRAMER,
    OPT_REFRAME,
    OPT_DECODE,
    OPT_PID_FILTER_OUT,
    OPT_PID,
    OPT_DATE,
    OPT_SIZE,
    OPT_RANDOM,
    OPT_HEX,
    OPT_HEX_SIZE,
};

static struct option options[] = {
    { "verbose", no_argument, NULL, OPT_VERBOSE },
    { "quiet", no_argument, NULL, OPT_QUIET },
    { "help", no_argument, NULL, OPT_HELP },
    { "ts", no_argument, NULL, OPT_TS },
    { "framer", required_argument, NULL, OPT_FRAMER },
    { "reframe", no_argument, NULL, OPT_REFRAME },
    { "decode", no_argument, NULL, OPT_DECODE },
    { "pid-filter-out", no_argument, NULL, OPT_PID_FILTER_OUT },
    { "pid", required_argument, NULL, OPT_PID },
    { "date", no_argument, NULL, OPT_DATE },
    { "size", no_argument, NULL, OPT_SIZE },
    { "random", no_argument, NULL, OPT_RANDOM },
    { "hex", no_argument, NULL, OPT_HEX},
    { "hex-size", required_argument, NULL, OPT_HEX_SIZE },
    { NULL, 0, NULL, 0 },
};

struct pid {
    struct uchain uchain;
    uint64_t pid;
};

UBASE_FROM_TO(pid, uchain, uchain, uchain);

static struct uchain pids;

enum pid_selection {
    PID_FILTER_IN,
    PID_FILTER_OUT,
};

static enum pid_selection pid_selection = PID_FILTER_IN;

struct es {
    struct uchain uchain;
    uint64_t id;
    struct uref *flow_def;
    bool marked;
    struct upipe *source;
};

UBASE_FROM_TO(es, uchain, uchain, uchain);

static struct uchain es_list;

static struct pid *pid_find(uint64_t id)
{
    struct uchain *uchain;
    ulist_foreach(&pids, uchain) {
        struct pid *pid = pid_from_uchain(uchain);
        if (pid->pid == id)
            return pid;
    }
    return NULL;
}

static void pid_add(uint64_t id)
{
    struct pid *pid = pid_find(id);
    if (!pid) {
        pid = malloc(sizeof (*pid));
        assert(pid);
        pid->pid = id;
        ulist_add(&pids, &pid->uchain);
    }
}

static void pid_del(struct pid *pid)
{
    assert(pid);
    ulist_delete(&pid->uchain);
    free(pid);
}

static int catch_uref(struct uprobe *uprobe, struct upipe *upipe,
                      int event, va_list args)
{
    if (event != UPROBE_PROBE_UREF ||
        ubase_get_signature(args) != UPIPE_PROBE_UREF_SIGNATURE)
        return uprobe_throw_next(uprobe, upipe, event, args);

    UBASE_SIGNATURE_CHECK(args, UPIPE_PROBE_UREF_SIGNATURE);
    struct uref *uref = va_arg(args, struct uref *);

    if (dump_random) {
        if (ubase_check(uref_flow_get_random(uref)))
            upipe_notice_va(upipe, "random");
    }

    if (dump_date)
        uref_dump_clock_dbg(uref, upipe->uprobe);

    if (dump_size) {
        size_t size = 0, vsize = 0;
        uint8_t sample_size = 0;
        if (ubase_check(uref_block_size(uref, &size)))
            upipe_dbg_va(upipe, "block size %zu", size);
        else if (ubase_check(uref_sound_size(uref, &size, &sample_size)))
            upipe_dbg_va(upipe, "sound size %zu (sample %u)", size, sample_size);
        else if (ubase_check(uref_pic_size(uref, &size, &vsize, &sample_size)))
            upipe_dbg_va(upipe, "pic size %zux%zu (sample %u)",
                         size, vsize, sample_size);
    }

    if (dump_hex) {
        size_t block_size = 0;
        if (ubase_check(uref_block_size(uref, &block_size))) {
            const uint8_t *buffer = NULL;
            int offset = 0;

            printf("hexdump uref %p (block_size %zu)\n", uref, block_size);

            if (dump_hex_size > 0 && dump_hex_size < block_size)
                block_size = dump_hex_size;

            while (block_size) {
                int size = block_size;
                if (!ubase_check(uref_block_read(uref, offset, &size, &buffer)) || !size) {
                    upipe_err(upipe, "fail to read buffer");
                    break;
                }

                for (int i = offset; i < offset + size; i++)
                {
                    if (!(i % 16)) {
                        if (i)
                            printf("\n");
                        printf("%08x:", i);
                    }
                    if (!(i % 2 ))
                        printf(" ");
                    printf("%02x", buffer[i - offset]);
                }
                block_size -= size;
                offset += size;
            }
            if (offset)
                printf("\n");
        } else {
            upipe_err(upipe, "block size failed");
        }
    }

    return UBASE_ERR_NONE;
}

static int catch_es(struct uprobe *uprobe, struct upipe *upipe,
                    int event, va_list args)
{
    switch (event) {
        case UPROBE_SOURCE_END:
            return UBASE_ERR_NONE;

        case UPROBE_NEED_OUTPUT: {
            struct uref *flow_def = va_arg(args, struct uref *);
            uref_dump_notice(flow_def, uprobe);

            upipe_use(upipe);

            struct upipe_mgr *upipe_probe_uref_mgr =
                upipe_probe_uref_mgr_alloc();
            upipe = upipe_void_chain_output(
                upipe, upipe_probe_uref_mgr,
                uprobe_pfx_alloc(
                    uprobe_alloc(catch_uref, uprobe_use(uprobe)),
                    UPROBE_LOG_VERBOSE, "probe"));
            assert(upipe);

            struct upipe_mgr *upipe_autof_mgr = upipe_autof_mgr_alloc();
            for (unsigned i = 0; i < additional_framer; i++) {
                upipe = upipe_void_chain_output(
                    upipe, upipe_autof_mgr,
                    uprobe_pfx_alloc_va(
                        uprobe_use(uprobe),
                        UPROBE_LOG_VERBOSE, "framer %u", i));
                assert(upipe);

                upipe = upipe_void_chain_output(
                    upipe, upipe_probe_uref_mgr,
                    uprobe_pfx_alloc_va(
                        uprobe_alloc(catch_uref, uprobe_use(uprobe)),
                        UPROBE_LOG_VERBOSE, "probe %u", i));
                assert(upipe);
            }
            upipe_mgr_release(upipe_autof_mgr);

            if (decode) {
                struct upipe_mgr *upipe_fdec_mgr = upipe_fdec_mgr_alloc();
                struct upipe_mgr *upipe_avcdec_mgr = upipe_avcdec_mgr_alloc();
                upipe_fdec_mgr_set_avcdec_mgr(upipe_fdec_mgr, upipe_avcdec_mgr);
                upipe_mgr_release(upipe_avcdec_mgr);

                upipe = upipe_void_chain_output(
                    upipe, upipe_fdec_mgr,
                    uprobe_pfx_alloc(
                        uprobe_use(uprobe),
                        UPROBE_LOG_VERBOSE, "fdec"));
                assert(upipe);
                upipe_mgr_release(upipe_fdec_mgr);

                if (!strcmp(framer, "video")) {
                    upipe_set_option(upipe, "threads", "auto");
                    upipe_set_option(upipe, "ec", "1");
                }

                upipe = upipe_void_chain_output(
                    upipe, upipe_probe_uref_mgr,
                    uprobe_pfx_alloc(
                        uprobe_alloc(catch_uref, uprobe_use(uprobe)),
                        UPROBE_LOG_VERBOSE, "probe dec"));
                assert(upipe);
            }

            struct upipe_mgr *upipe_null_mgr = upipe_null_mgr_alloc();
            assert(upipe_null_mgr);
            upipe = upipe_void_chain_output(
                upipe, upipe_null_mgr,
                uprobe_pfx_alloc(
                    uprobe_use(uprobe),
                    UPROBE_LOG_VERBOSE, "null"));
            upipe_mgr_release(upipe_null_mgr);

            upipe_mgr_release(upipe_probe_uref_mgr);
            upipe_release(upipe);
            return UBASE_ERR_NONE;
        }
        default:
            break;
    }
    return uprobe_throw_next(uprobe, upipe, event, args);
}

static struct es *es_add(uint64_t id, struct uref *flow_def,
                         struct upipe *upipe)
{
    assert(id && flow_def);

    struct es *es = malloc(sizeof (*es));
    assert(es);
    es->id = id;
    es->flow_def = uref_dup(flow_def);
    assert(es->flow_def);
    es->marked = false;
    ulist_add(&es_list, &es->uchain);
    bool selected = pid_find(id) != NULL;
    if ((selected && pid_selection == PID_FILTER_IN) ||
        (!selected && pid_selection == PID_FILTER_OUT)) {
        es->source = upipe_flow_alloc_sub(
            upipe,
            uprobe_alloc(
                catch_es,
                uprobe_pfx_alloc_va(uprobe_use(upipe->uprobe),
                                    UPROBE_LOG_VERBOSE, "es %"PRIu64, id)),
            flow_def);
        assert(es->source);
    } else {
        es->source = NULL;
    }

    return es;
}

static void es_del(struct es *es)
{
    assert(es);
    if (es->source)
        upipe_release(es->source);
    uref_free(es->flow_def);
    ulist_delete(&es->uchain);
    free(es);
}

static struct es *es_find(uint64_t id)
{
    struct uchain *uchain;
    ulist_foreach(&es_list, uchain) {
        struct es *es = es_from_uchain(uchain);
        if (es->id == id)
            return es;
    }
    return NULL;
}

static void es_mark(void)
{
    struct uchain *uchain;
    ulist_foreach(&es_list, uchain) {
        struct es *es = es_from_uchain(uchain);
        es->marked = true;
    }
}

static int catch_prog(struct uprobe *uprobe, struct upipe *upipe,
                      int event, va_list args)
{
    if (event != UPROBE_SPLIT_UPDATE)
        return uprobe_throw_next(uprobe, upipe, event, args);

    es_mark();

    upipe_split_foreach(upipe, flow_def) {
        uint64_t id = 0;
        ubase_assert(uref_flow_get_id(flow_def, &id));
        assert(id);

        const char *def = NULL;
        ubase_assert(uref_flow_get_def(flow_def, &def));
        assert(def);

        const char *prefix = "";
        struct es *es = es_find(id);
        if (!es) {
            es = es_add(id, flow_def, upipe);
            prefix = "created";
            uref_dump_dbg(flow_def, uprobe);
        } else if (udict_cmp(flow_def->udict, es->flow_def->udict)) {
            prefix = "updated";
        } else {
            prefix = "untouched";
        }
        es->marked = false;
        uprobe_notice_va(uprobe, upipe, "%s es %"PRIu64" - %s",
                         prefix, id, def);
    }

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach(&es_list, uchain, uchain_tmp) {
        struct es *es = es_from_uchain(uchain);
        if (!es->marked)
            continue;

        const char *def = NULL;
        ubase_assert(uref_flow_get_def(es->flow_def, &def));
        assert(def);
        uprobe_notice_va(uprobe, upipe, "deleted es %"PRIu64 " - %s",
                         es->id, def);
        es_del(es);
    }

    return UBASE_ERR_NONE;
}

static struct upipe *upipe_source_alloc(const char *uri, struct uprobe *uprobe)
{
    struct uuri uuri;
    if (unlikely(!ubase_check(uuri_from_str(&uuri, uri)))) {
        /* URI must be a file path */
        uuri = uuri_null();
        uuri.scheme = ustring_from_str("file");
        uuri.path = ustring_from_str(uri);
    }

    struct upipe *upipe_src = NULL;

    if (ustring_match_str(uuri.scheme, "file")) {
        struct upipe_mgr *upipe_fsrc_mgr = upipe_fsrc_mgr_alloc();
        assert(upipe_fsrc_mgr);
        upipe_src = upipe_void_alloc(
            upipe_fsrc_mgr,
            uprobe_pfx_alloc(
                uprobe_use(uprobe),
                UPROBE_LOG_VERBOSE, "fsrc"));
        assert(upipe_src);
        upipe_mgr_release(upipe_fsrc_mgr);
        USTRING_STR(uuri.path, path, upipe_set_uri(upipe_src, path));
    }
    else if (ustring_match_str(uuri.scheme, "rtp")) {
        struct uref *flow_def = uref_block_flow_alloc_def(uref_mgr, "rtp.");
        assert(flow_def);
        struct upipe_mgr *upipe_rtpsrc_mgr = upipe_rtpsrc_mgr_alloc();
        upipe_src = upipe_flow_alloc(
            upipe_rtpsrc_mgr,
            uprobe_pfx_alloc(
                uprobe_use(uprobe),
                UPROBE_LOG_VERBOSE, "rtp"),
            flow_def);
        upipe_mgr_release(upipe_rtpsrc_mgr);
        uref_free(flow_def);
        assert(upipe_src);
        UURI_AUTHORITY_STR(uuri, path, upipe_set_uri(upipe_src, path));

    }
    else if (ustring_match_str(uuri.scheme, "udp")) {
        struct upipe_mgr *upipe_udpsrc_mgr =upipe_udpsrc_mgr_alloc();
        upipe_src = upipe_void_alloc(
            upipe_udpsrc_mgr,
            uprobe_pfx_alloc(
                uprobe_use(uprobe),
                UPROBE_LOG_VERBOSE, "udp"));
        upipe_mgr_release(upipe_udpsrc_mgr);
        assert(upipe_src);
        UURI_AUTHORITY_STR(uuri, path, upipe_set_uri(upipe_src, path));

    }
    else if (ustring_match_str(uuri.scheme, "http") ||
             ustring_match_str(uuri.scheme, "https")) {
        struct upipe_mgr *upipe_http_src_mgr = upipe_http_src_mgr_alloc();
        upipe_src = upipe_void_alloc(
            upipe_http_src_mgr,
            uprobe_pfx_alloc(
                uprobe_use(uprobe),
                UPROBE_LOG_VERBOSE, "http"));
        upipe_mgr_release(upipe_http_src_mgr);
        assert(upipe_src);
        upipe_set_uri(upipe_src, uri);
    }

    upipe_attach_uclock(upipe_src);
    return upipe_src;
}

static void usage(const char *name)
{
    fprintf(stderr, "usage: %s [options] <source>\n", name);
    fprintf(stderr, "options:\n");
    for (unsigned i = 0; options[i].name; i++) {
        fprintf(stderr, "  -%s", options[i].name);
        if (options[i].has_arg == required_argument)
            fprintf(stderr, " <value>");
        else if (options[i].has_arg == optional_argument)
            fprintf(stderr, " [<value>]");
        fprintf(stderr, "\n");
    }
}

int main(int argc, char *argv[])
{
    struct uchain *uchain, *uchain_tmp;

    ulist_init(&pids);
    ulist_init(&es_list);

    bool ts = false;
    int opt;
    while ((opt = getopt_long_only(argc, argv, "", options, NULL)) != -1) {
        switch (opt) {
            case OPT_QUIET:
                if (uprobe_log_level < UPROBE_LOG_ERROR)
                    uprobe_log_level++;
                break;

            case OPT_VERBOSE:
                if (uprobe_log_level > 0)
                    uprobe_log_level--;
                break;

            case OPT_HELP:
                usage(argv[0]);
                exit(0);
                break;

            case OPT_TS:
                ts = true;
                break;

            case OPT_FRAMER:
                framer = optarg;
                break;

            case OPT_REFRAME:
                additional_framer++;
                break;

            case OPT_DECODE:
                decode = true;
                break;

            case OPT_PID_FILTER_OUT:
                pid_selection = PID_FILTER_OUT;
                break;

            case OPT_PID:
                pid_add(strtoull(optarg, NULL, 0));
                break;

            case OPT_DATE:
                dump_date = true;
                break;

            case OPT_SIZE:
                dump_size = true;
                break;

            case OPT_RANDOM:
                dump_random = true;
                break;

            case OPT_HEX:
                dump_hex = true;
                break;

            case OPT_HEX_SIZE:
                dump_hex_size = atoi(optarg);
                break;

            default:
                fprintf(stderr, "unknown option -%c\n", opt);
                break;
        }
    }

    if (optind >= argc) {
        usage(argv[0]);
        exit(0);
    }
    source = argv[optind];

    /* create managers */
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc_default(0, 0);
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(0, umem_mgr, -1, -1);
    uref_mgr = uref_std_mgr_alloc(0, udict_mgr, 0);
    uclock = uclock_std_alloc(0);
    udict_mgr_release(udict_mgr);

    /* create root probe */
    struct uprobe *uprobe = NULL;
    uprobe = uprobe_stdio_alloc(NULL, stderr, uprobe_log_level);
    assert(uprobe != NULL);
    uprobe = uprobe_uref_mgr_alloc(uprobe, uref_mgr);
    assert(uprobe != NULL);
    uprobe = uprobe_upump_mgr_alloc(uprobe, upump_mgr);
    assert(uprobe != NULL);
    uprobe = uprobe_ubuf_mem_alloc(uprobe, umem_mgr, 0, 0);
    assert(uprobe != NULL);
    uprobe = uprobe_uclock_alloc(uprobe, uclock);
    assert(uprobe != NULL);
    main_probe = uprobe;

    if (decode) {
        upipe_av_init(true, uprobe_pfx_alloc(uprobe_use(main_probe),
                                             UPROBE_LOG_VERBOSE, "av"));
    }

    /* create source */
    struct upipe *upipe_src = upipe_source_alloc(source, uprobe);

    if (ts) {
        struct upipe_mgr *upipe_ts_demux_mgr = upipe_ts_demux_mgr_alloc();
        struct upipe_mgr *upipe_autof_mgr = upipe_autof_mgr_alloc();
        upipe_ts_demux_mgr_set_autof_mgr(upipe_ts_demux_mgr, upipe_autof_mgr);
        upipe_mgr_release(upipe_autof_mgr);

        struct uprobe *uprobe_dejitter =
            uprobe_dejitter_alloc(uprobe_use(uprobe), true, UCLOCK_FREQ);
        struct upipe *demux = upipe_void_alloc_output(
            upipe_src,
            upipe_ts_demux_mgr,
            uprobe_pfx_alloc(
                uprobe_selflow_alloc(
                    uprobe_use(uprobe_dejitter),
                    uprobe_alloc(catch_prog, uprobe_use(uprobe_dejitter)),
                    UPROBE_SELFLOW_VOID, "auto"),
                UPROBE_LOG_VERBOSE, "ts demux"));
        assert(demux != NULL);
        upipe_mgr_release(upipe_ts_demux_mgr);
        uprobe_release(uprobe_dejitter);
        upipe_ts_demux_set_conformance(demux, UPIPE_TS_CONFORMANCE_AUTO);
        upipe_release(demux);
    }
    else {
        struct upipe_mgr *upipe_framer_mgr = NULL;
        if (!strcmp(framer, "mpga")) {
            upipe_framer_mgr = upipe_mpgaf_mgr_alloc();
        }
        else if (!strcmp(framer, "h264")) {
            upipe_framer_mgr = upipe_h264f_mgr_alloc();
        }
        else if (!strcmp(framer, "h265")) {
            upipe_framer_mgr = upipe_h265f_mgr_alloc();
        }
        else if (!strcmp(framer, "dvbsub")) {
            upipe_framer_mgr = upipe_dvbsubf_mgr_alloc();
        }
        else {
            uprobe_err_va(main_probe, NULL, "unsupported framer %s", framer);
            exit(-1);
        }

        assert(upipe_framer_mgr);

        struct upipe *upipe_framer = upipe_void_alloc(
            upipe_framer_mgr,
            uprobe_pfx_alloc(
                uprobe_use(uprobe),
                UPROBE_LOG_VERBOSE, framer));
        assert(upipe_framer);
        upipe_mgr_release(upipe_framer_mgr);
        upipe_set_output(upipe_src, upipe_framer);

        struct upipe_mgr *upipe_null_mgr = upipe_null_mgr_alloc();
        assert(upipe_null_mgr);
        struct upipe *upipe_null = upipe_void_alloc(
            upipe_null_mgr,
            uprobe_pfx_alloc(
                uprobe_use(uprobe),
                UPROBE_LOG_VERBOSE, "null"));
        upipe_mgr_release(upipe_null_mgr);
        upipe_set_output(upipe_framer, upipe_null);
        upipe_release(upipe_framer);
        upipe_release(upipe_null);
    }

    /* main loop */
    upump_mgr_run(upump_mgr, NULL);

    ulist_delete_foreach(&es_list, uchain, uchain_tmp) {
        struct es *es = es_from_uchain(uchain);
        es_del(es);
    }

    if (decode) {
        upipe_av_clean();
    }

    /* release probes, pipes and managers */
    uprobe_release(uprobe);
    upipe_release(upipe_src);
    uclock_release(uclock);
    uref_mgr_release(uref_mgr);
    umem_mgr_release(umem_mgr);
    upump_mgr_release(upump_mgr);

    ulist_delete_foreach(&pids, uchain, uchain_tmp) {
        struct pid *pid = pid_from_uchain(uchain);
        pid_del(pid);
    }

    return 0;
}
