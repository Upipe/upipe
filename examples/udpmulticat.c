/* GPL */

/** @file
 * @short plays a URI
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <syslog.h>

#include <upipe/uprobe.h>
#include <upipe/uprobe_syslog.h>
#include <arpa/inet.h> /* htonl */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_helper_alloc.h>
#include <upipe/uprobe_stdio_color.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_select_flows.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_upump_mgr.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/uref_block.h>
#include <upipe/uprobe_uclock.h>
#include <upipe/umem.h>
#include <upipe/umem_pool.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/ubuf.h>
#include <upipe/uclock.h>
#include <upipe/uclock_std.h>
#include <upipe/upipe.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe-modules/upipe_probe_uref.h>
#include <upipe-modules/upipe_null.h>
#include <upipe-modules/upipe_udp_sink.h>
#include <upipe-modules/upipe_udp_source.h>
#include <upipe-modules/upipe_rtp_source.h>
#include <upipe-modules/upipe_rtp_fec.h>
#include <upipe/uref_dump.h>

#define UMEM_POOL               512
#define UDICT_POOL_DEPTH        500
#define UREF_POOL_DEPTH         500
#define UBUF_POOL_DEPTH         3000
#define UBUF_SHARED_POOL_DEPTH  50
#define UPUMP_POOL              10
#define UPUMP_BLOCKER_POOL      10

/* main probe */
static struct uprobe *uprobe_main = NULL;

static enum uprobe_log_level loglevel = UPROBE_LOG_DEBUG;

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Error: Needs url\n");
        return -1;
    }

    const char *uri = argv[1];

    /* structures managers */
    struct ev_loop *main_loop = ev_default_loop(0);

    /* upump manager */
    struct upump_mgr *main_upump_mgr = upump_ev_mgr_alloc(main_loop,
            UPUMP_POOL, UPUMP_BLOCKER_POOL);
    assert(main_upump_mgr);

    struct umem_mgr *umem_mgr = umem_pool_mgr_alloc_simple(UMEM_POOL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    udict_mgr_release(udict_mgr);

    /* probes */
    uprobe_main = uprobe_stdio_color_alloc(NULL, stdout, loglevel);

    assert(uprobe_main);
    uprobe_main = uprobe_uref_mgr_alloc(uprobe_main, uref_mgr);
    uref_mgr_release(uref_mgr);
    assert(uprobe_main);
    uprobe_main = uprobe_upump_mgr_alloc(uprobe_main, main_upump_mgr);
    assert(uprobe_main != NULL);
    uprobe_main = uprobe_ubuf_mem_alloc(uprobe_main, umem_mgr, UBUF_POOL_DEPTH,
                                        UBUF_SHARED_POOL_DEPTH);
    umem_mgr_release(umem_mgr);
    assert(uprobe_main);

    struct uclock *uclock = uclock_std_alloc(0);

    uprobe_main = uprobe_uclock_alloc(uprobe_main, uclock);
    uclock_release(uclock);
    assert(uprobe_main);

    /* udp */
    /* main source pipe */
    struct upipe *main_src = NULL;
    /* col pipe */
    struct upipe *col_src = NULL;
    /* row pipe */
    struct upipe *row_src = NULL;

    struct upipe_mgr *src_mgr = upipe_udpsrc_mgr_alloc();
    main_src = upipe_void_alloc(src_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_main),
                loglevel, "udpsrc main"));
    assert(main_src);
    row_src = upipe_void_alloc(src_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_main),
                loglevel, "udpsrc row"));
    assert(row_src);
    col_src = upipe_void_alloc(src_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_main),
                loglevel, "udpsrc col"));
    assert(col_src);
    upipe_mgr_release(src_mgr);

    ubase_assert(upipe_set_uri(main_src, "@:1234"));
    ubase_assert(upipe_set_uri(col_src, "@:1236"));
    ubase_assert(upipe_set_uri(row_src, "@:1238"));

    upipe_attach_uclock(main_src);
    upipe_attach_uclock(col_src);
    upipe_attach_uclock(row_src);

    /* fec */
    struct upipe_mgr *upipe_rtp_fec_mgr = upipe_rtp_fec_mgr_alloc();

    struct upipe *rtp_fec = upipe_rtp_fec_alloc(upipe_rtp_fec_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_main), loglevel, "rtp_fec"),
            uprobe_pfx_alloc(uprobe_use(uprobe_main), loglevel, "rtp_main_fec"),
            uprobe_pfx_alloc(uprobe_use(uprobe_main), loglevel, "rtp_col_fec"),
            uprobe_pfx_alloc(uprobe_use(uprobe_main), loglevel, "rtp_row_fec"));
    assert(rtp_fec);
    upipe_mgr_release(upipe_rtp_fec_mgr);

    upipe_attach_uclock(rtp_fec);

    struct upipe *dst;

    upipe_rtp_fec_get_main_sub(rtp_fec, &dst);
    upipe_set_output(main_src, dst);

    upipe_rtp_fec_get_col_sub(rtp_fec, &dst);
    upipe_set_output(col_src, dst);

    upipe_rtp_fec_get_row_sub(rtp_fec, &dst);
    upipe_set_output(row_src, dst);

    /* sink */
    struct upipe_mgr *udpsink_mgr = upipe_udpsink_mgr_alloc();
    struct upipe *sink = upipe_void_alloc_output(rtp_fec, udpsink_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_main),
                loglevel, "udpsink"));
    assert(sink);

    if (!ubase_check(upipe_set_uri(sink, uri))) {
        upipe_err_va(sink, "Could not set uri");
        exit(1);
    }

    /* main loop */
    ev_loop(main_loop, 0);

    upipe_release(sink);

    uprobe_release(uprobe_main);

    upump_mgr_release(main_upump_mgr);

    ev_default_destroy();

    return 0;
}
