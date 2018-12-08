/*
 * Copyright (C) 2016-2017 OpenHeadend S.A.R.L
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

/** @file
 * @short Simple mp3 to udp/rtp streamer
 */

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_uclock.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_upump_mgr.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/uclock.h>
#include <upipe/uclock_std.h>
#include <upipe/umem_pool.h>
#include <upipe/udict_inline.h>
#include <upipe/uref_std.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/upipe.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe-modules/upipe_file_source.h>
#include <upipe-av/upipe_av.h>
#include <upipe-av/upipe_avcodec_decode.h>
#include <upipe-swresample/upipe_swr.h>
#include <upipe-modules/upipe_trickplay.h>
#include <upipe-modules/upipe_probe_uref.h>
#include <upipe-modules/upipe_rtp_prepend.h>
#include <upipe-modules/upipe_rtp_pcm_pack.h>
#include <upipe-modules/upipe_udp_sink.h>
#include <upipe-modules/upipe_nodemux.h>
#include <upipe-framers/upipe_mpga_framer.h>

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/param.h>
#include <stdarg.h>
#include <dirent.h>
#include <errno.h>

#include <ev.h>

#define UPROBE_LOG_LEVEL UPROBE_LOG_NOTICE
#define UMEM_POOL 512
#define UDICT_POOL_DEPTH 500
#define UREF_POOL_DEPTH 500
#define UBUF_POOL_DEPTH 3000
#define UBUF_SHARED_POOL_DEPTH 50
#define UPUMP_POOL 10
#define UPUMP_BLOCKER_POOL 10

enum uprobe_log_level loglevel = UPROBE_LOG_LEVEL;
const char *suri = NULL;

struct uprobe *mainprobe;
struct upipe *source = NULL;

/* generic signal handler */
static void sighandler(struct upump *upump)
{
    int signal = (int)upump_get_opaque(upump, ptrdiff_t);
    uprobe_err_va(mainprobe, NULL, "signal %s received, exiting",
                  strsignal(signal));
    upipe_release(source);
    source = NULL;
}

/* source events */
static int catch_source(struct uprobe *uprobe, struct upipe *upipe,
                        int event, va_list args)
{
    if (event != UPROBE_SOURCE_END) {
        return uprobe_throw_next(uprobe, upipe, event, args);
    }

    /* loop through input */
    upipe_set_uri(upipe, suri);
    return UBASE_ERR_NONE;
}

/* uref event */
static int catch_probe_uref(struct uprobe *uprobe, struct upipe *upipe,
                            int event, va_list args)
{
    if (event != UPROBE_PROBE_UREF) {
        return uprobe_throw_next(uprobe, upipe, event, args);
    }

    va_arg(args, unsigned int); /* skip signature */
    struct uref *uref = va_arg(args, struct uref *);

    uref_clock_set_dts_pts_delay(uref, 0);
    uref_clock_set_cr_dts_delay(uref, 0);

    return UBASE_ERR_NONE;
}

