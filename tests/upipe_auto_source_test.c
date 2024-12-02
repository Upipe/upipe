#undef NDEBUG

#include "upipe/config.h"
#include "upump-ev/upump_ev.h"
#include "upipe/uprobe_stdio.h"
#include "upipe/umem.h"
#include "upipe/umem_pool.h"
#include "upipe/udict.h"
#include "upipe/udict_inline.h"
#include "upipe/uref_std.h"
#include "upipe/uclock.h"
#include "upipe/uclock_std.h"
#include "upipe/uprobe_uref_mgr.h"
#include "upipe/uprobe_ubuf_mem.h"
#include "upipe/uprobe_uclock.h"
#include "upipe/uprobe_upump_mgr.h"
#include "upipe/uprobe_source_mgr.h"
#include "upipe/uprobe_prefix.h"
#include "upipe-modules/upipe_auto_source.h"
#include "upipe-modules/upipe_file_source.h"
#include "upipe-modules/upipe_http_source.h"
#include "upipe-modules/upipe_dump.h"
#include "upipe-modules/upipe_null.h"
#ifdef UPIPE_HAVE_BEARSSL_H
#include "upipe-bearssl/uprobe_https_bearssl.h"
#endif
#ifdef UPIPE_HAVE_OPENSSL_SSL_H
#include "upipe-openssl/uprobe_https_openssl.h"
#endif

#include <getopt.h>

#define UPUMP_POOL                      10
#define UPUMP_BLOCKER_POOL              10
#define UDICT_POOL_DEPTH                500
#define UMEM_POOL                       512
#define UREF_POOL_DEPTH                 500
#define UBUF_POOL_DEPTH                 3000
#define LOG_LEVEL                       UPROBE_LOG_INFO

static int log_level = LOG_LEVEL;
static struct uprobe *main_probe = NULL;
static struct uref_mgr *uref_mgr = NULL;
static struct upipe **sources = NULL;
static const char *url = NULL;
static unsigned run_serial = 1;
static unsigned run_parallel = 1;
static bool dump = false;
static bool text = false;

static void quit(void)
{
    for (unsigned i = 0; i < run_parallel; i++) {
        upipe_release(sources[i]);
        sources[i] = NULL;
    }
}

/** @This handles SIGINT and SIGTERM signal. */
static void sigint_cb(struct upump *upump)
{
    static bool graceful = true;
    if (graceful)
        quit();
    else
        exit(-1);
    graceful = false;
}

static int catch_error(struct uprobe *uprobe,
                     struct upipe *upipe,
                     int event, va_list args)
{
    switch (event) {
    case UPROBE_FATAL: {
        int code = va_arg(args, int);
        exit(code);
        break;
    }

    case UPROBE_ERROR:
        quit();
        return UBASE_ERR_NONE;
    }

    return uprobe_throw_next(uprobe, upipe, event, args);
}

static int catch_source(struct uprobe *uprobe,
                        struct upipe *upipe,
                        int event, va_list args)
{
    if (event != UPROBE_SOURCE_END)
        return uprobe_throw_next(uprobe, upipe, event, args);
    upipe_notice(upipe, "source ended");
    unsigned *count = upipe_get_opaque(upipe, unsigned *);
    if (*count > 1) {
        --*count;
        upipe_set_uri(upipe, url);
    }
    return UBASE_ERR_NONE;
}

enum opt {
    OPT_VERBOSE         = 'v',
    OPT_DUMP            = 'd',
    OPT_DUMP_TEXT       = 'D',
    OPT_RUN_SERIAL      = 0x100,
    OPT_RUN_PARALLEL
};

static struct option options[] = {
    { "verbose", no_argument, NULL, OPT_VERBOSE },
    { "dump", no_argument, NULL, OPT_DUMP },
    { "dump-text", no_argument, NULL, OPT_DUMP_TEXT },
    { "run-serial", required_argument, NULL, OPT_RUN_SERIAL },
    { "run-parallel", required_argument, NULL, OPT_RUN_PARALLEL },
    { 0, 0, 0, 0 },
};

