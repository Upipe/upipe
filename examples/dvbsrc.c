/*
 * Copyright (C) 2017 Open Broadcast Systems Ltd.
 *
 * Authors: Rafaël Carré
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

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_ubuf_mem_pool.h>
#include <upipe/uprobe_uclock.h>
#include <upipe/uprobe_dejitter.h>
#include <upipe/uprobe_upump_mgr.h>
#include <upipe/umem.h>
#include <upipe/umem_pool.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uclock.h>
#include <upipe/uclock_std.h>
#include <upipe/upipe.h>

#include <upump-ev/upump_ev.h>

#include <upipe-dvb/upipe_dvbsrc.h>

#include <upipe-modules/upipe_udp_sink.h>

#include <linux/dvb/frontend.h>

#define UMEM_POOL               512
#define UDICT_POOL_DEPTH        500
#define UREF_POOL_DEPTH         500
#define UBUF_POOL_DEPTH         3000
#define UBUF_SHARED_POOL_DEPTH  50
#define UPUMP_POOL              10
#define UPUMP_BLOCKER_POOL      10

static void usage(const char *arg0)
{
    printf("Usage: %s [-q] [-d] [-2] -l EXTENDED -f 12187500 -s 27500000 -i 3_4 -p H 0:0 127.0.0.1:1234\n", arg0);
}

static struct upipe *dvbsrc;

static void stats_timer (struct upump *upump)
{
    struct dtv_property prop[2];
    struct dtv_properties props;
    props.num = sizeof(prop) / sizeof(*prop);
    props.props = prop;

    prop[0].cmd = DTV_STAT_SIGNAL_STRENGTH;
    prop[1].cmd = DTV_STAT_CNR;

    unsigned int signal_db = 0;
    float signal = 0., snr = 0.;
    unsigned int status = 0;

    if (!ubase_check(upipe_dvbsrc_get_frontend_status(dvbsrc, &status, &props))) {
        upipe_err(dvbsrc, "Could not read stats");
        status = 0;
    } else {
        for (int i = 0; i < props.num; i++) {
            struct dtv_fe_stats *fe_st = &prop[i].u.st;
            if (fe_st->len != 1)
                continue;
            struct dtv_stats *st = &fe_st->stat[0];
            switch(prop[i].cmd) {
                case DTV_STAT_CNR:
                    snr = (float)st->svalue / 1000.;
                    break;
                case DTV_STAT_SIGNAL_STRENGTH:
                    if (st->scale == FE_SCALE_RELATIVE) {
                        signal = 100. * st->uvalue / 65535.;
                        signal_db = 0;
                    } else {
                        signal = (float)st->svalue / 1000.;
                        signal_db = 1;
                    }
                    break;
                default:
                    break;
            }
        }
    }

    upipe_notice_va(dvbsrc, "[%s%s], Signal %.2f%s, S/N %.2fdB",
            (status & FE_HAS_LOCK) ? "LOCK" : "",
            (status & FE_HAS_SYNC) ? "|SYNC" : "",
            signal, signal_db ? "dbM" : "%",
            snr);
}

int main(int argc, char **argv)
{
    const char *freq = NULL;
    const char *srate = NULL;
    const char *fec = NULL;
    const char *lnb = NULL;
    const char *pol = NULL;
    const char *sys = "DVBS";

    enum uprobe_log_level loglevel = UPROBE_LOG_WARNING;
    int opt;
    while ((opt = getopt(argc, argv, "qd2f:s:i:p:l:")) != -1) {
        switch (opt) {
        case 'q':
            loglevel++;
            break;
        case 'd':
            loglevel--;
            break;
        case '2':
            sys = "DVBS2";
            break;
        case 'f':
            freq = optarg;
            break;
        case 's':
            srate = optarg;
            break;
        case 'i':
            fec = optarg;
            break;
        case 'p':
            pol = optarg;
            break;
        case 'l':
            lnb = optarg;
            break;
        default:
            usage(argv[0]);
            return 1;
        }
    }

    if (optind + 1 >= argc) {
        usage(argv[0]);
        return 1;
    }

    const char *src = argv[optind++];
    const char *dst = argv[optind++];

    /* structures managers */
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc_default(UPUMP_POOL, UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);
    struct umem_mgr *umem_mgr = umem_pool_mgr_alloc_simple(UMEM_POOL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    udict_mgr_release(udict_mgr);
    struct uclock *uclock = uclock_std_alloc(0);

    /* probes */
    struct uprobe *uprobe_main = uprobe_stdio_alloc(NULL, stderr, loglevel);
    assert(uprobe_main != NULL);
    uprobe_main = uprobe_uref_mgr_alloc(uprobe_main, uref_mgr);
    assert(uprobe_main != NULL);
    uprobe_main = uprobe_uclock_alloc(uprobe_main, uclock);
    assert(uprobe_main != NULL);
    uprobe_main = uprobe_ubuf_mem_pool_alloc(uprobe_main, umem_mgr,
            UBUF_POOL_DEPTH, UBUF_SHARED_POOL_DEPTH);
    assert(uprobe_main != NULL);
    uprobe_main = uprobe_upump_mgr_alloc(uprobe_main, upump_mgr);
    assert(uprobe_main != NULL);
    uref_mgr_release(uref_mgr);
    uclock_release(uclock);
    umem_mgr_release(umem_mgr);

    struct uprobe *uprobe_dejitter = uprobe_dejitter_alloc(uprobe_use(uprobe_main), true, 0);
    assert(uprobe_dejitter != NULL);

    /* dvb */
    struct upipe_mgr *upipe_dvbsrc_mgr = upipe_dvbsrc_mgr_alloc();
    dvbsrc = upipe_void_alloc(upipe_dvbsrc_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_dejitter), loglevel , "dvbsrc"));
    upipe_mgr_release(upipe_dvbsrc_mgr);
    assert(dvbsrc);

    ubase_assert(upipe_set_uri(dvbsrc, src));
    ubase_assert(upipe_set_option(dvbsrc, "sys", sys));
    ubase_assert(upipe_set_option(dvbsrc, "lnb", lnb));
    ubase_assert(upipe_set_option(dvbsrc, "frequency", freq));
    ubase_assert(upipe_set_option(dvbsrc, "symbol-rate", srate));
    ubase_assert(upipe_set_option(dvbsrc, "inner-fec", fec));
    ubase_assert(upipe_set_option(dvbsrc, "polarization", pol));
    ubase_assert(upipe_attach_uclock(dvbsrc));

    struct upipe_mgr *udpsink_mgr = upipe_udpsink_mgr_alloc();
    struct upipe *udpsink = upipe_void_alloc_output(dvbsrc, udpsink_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_main), loglevel, "udp sink"));
    upipe_mgr_release(udpsink_mgr);
    assert(udpsink);
    ubase_assert(upipe_attach_uclock(udpsink));
    upipe_release(udpsink);

    ubase_assert(upipe_set_uri(udpsink, dst));

    struct upump *timer = upump_alloc_timer(upump_mgr, stats_timer, NULL, NULL,
            0, 10 * UCLOCK_FREQ);
    assert(timer != NULL);
    upump_start(timer);

    /* main loop */
    upump_mgr_run(upump_mgr, NULL);

    upump_stop(timer);
    upump_free(timer);

    upump_mgr_release(upump_mgr);
    uprobe_release(uprobe_main);

    return 0;
}
