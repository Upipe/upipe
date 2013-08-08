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
 *
 */

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_log.h>
#include <upipe/uclock.h>
#include <upipe/uclock_std.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/upipe.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe-modules/upipe_file_source.h>
#include <upipe-modules/upipe_trickplay.h>
#include <upipe-framers/upipe_mpga_framer.h>
#include <upipe-av/upipe_av.h>
#include <upipe-av/upipe_avcodec_decode.h>
#include <upipe-alsa/upipe_alsa_sink.h>

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <ev.h>

#define UPROBE_LOG_LEVEL UPROBE_LOG_NOTICE
#define UDICT_POOL_DEPTH 10
#define UREF_POOL_DEPTH 10
#define UBUF_POOL_DEPTH 10
#define UPUMP_POOL 10
#define UPUMP_BLOCKER_POOL 10
#define UBUF_PREPEND        0
#define UBUF_APPEND         0
#define UBUF_ALIGN          32
#define UBUF_ALIGN_OFFSET   0
#define QUEUE_LENGTH        10

const char *device = "default";
enum uprobe_log_level loglevel = UPROBE_LOG_LEVEL;
struct uprobe *logger;
struct uprobe uprobe_avcdec;

struct ubuf_mgr *block_mgr;
struct upump_mgr *upump_mgr;
struct uclock *uclock;
bool inited = false;

static bool catch_src(struct uprobe *uprobe, struct upipe *upipe,
                      enum uprobe_event event, va_list args)
{
    switch (event) {
        case UPROBE_SOURCE_END:
            upipe_release(upipe);
            return true;
        default:
            return false;
    }
}

static bool catch_mpgaf(struct uprobe *uprobe, struct upipe *upipe,
                        enum uprobe_event event, va_list args)
{
    struct uref *flow_def;
    const char *def;
    if (!uprobe_plumber(uprobe, upipe, event, args, &flow_def, &def))
        return false;

    if (inited)
        return true;
    inited = true;
    if (ubase_ncmp(def, "block.mp2.sound.") &&
        ubase_ncmp(def, "block.mp3.sound.") &&
        ubase_ncmp(def, "block.aac.sound.")) {
        upipe_warn_va(upipe, "flow def %s is not supported", def);
        return false;
    }

    /* avcdec */
    struct upipe_mgr *upipe_avcdec_mgr = upipe_avcdec_mgr_alloc();
    struct upipe *avcdec = upipe_flow_alloc(upipe_avcdec_mgr,
            uprobe_pfx_adhoc_alloc_va(&uprobe_avcdec, loglevel, "avcdec"),
            flow_def);
    upipe_set_ubuf_mgr(avcdec, block_mgr);
    upipe_set_output(upipe, avcdec);
    upipe_release(avcdec);
    return true;
}

static bool catch_avcdec(struct uprobe *uprobe, struct upipe *upipe,
                         enum uprobe_event event, va_list args)
{
    struct uref *flow_def;
    const char *def;
    if (!uprobe_plumber(uprobe, upipe, event, args, &flow_def, &def))
        return false;

    /* trick play */
    struct upipe_mgr *upipe_trickp_mgr = upipe_trickp_mgr_alloc();
    struct upipe *trickp = upipe_void_alloc(upipe_trickp_mgr,
            uprobe_pfx_adhoc_alloc_va(logger, loglevel, "trickp"));
    upipe_mgr_release(upipe_trickp_mgr);
    upipe_set_uclock(trickp, uclock);
    struct upipe *trickp_audio = upipe_flow_alloc_sub(trickp,
            uprobe_pfx_adhoc_alloc_va(logger, loglevel, "trickp audio"),
            flow_def);
    upipe_release(trickp);
    upipe_set_output(upipe, trickp_audio);

    /* alsa sink */
    struct upipe_mgr *upipe_alsink_mgr = upipe_alsink_mgr_alloc();
    struct upipe *alsink = upipe_flow_alloc(upipe_alsink_mgr,
                    uprobe_pfx_adhoc_alloc(logger, loglevel, "alsink"),
                    flow_def);
    upipe_mgr_release(upipe_alsink_mgr);
    upipe_set_upump_mgr(alsink, upump_mgr);
    upipe_set_uclock(alsink, uclock);
    if (!upipe_set_uri(alsink, device))
        exit(1);
    upipe_set_output(trickp_audio, alsink);
    upipe_release(trickp_audio);
    upipe_release(alsink);
    return true;
}