int main(int argc, char *argv[])
{
    int opt;
    int index;
    /*
     * parse options
     */
    while ((opt = getopt_long(argc, argv, "vdD", options, &index)) != -1) {
        switch (opt) {
        case OPT_VERBOSE:
            switch (log_level) {
            case UPROBE_LOG_DEBUG:
                log_level = UPROBE_LOG_VERBOSE;
                break;
            case UPROBE_LOG_INFO:
                log_level = UPROBE_LOG_DEBUG;
                break;
            }
            break;

        case OPT_DUMP:
            dump = true;
            break;

        case OPT_DUMP_TEXT:
            dump = true;
            text = true;
            break;

        case OPT_RUN_SERIAL:
            run_serial = atoi(optarg);
            break;

        case OPT_RUN_PARALLEL:
            run_parallel = atoi(optarg);
            break;

        case -1:
            break;

        default:
            abort();
        }
    }

    /*
     * parse arguments
     */
    if (optind >= argc)
        return -1;
    url = argv[optind];

    /*
     * create event loop
     */
    struct upump_mgr *upump_mgr =
        upump_ev_mgr_alloc_default(UPUMP_POOL, UPUMP_BLOCKER_POOL);
    assert(upump_mgr);
    struct upump *sigint_pump = upump_alloc_signal(upump_mgr, sigint_cb,
            (void *)SIGINT, NULL, SIGINT);
    upump_set_status(sigint_pump, false);
    upump_start(sigint_pump);
    struct upump *sigterm_pump = upump_alloc_signal(upump_mgr, sigint_cb,
            (void *)SIGTERM, NULL, SIGTERM);
    upump_set_status(sigterm_pump, false);
    upump_start(sigterm_pump);

    /*
     * create root probe
     */
    main_probe = uprobe_stdio_alloc(NULL, stderr, log_level);
    assert(main_probe);
    uprobe_stdio_set_color(main_probe, true);

    main_probe = uprobe_alloc(catch_error, main_probe);
    assert(main_probe);

    /*
     * add umem manager probe
     */
    {
        struct umem_mgr *umem_mgr = umem_pool_mgr_alloc_simple(UMEM_POOL);
        assert(umem_mgr);

        /*
         * add uref manager probe
         */
        {
            /*
             * add udict manager
             */
            struct udict_mgr *udict_mgr =
                udict_inline_mgr_alloc(UDICT_POOL_DEPTH, umem_mgr, -1, -1);
            assert(udict_mgr);
            uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
            udict_mgr_release(udict_mgr);
        }
        assert(uref_mgr);
        main_probe = uprobe_uref_mgr_alloc(main_probe, uref_mgr);
        assert(main_probe);

        main_probe =
            uprobe_ubuf_mem_alloc(main_probe, umem_mgr,
                                  UBUF_POOL_DEPTH, UBUF_POOL_DEPTH);
        umem_mgr_release(umem_mgr);
        assert(main_probe);
    }

    /*
     * add uclock probe
     */
    {
        struct uclock *uclock = uclock_std_alloc(0);
        assert(uclock);
        main_probe = uprobe_uclock_alloc(main_probe, uclock);
        assert(main_probe);
        uclock_release(uclock);
    }

    /* add upump_mgr probe */
    {
        main_probe = uprobe_upump_mgr_alloc(main_probe, upump_mgr);
        assert(main_probe);
    }

#ifdef UPIPE_HAVE_BEARSSL_H
    main_probe = uprobe_https_bearssl_alloc(main_probe);
    assert(main_probe);
#endif

#ifdef UPIPE_HAVE_OPENSSL_SSL_H
    main_probe = uprobe_https_openssl_alloc(main_probe);
    assert(main_probe);
#endif

    struct upipe_mgr *upipe_auto_src_mgr = upipe_auto_src_mgr_alloc();
    assert(upipe_auto_src_mgr);
    {
        struct upipe_mgr *upipe_fsrc_mgr = upipe_fsrc_mgr_alloc();
        struct upipe_mgr *upipe_http_src_mgr = upipe_http_src_mgr_alloc();
        assert(upipe_fsrc_mgr && upipe_http_src_mgr);
        ubase_assert(upipe_auto_src_mgr_set_mgr(upipe_auto_src_mgr, "file",
                                                upipe_fsrc_mgr));
        ubase_assert(upipe_auto_src_mgr_set_mgr(upipe_auto_src_mgr, "http",
                                                upipe_http_src_mgr));
#if defined(UPIPE_HAVE_BEARSSL_H) || defined(UPIPE_HAVE_OPENSSL_SSL_H)
        ubase_assert(upipe_auto_src_mgr_set_mgr(upipe_auto_src_mgr, "https",
                                                upipe_http_src_mgr));
#endif
        upipe_mgr_release(upipe_fsrc_mgr);
        upipe_mgr_release(upipe_http_src_mgr);
    }
    main_probe = uprobe_source_mgr_alloc(main_probe, upipe_auto_src_mgr);
    assert(main_probe);

    struct upipe *parallels[run_parallel];
    int counts[run_parallel];
    for (unsigned i = 0; i < run_parallel; i++) {
        parallels[i] = upipe_void_alloc(
            upipe_auto_src_mgr,
            uprobe_pfx_alloc_va(
                uprobe_alloc(catch_source, uprobe_use(main_probe)),
                UPROBE_LOG_VERBOSE, "src %u", i));
        assert(parallels[i]);
        ubase_assert(upipe_set_uri(parallels[i], url));
        counts[i] = run_serial;
        upipe_set_opaque(parallels[i], &counts[i]);

        /* create dump pipeline */
        {
            struct upipe *in = upipe_use(parallels[i]);

            if (dump) {
                struct upipe_mgr *upipe_dump_mgr = upipe_dump_mgr_alloc();
                assert(upipe_dump_mgr);
                in = upipe_void_chain_output(
                    in, upipe_dump_mgr,
                    uprobe_pfx_alloc_va(uprobe_use(main_probe),
                                        UPROBE_LOG_VERBOSE, "dump %u", i));
                upipe_mgr_release(upipe_dump_mgr);
                assert(in);
                if (text)
                    upipe_dump_set_text_mode(in);
            }

            struct upipe_mgr *upipe_null_mgr = upipe_null_mgr_alloc();
            assert(upipe_null_mgr);
            in = upipe_void_chain_output(
                in, upipe_null_mgr,
                uprobe_pfx_alloc_va(uprobe_use(main_probe),
                                    UPROBE_LOG_VERBOSE, "null %u", i));
            upipe_mgr_release(upipe_null_mgr);
            assert(in);
            upipe_release(in);
        }
    }
    upipe_mgr_release(upipe_auto_src_mgr);
    uprobe_release(main_probe);
    sources = parallels;

    /*
     * run main loop
     */
    upump_mgr_run(upump_mgr, NULL);

    for (unsigned i = 0; i < run_parallel; i++)
        upipe_release(sources[i]);

    upump_free(sigint_pump);
    upump_free(sigterm_pump);
    upump_mgr_release(upump_mgr);
    uref_mgr_release(uref_mgr);
}
