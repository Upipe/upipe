/*
 * Copyright (C) 2012-2017 OpenHeadend S.A.R.L.
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
 */

/** @file
 * @short unit test for http source
 */

#undef NDEBUG

#include "upipe/config.h"
#include "upipe/uprobe.h"
#include "upipe/uprobe_stdio.h"
#include "upipe/uprobe_prefix.h"
#include "upipe/uprobe_uref_mgr.h"
#include "upipe/uprobe_upump_mgr.h"
#include "upipe/uprobe_ubuf_mem.h"
#include "upipe/uclock.h"
#include "upipe/uclock_std.h"
#include "upipe/umem.h"
#include "upipe/umem_alloc.h"
#include "upipe/udict.h"
#include "upipe/udict_inline.h"
#include "upipe/uref.h"
#include "upipe/uref_std.h"
#include "upipe/upump.h"
#include "upump-ev/upump_ev.h"
#include "upipe/upipe.h"
#include "upipe-modules/upipe_http_source.h"
#include "upipe-modules/uprobe_http_redirect.h"
#include "upipe-modules/upipe_file_sink.h"
#ifdef UPIPE_HAVE_BEARSSL_H
#include "upipe-bearssl/uprobe_https_bearssl.h"
#endif
#ifdef UPIPE_HAVE_OPENSSL_SSL_H
#include "upipe-openssl/uprobe_https_openssl.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <getopt.h>

#define UDICT_POOL_DEPTH 10
#define UREF_POOL_DEPTH 10
#define UBUF_POOL_DEPTH 10
#define UPUMP_POOL 1
#define UPUMP_BLOCKER_POOL 1
#define READ_SIZE 4096

static int log_level = UPROBE_LOG_NOTICE;

/** definition of our uprobe */
static int catch(struct uprobe *uprobe, struct upipe *upipe,
                 int event, va_list args)
{
    switch (event) {
        default:
            assert(0);
            break;
        case UPROBE_READY:
        case UPROBE_DEAD:
        case UPROBE_SOURCE_END:
        case UPROBE_NEW_FLOW_DEF:
        case UPROBE_HTTP_SRC_REDIRECT:
            break;
    }
    return UBASE_ERR_NONE;
}

enum opt {
    OPT_HELP        = 'h',
    OPT_VERBOSE     = 'v',
    OPT_QUIET       = 'q',
    OPT_PARALLEL    = 'p',
    OPT_USE_BEARSSL = 0x100,
    OPT_USE_OPENSSL,
};

static struct option options[] = {
    { "help", no_argument, NULL, OPT_HELP },
    { "verbose", no_argument, NULL, OPT_VERBOSE },
    { "quiet", no_argument, NULL, OPT_QUIET },
    { "parallel", required_argument, NULL, OPT_PARALLEL },
    { "use-bearssl", no_argument, NULL, OPT_USE_BEARSSL },
    { "use-openssl", no_argument, NULL, OPT_USE_OPENSSL },
    { 0, 0, 0, 0 },
};

static int usage(const char *name)
{
    fprintf(stdout, "Usage: %s [options] <url>\n", name);
    for (int i = 0; options[i].name; i++) {
        if (options[i].val < 0x100) {
            fprintf(stdout, "   -%c, --%s\n", options[i].val, options[i].name);
        } else {
            fprintf(stdout, "   --%s\n", options[i].name);
        }
    }
    return 0;
}

int main(int argc, char *argv[])
{
    int opt;
    int index;
    int parallel = 1;
#ifdef UPIPE_HAVE_BEARSSL_H
    bool use_bearssl = true;
#endif
#ifdef UPIPE_HAVE_OPENSSL_SSL_H
    bool use_openssl = true;
#endif

    /*
     * parse options
     */
    while ((opt = getopt_long(argc, argv, "hvqp:", options, &index)) != -1) {
        switch (opt) {
        case OPT_HELP:
            usage(argv[0]);
            exit(EXIT_SUCCESS);
            return 0;

        case OPT_VERBOSE:
            if (log_level > UPROBE_LOG_VERBOSE)
                log_level--;
            break;

        case OPT_QUIET:
            if (log_level < UPROBE_LOG_ERROR)
                log_level++;
            break;

        case OPT_PARALLEL:
            parallel = atoi(optarg);
            break;

#ifdef UPIPE_HAVE_BEARSSL_H
        case OPT_USE_BEARSSL:
            use_bearssl = true;
#ifdef UPIPE_HAVE_OPENSSL_SSL_H
            use_openssl = false;
#endif
            break;
#endif

#ifdef UPIPE_HAVE_OPENSSL_SSL_H
        case OPT_USE_OPENSSL:
            use_openssl = true;
#ifdef UPIPE_HAVE_BEARSSL_H
            use_bearssl = false;
#endif
            break;
#endif

        case -1:
            break;

        default:
            abort();
        }
    }

    /*
     * parse arguments
     */
    if (optind >= argc) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
        return -1;
    }

    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    assert(uref_mgr != NULL);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc_default(UPUMP_POOL,
            UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);
    struct uclock *uclock = uclock_std_alloc(0);
    assert(uclock != NULL);
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stderr, log_level);
    assert(logger != NULL);
    logger = uprobe_uref_mgr_alloc(logger, uref_mgr);
    assert(logger != NULL);
    logger = uprobe_upump_mgr_alloc(logger, upump_mgr);
    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_POOL_DEPTH);
    assert(logger != NULL);
    logger = uprobe_http_redir_alloc(logger);
    assert(logger);
#ifdef UPIPE_HAVE_BEARSSL_H
    if (use_bearssl) {
        logger = uprobe_https_bearssl_alloc(logger);
        assert(logger);
    }
#endif

#ifdef UPIPE_HAVE_OPENSSL_SSL_H
    if (use_openssl) {
        logger = uprobe_https_openssl_alloc(logger);
        assert(logger);
    }
#endif

    struct upipe *sources[parallel];
    for (int i = 0; i < parallel; i++) {
        for (int j = optind; j < argc; j++) {
            struct upipe_mgr *upipe_fsink_mgr = upipe_fsink_mgr_alloc();
            struct upipe *upipe_fsink = upipe_void_alloc(
                upipe_fsink_mgr,
                uprobe_pfx_alloc(uprobe_use(logger), log_level, "fsink"));
            upipe_mgr_release(upipe_fsink_mgr);
            ubase_assert(upipe_fsink_set_path(
                upipe_fsink, "/dev/stdout", UPIPE_FSINK_OVERWRITE));

            struct upipe_mgr *upipe_http_src_mgr = upipe_http_src_mgr_alloc();
            sources[i] = upipe_void_alloc(
                upipe_http_src_mgr,
                uprobe_pfx_alloc(uprobe_use(logger), log_level, "http"));
            upipe_mgr_release(upipe_http_src_mgr); // nop
            assert(sources[i] != NULL);
            ubase_assert(upipe_set_output_size(sources[i], READ_SIZE));
            ubase_assert(upipe_set_uri(sources[i], argv[j]));
            ubase_assert(upipe_set_output(sources[i], upipe_fsink));
            upipe_release(upipe_fsink);
        }
    }

    upump_mgr_run(upump_mgr, NULL);

    for (int i = 0; i < parallel; i++)
        upipe_release(sources[i]);

    upump_mgr_release(upump_mgr);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uclock_release(uclock);
    uprobe_release(logger);
    uprobe_clean(&uprobe);

    return 0;
}