static void usage(const char *argv0)
{
    fprintf(stderr, "Usage: %s [-p] [-d] [-q] [-u] <source file> <destip:destport>\n", argv0);
    fprintf(stderr, "   -u: UDP only\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    const char *duri = NULL;
    bool rtp = true;
    bool pcm = false;
    int opt;

    /* parse options */
    while ((opt = getopt(argc, argv, "pdqu")) != -1) {
        switch(opt) {
            case 'd':
                if (loglevel > 0) loglevel--;
                break;
            case 'q':
                if (loglevel < UPROBE_LOG_ERROR) loglevel++;
                break;
            case 'u':
                rtp = false;
                break;
            case 'p':
                pcm = true;
                break;

            default:
                usage(argv[0]);
                break;
        }
    }
    if (argc - optind < 2) {
        usage(argv[0]);
    }
    suri = argv[optind++];
    duri = argv[optind++];

    /* event-loop management */
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc_default(UPUMP_POOL,
            UPUMP_BLOCKER_POOL);

    /* mem management */
    struct umem_mgr *umem_mgr = umem_pool_mgr_alloc_simple(UMEM_POOL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    /* monotonic clock */
    struct uclock *uclock = uclock_std_alloc(0);

    /* global probes */
    mainprobe = uprobe_stdio_alloc(NULL, stdout, loglevel);
    assert(mainprobe != NULL);
    mainprobe = uprobe_uref_mgr_alloc(mainprobe, uref_mgr);
    assert(mainprobe != NULL);
    mainprobe = uprobe_upump_mgr_alloc(mainprobe, upump_mgr);
    assert(mainprobe != NULL);
    mainprobe = uprobe_uclock_alloc(mainprobe, uclock);
    assert(mainprobe != NULL);
    mainprobe = uprobe_ubuf_mem_alloc(mainprobe, umem_mgr, UBUF_POOL_DEPTH,
                                      UBUF_POOL_DEPTH);
    assert(mainprobe != NULL);

    /* specific probes */
    struct uprobe uprobe_probe_uref_s, uprobe_source_s;
    uprobe_init(&uprobe_probe_uref_s, catch_probe_uref, uprobe_use(mainprobe));
    uprobe_init(&uprobe_source_s, catch_source, uprobe_use(mainprobe));

    /* pipe management */
    struct upipe_mgr *fsrc_mgr = upipe_fsrc_mgr_alloc();
    struct upipe_mgr *nodemux_mgr = upipe_nodemux_mgr_alloc();
    struct upipe_mgr *probe_uref_mgr = upipe_probe_uref_mgr_alloc();
    struct upipe_mgr *mpgaf_mgr = upipe_mpgaf_mgr_alloc();
    struct upipe_mgr *trickp_mgr = upipe_trickp_mgr_alloc();
    struct upipe_mgr *rtp_mgr = upipe_rtp_prepend_mgr_alloc();
    struct upipe_mgr *udp_mgr = upipe_udpsink_mgr_alloc();

    /* file source */
    source = upipe_void_alloc(fsrc_mgr,
        uprobe_pfx_alloc(uprobe_use(&uprobe_source_s), UPROBE_LOG_VERBOSE,
                         "fsrc"));
    assert(source != NULL);
    ubase_assert(upipe_set_uri(source, suri));

    /* fix first pts before framer */
    struct upipe *upipe = upipe_void_alloc_output(source, nodemux_mgr,
        uprobe_pfx_alloc(uprobe_use(mainprobe), UPROBE_LOG_VERBOSE, "nodemux"));
    assert(upipe != NULL);

    /* mpga framer */
    upipe = upipe_void_chain_output(upipe, mpgaf_mgr,
        uprobe_pfx_alloc(uprobe_use(mainprobe), UPROBE_LOG_VERBOSE, "mpga"));
    assert(upipe != NULL);

    /* trick play */
    struct upipe *trickp = upipe_void_alloc(trickp_mgr,
        uprobe_pfx_alloc(uprobe_use(mainprobe), UPROBE_LOG_VERBOSE, "trickp"));
    assert(trickp != NULL);

    upipe = upipe_void_chain_output_sub(upipe, trickp,
        uprobe_pfx_alloc(uprobe_use(mainprobe), UPROBE_LOG_VERBOSE, "trickpa"));
    upipe_release(trickp);
    assert(upipe != NULL);

    /* set pts to cr_sys */
    upipe = upipe_void_chain_output(upipe, probe_uref_mgr,
        uprobe_pfx_alloc(uprobe_use(&uprobe_probe_uref_s), UPROBE_LOG_VERBOSE,
                         "probe_uref"));
    assert(upipe != NULL);

    if (pcm) {
        /* avcodec */
        if (unlikely(!upipe_av_init(false,
                        uprobe_pfx_alloc(uprobe_use(mainprobe),
                            UPROBE_LOG_VERBOSE, "av")))) {
            exit(EXIT_FAILURE);
        }

        /* decode */
        struct upipe_mgr *avcdec_mgr = upipe_avcdec_mgr_alloc();
        upipe = upipe_void_chain_output(upipe, avcdec_mgr,
                uprobe_pfx_alloc(uprobe_use(mainprobe),
                    UPROBE_LOG_VERBOSE, "avcdec audio"));
        upipe_mgr_release(avcdec_mgr);

        /* convert to interleaved s32, TODO: non-stereo */
        struct uref *uref = uref_sound_flow_alloc_def(uref_mgr,
            "s32.", 2, 8
        );
        uref_sound_flow_set_planes(uref, 1);

        /* swr */
        struct upipe_mgr *swr_mgr = upipe_swr_mgr_alloc();
        upipe = upipe_flow_chain_output(upipe, swr_mgr,
                uprobe_pfx_alloc(uprobe_use(mainprobe),
                    UPROBE_LOG_VERBOSE, "swr"), uref);
        upipe_mgr_release(swr_mgr);
        uref_free(uref);

        /* pcm pack */
        struct upipe_mgr *pack_mgr = upipe_rtp_pcm_pack_mgr_alloc();
        upipe = upipe_void_chain_output(upipe, pack_mgr,
            uprobe_pfx_alloc(uprobe_use(mainprobe), UPROBE_LOG_VERBOSE, "pack"));
        assert(upipe != NULL);
        upipe_mgr_release(pack_mgr);

    }

    if (rtp) {
        /* rtp header */
        upipe = upipe_void_chain_output(upipe, rtp_mgr,
            uprobe_pfx_alloc(uprobe_use(mainprobe), UPROBE_LOG_VERBOSE, "rtp"));
        assert(upipe != NULL);
        upipe_rtp_prepend_set_type(upipe,
                pcm ? 96 /* dynamic */ : 14 /* mpeg audio */);
    }

    /* udp sink */
    upipe = upipe_void_chain_output(upipe, udp_mgr,
        uprobe_pfx_alloc(uprobe_use(mainprobe), UPROBE_LOG_VERBOSE, "udp"));
    upipe_attach_uclock(upipe);
    ubase_assert(upipe_set_uri(upipe, duri));
    upipe_release(upipe);

    upipe_mgr_release(fsrc_mgr);
    upipe_mgr_release(nodemux_mgr);
    upipe_mgr_release(probe_uref_mgr);
    upipe_mgr_release(mpgaf_mgr);
    upipe_mgr_release(trickp_mgr);
    upipe_mgr_release(rtp_mgr);
    upipe_mgr_release(udp_mgr);

    /* sighandlers */
    struct upump *sigint_pump = upump_alloc_signal(upump_mgr, sighandler,
            (void *)SIGINT, NULL, SIGINT);
    upump_set_status(sigint_pump, false);
    upump_start(sigint_pump);

    /* fire loop */
    upump_mgr_run(upump_mgr, NULL);

    /* clean */
    if (source != NULL)
        upipe_release(source);

    upump_stop(sigint_pump);
    upump_free(sigint_pump);
    uprobe_clean(&uprobe_probe_uref_s);
    uprobe_clean(&uprobe_source_s);
    uprobe_release(mainprobe);
    uclock_release(uclock);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    upump_mgr_release(upump_mgr);

    if (pcm)
        upipe_av_clean();

    return 0;
}
