/*
 * Copyright (C) 2013-2014 OpenHeadend S.A.R.L.
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
#include <upipe/uref_flow.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_dump.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/upipe.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe-modules/upipe_file_source.h>
#include <upipe-modules/upipe_nodemux.h>
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

#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG
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

struct upump_mgr *upump_mgr;
struct uclock *uclock;
bool inited = false;

static int catch_src(struct uprobe *uprobe, struct upipe *upipe,
                     int event, va_list args)
{
    switch (event) {
        case UPROBE_SOURCE_END:
            upipe_release(upipe);
            return UBASE_ERR_NONE;
        default:
            return uprobe_throw_next(uprobe, upipe, event, args);
    }
}

static int catch_mpgaf(struct uprobe *uprobe, struct upipe *upipe,
                       int event, va_list args)
{
    struct uref *flow_def;
    const char *def;
    if (!uprobe_plumber(event, args, &flow_def, &def))
        return uprobe_throw_next(uprobe, upipe, event, args);

    upipe_dbg_va(upipe, "framer flow def:");
    uref_dump(flow_def, upipe->uprobe);

    if (inited)
        return UBASE_ERR_NONE;
    inited = true;
    if (ubase_ncmp(def, "block.mp2.sound.") &&
        ubase_ncmp(def, "block.mp3.sound.") &&
        ubase_ncmp(def, "block.aac.sound.")) {
        upipe_warn_va(upipe, "flow def %s is not supported", def);
        return UBASE_ERR_UNHANDLED;
    }

    /* avcdec */
    struct upipe_mgr *upipe_avcdec_mgr = upipe_avcdec_mgr_alloc();
    struct upipe *avcdec = upipe_void_alloc_output(upipe, upipe_avcdec_mgr,
            uprobe_pfx_alloc_va(uprobe_use(&uprobe_avcdec),
                                loglevel, "avcdec"));
    upipe_release(avcdec);
    return UBASE_ERR_NONE;
}

static int catch_avcdec(struct uprobe *uprobe, struct upipe *upipe,
                        int event, va_list args)
{
    struct uref *flow_def;
    const char *def;
    if (!uprobe_plumber(event, args, &flow_def, &def))
        return uprobe_throw_next(uprobe, upipe, event, args);

    /* trick play */
    struct upipe_mgr *upipe_trickp_mgr = upipe_trickp_mgr_alloc();
    struct upipe *trickp = upipe_void_alloc(upipe_trickp_mgr,
            uprobe_pfx_alloc_va(uprobe_use(logger), loglevel, "trickp"));
    upipe_mgr_release(upipe_trickp_mgr);
    upipe_attach_uclock(trickp);
    struct upipe *trickp_audio = upipe_void_alloc_output_sub(upipe, trickp,
            uprobe_pfx_alloc_va(uprobe_use(logger),
                                loglevel, "trickp audio"));
    upipe_release(trickp);

    /* alsa sink */
    struct upipe_mgr *upipe_alsink_mgr = upipe_alsink_mgr_alloc();
    struct upipe *alsink = upipe_void_alloc_output(trickp_audio,
                    upipe_alsink_mgr,
                    uprobe_pfx_alloc(uprobe_use(logger), loglevel, "alsink"));
    if (alsink == NULL) {
        assert(0);
    }
    upipe_mgr_release(upipe_alsink_mgr);
    upipe_attach_uclock(alsink);
    if (!ubase_check(upipe_set_uri(alsink, device))) {
        assert(0);
    }
    upipe_release(trickp_audio);
    upipe_release(alsink);
    return true;
}

static void usage(const char *argv0)
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

    /* uclock */
    uclock = uclock_std_alloc(0);

    /* log probes */
    logger = uprobe_stdio_alloc(NULL, stdout, loglevel);
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

    /* source probe */
    struct uprobe uprobe_src;
    uprobe_init(&uprobe_src, catch_src, uprobe_use(logger));

    /* framer probe */
    struct uprobe uprobe_mpgaf;
    uprobe_init(&uprobe_mpgaf, catch_mpgaf, uprobe_use(logger));

    /* avcdec probe */
    uprobe_init(&uprobe_avcdec, catch_avcdec, uprobe_use(logger));

    /* upipe-av */
    upipe_av_init(true, uprobe_use(logger));

    /* file source */
    struct upipe_mgr *upipe_fsrc_mgr = upipe_fsrc_mgr_alloc();
    if (unlikely(upipe_fsrc_mgr == NULL))
        exit(1);

    struct upipe *upipe_src = upipe_void_alloc(upipe_fsrc_mgr,
                uprobe_pfx_alloc(uprobe_use(&uprobe_src),
                                 loglevel, "fsrc"));
    upipe_mgr_release(upipe_fsrc_mgr);
    if (unlikely(upipe_src == NULL))
        exit(1);
    upipe_attach_uclock(upipe_src);
    if (!ubase_check(upipe_set_uri(upipe_src, uri)))
        return false;

    /* no demux */
    struct upipe_mgr *upipe_nodemux_mgr = upipe_nodemux_mgr_alloc();
    if (unlikely(upipe_nodemux_mgr == NULL))
        exit(1);

    struct upipe *upipe_nodemux = upipe_void_alloc_output(upipe_src,
                upipe_nodemux_mgr,
                uprobe_pfx_alloc(uprobe_use(logger),
                                 loglevel, "nodemux"));
    if (upipe_nodemux == NULL)
        exit(1);
    upipe_mgr_release(upipe_nodemux_mgr);

    /* mpga framer */
    struct upipe_mgr *upipe_mpgaf_mgr = upipe_mpgaf_mgr_alloc();
    if (unlikely(upipe_mpgaf_mgr == NULL))
        exit(1);

    struct upipe *upipe_mpgaf = upipe_void_alloc_output(upipe_nodemux,
                upipe_mpgaf_mgr,
                uprobe_pfx_alloc(uprobe_use(&uprobe_mpgaf),
                                 loglevel, "mpgaf"));
    if (upipe_mpgaf == NULL)
        exit(1);
    upipe_mgr_release(upipe_mpgaf_mgr);
    if (unlikely(upipe_mpgaf == NULL))
        exit(1);
    upipe_release(upipe_nodemux);
    upipe_release(upipe_mpgaf);

    /* fire decode engine and main loop */
    ev_loop(loop, 0);

    upipe_av_clean();
    uclock_release(uclock);
    
    upump_mgr_release(upump_mgr);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe_src);
    uprobe_clean(&uprobe_mpgaf);
    uprobe_clean(&uprobe_avcdec);

    ev_default_destroy();

    return 0;
}