void usage(const char *argv0)
{
    printf("Usage: %s [-d] <file> [<alsa device>]\n", argv0);
    exit(-1);
}

int main(int argc, char **argv)
{
    int opt;
    // parse options
    while ((opt = getopt(argc, argv, "d")) != -1) {
        switch (opt) {
            case 'd':
                loglevel = UPROBE_LOG_DEBUG;
                break;
            default:
                break;
        }
    }
    if (optind >= argc)
        usage(argv[0]);

    const char *uri = argv[optind++];
    if (optind < argc)
        device = argv[optind++];

    /* upipe env */
    struct ev_loop *loop = ev_default_loop(0);
    upump_mgr = upump_ev_mgr_alloc(loop, UPUMP_POOL, UPUMP_BLOCKER_POOL);
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH,
                                                   udict_mgr, 0);
    block_mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH,
                                         UBUF_POOL_DEPTH, umem_mgr,
                                         -1, -1, -1, 0);

    /* log probes */
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(NULL, stdout, loglevel);
    logger = uprobe_log_alloc(uprobe_stdio, loglevel);

    /* source probe */
    struct uprobe uprobe_src;
    uprobe_init(&uprobe_src, catch_src, logger);

    /* framer probe */
    struct uprobe uprobe_mpgaf;
    uprobe_init(&uprobe_mpgaf, catch_mpgaf, logger);

    /* avcdec probe */
    uprobe_init(&uprobe_avcdec, catch_avcdec, logger);

    /* uclock */
    uclock = uclock_std_alloc(0);

    /* upipe-av */
    upipe_av_init(true);

    /* file source */
    struct upipe_mgr *upipe_fsrc_mgr = upipe_fsrc_mgr_alloc();
    if (unlikely(upipe_fsrc_mgr == NULL))
        exit(1);

    struct upipe *upipe_src = upipe_void_alloc(upipe_fsrc_mgr,
                uprobe_pfx_adhoc_alloc(&uprobe_src, loglevel, "fsrc"));
    upipe_mgr_release(upipe_fsrc_mgr);
    if (unlikely(upipe_src == NULL))
        exit(1);
    upipe_set_uref_mgr(upipe_src, uref_mgr);
    upipe_set_ubuf_mgr(upipe_src, block_mgr);
    upipe_set_upump_mgr(upipe_src, upump_mgr);
    upipe_set_uclock(upipe_src, uclock);
    if (!upipe_set_uri(upipe_src, uri))
        return false;

    struct uref *flow_def;
    if (!upipe_get_flow_def(upipe_src, &flow_def))
        exit(1);
    struct uref *flow_def_dup = uref_dup(flow_def);
    uref_flow_set_def(flow_def_dup, "block.mp2.sound.");

    /* mpga framer */
    struct upipe_mgr *upipe_mpgaf_mgr = upipe_mpgaf_mgr_alloc();
    if (unlikely(upipe_mpgaf_mgr == NULL))
        exit(1);

    struct upipe *upipe_mpgaf = upipe_flow_alloc(upipe_mpgaf_mgr,
                uprobe_pfx_adhoc_alloc(&uprobe_mpgaf, loglevel, "mpgaf"),
                flow_def_dup);
    uref_free(flow_def_dup);
    upipe_mgr_release(upipe_mpgaf_mgr);
    if (unlikely(upipe_mpgaf == NULL))
        exit(1);
    upipe_set_output(upipe_src, upipe_mpgaf);
    upipe_release(upipe_mpgaf);

    /* fire decode engine and main loop */
    ev_loop(loop, 0);

    upipe_av_clean();
    uclock_release(uclock);
    
    upump_mgr_release(upump_mgr);
    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(block_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_log_free(logger);
    uprobe_stdio_free(uprobe_stdio);

    ev_default_destroy();

    return 0;
}
