/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
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

#include <upipe-modules/upipe_sequential_source.h>
#include <upipe-modules/upipe_probe_uref.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe/upipe_helper_bin_output.h>
#include <upipe/upipe_helper_inner.h>
#include <upipe/upipe_helper_uprobe.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe.h>
#include <upipe/uprobe_prefix.h>

struct upipe_seq_src {
    struct upipe upipe;
    struct urefcount urefcount;
    struct urefcount urefcount_real;
    struct uprobe probe_src;
    struct upipe *src;
    struct upipe *output;
    struct uchain requests;
    char *uri;
    struct uchain uchain;
    struct urefcount inner_ref;
    struct uclock *uclock;
    struct urequest uclock_request;
};

static int probe_src(struct uprobe *uprobe, struct upipe *inner,
                     int event, va_list args);

UPIPE_HELPER_UPIPE(upipe_seq_src, upipe, UPIPE_SEQ_SRC_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_seq_src, urefcount, upipe_seq_src_no_ref);
UPIPE_HELPER_VOID(upipe_seq_src);
UPIPE_HELPER_INNER(upipe_seq_src, src);
UPIPE_HELPER_UPROBE(upipe_seq_src, urefcount_real, probe_src, probe_src);
UPIPE_HELPER_BIN_OUTPUT(upipe_seq_src, src, output, requests);
UPIPE_HELPER_UCLOCK(upipe_seq_src, uclock, uclock_request, NULL,
                    upipe_seq_src_register_bin_output_request,
                    upipe_seq_src_unregister_bin_output_request)

UBASE_FROM_TO(upipe_seq_src, urefcount, urefcount_real, urefcount_real);
UBASE_FROM_TO(upipe_seq_src, urefcount, inner_ref, inner_ref);
UBASE_FROM_TO(upipe_seq_src, uchain, uchain, uchain);

static void upipe_seq_src_free(struct urefcount *urefcount);
static void upipe_seq_src_done(struct urefcount *urefcount);

struct upipe_seq_src_mgr {
    struct upipe_mgr mgr;
    struct urefcount urefcount;
    struct upipe_mgr *source_mgr;
    struct uchain jobs;
    struct urefcount *lock;
};

UBASE_FROM_TO(upipe_seq_src_mgr, upipe_mgr, mgr, mgr);
UBASE_FROM_TO(upipe_seq_src_mgr, urefcount, urefcount, urefcount);

static int upipe_seq_src_mgr_next(struct upipe_mgr *mgr);

/*
 * pipe
 */

static int probe_src(struct uprobe *uprobe, struct upipe *inner,
                     int event, va_list args)
{
    struct upipe_seq_src *upipe_seq_src = upipe_seq_src_from_probe_src(uprobe);
    struct upipe *upipe = upipe_seq_src_to_upipe(upipe_seq_src);

    switch (event) {
    case UPROBE_SOURCE_END:
        upipe_seq_src_store_bin_output(upipe, NULL);
        break;
    }

    return upipe_throw_proxy(upipe, inner, event, args);
}

static struct upipe *upipe_seq_src_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature,
                                         va_list args)
{
    struct upipe *upipe =
        upipe_seq_src_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_seq_src_init_urefcount(upipe);
    upipe_seq_src_init_probe_src(upipe);
    upipe_seq_src_init_bin_output(upipe);
    upipe_seq_src_init_uclock(upipe);

    struct upipe_seq_src *upipe_seq_src = upipe_seq_src_from_upipe(upipe);
    urefcount_init(&upipe_seq_src->urefcount_real, upipe_seq_src_free);
    uchain_init(&upipe_seq_src->uchain);
    upipe_seq_src->uri = NULL;

    upipe_throw_ready(upipe);

    return upipe;
}

static void upipe_seq_src_free(struct urefcount *urefcount)
{
    struct upipe_seq_src *upipe_seq_src =
        upipe_seq_src_from_urefcount_real(urefcount);
    struct upipe *upipe = upipe_seq_src_to_upipe(upipe_seq_src);

    upipe_throw_dead(upipe);

    free(upipe_seq_src->uri);
    urefcount_clean(&upipe_seq_src->urefcount_real);
    upipe_seq_src_clean_uclock(upipe);
    upipe_seq_src_clean_bin_output(upipe);
    upipe_seq_src_clean_probe_src(upipe);
    upipe_seq_src_clean_urefcount(upipe);
    upipe_seq_src_free_void(upipe);
}

static void upipe_seq_src_no_ref(struct upipe *upipe)
{
    struct upipe_seq_src *upipe_seq_src = upipe_seq_src_from_upipe(upipe);
    if (ulist_is_in(&upipe_seq_src->uchain))
        ulist_delete(&upipe_seq_src->uchain);
    upipe_seq_src_store_bin_output(upipe, NULL);
    urefcount_release(&upipe_seq_src->urefcount_real);
}

static void upipe_seq_src_done(struct urefcount *urefcount)
{
    struct upipe_seq_src *upipe_seq_src =
        upipe_seq_src_from_inner_ref(urefcount);
    struct upipe *upipe = upipe_seq_src_to_upipe(upipe_seq_src);
    struct upipe_seq_src_mgr *upipe_seq_src_mgr =
        upipe_seq_src_mgr_from_mgr(upipe->mgr);

    upipe_seq_src->probe_src.refcount = NULL;
    upipe_seq_src_mgr->lock = NULL;
    upipe_seq_src_mgr_next(upipe->mgr);
    urefcount_release(&upipe_seq_src->urefcount_real);
}

static inline void upipe_seq_src_set_inner(struct upipe *upipe,
                                           struct upipe *inner)
{
    struct upipe_seq_src *upipe_seq_src = upipe_seq_src_from_upipe(upipe);

    upipe_seq_src_store_bin_output(upipe, inner);
    if (upipe_seq_src->uclock)
        upipe_attach_uclock(inner);
}

static int upipe_seq_src_worker(struct upipe *upipe)
{
    struct upipe_seq_src *upipe_seq_src = upipe_seq_src_from_upipe(upipe);
    struct upipe_seq_src_mgr *upipe_seq_src_mgr =
        upipe_seq_src_mgr_from_mgr(upipe->mgr);

    urefcount_init(&upipe_seq_src->inner_ref, upipe_seq_src_done);
    urefcount_use(&upipe_seq_src->urefcount_real);
    upipe_seq_src->probe_src.refcount = &upipe_seq_src->inner_ref;
    struct upipe *inner = upipe_void_alloc(
        upipe_seq_src_mgr->source_mgr,
        uprobe_pfx_alloc(uprobe_use(&upipe_seq_src->probe_src),
                         UPROBE_LOG_VERBOSE, "src"));
    urefcount_release(&upipe_seq_src->inner_ref);
    UBASE_ALLOC_RETURN(inner);
    int ret = upipe_set_uri(inner, upipe_seq_src->uri);
    if (unlikely(!ubase_check(ret))) {
        upipe_release(inner);
        return ret;
    }
    upipe_seq_src_set_inner(upipe, inner);
    return UBASE_ERR_NONE;
}

static int upipe_seq_src_set_uri(struct upipe *upipe, const char *uri)
{
    struct upipe_seq_src *upipe_seq_src = upipe_seq_src_from_upipe(upipe);
    struct upipe_seq_src_mgr *upipe_seq_src_mgr =
        upipe_seq_src_mgr_from_mgr(upipe->mgr);

    if (ulist_is_in(&upipe_seq_src->uchain))
        ulist_delete(&upipe_seq_src->uchain);
    if (upipe_seq_src->uri)
        free(upipe_seq_src->uri);
    upipe_seq_src->uri = NULL;

    if (uri == NULL)
        return UBASE_ERR_NONE;

    upipe_seq_src->uri = strdup(uri);
    UBASE_ALLOC_RETURN(upipe_seq_src->uri);
    ulist_add(&upipe_seq_src_mgr->jobs, &upipe_seq_src->uchain);
    return upipe_seq_src_mgr_next(upipe->mgr);
}

static int upipe_seq_src_get_uri(struct upipe *upipe, const char **uri_p)
{
    struct upipe_seq_src *upipe_seq_src = upipe_seq_src_from_upipe(upipe);
    if (uri_p)
        *uri_p = upipe_seq_src->uri;
    return UBASE_ERR_NONE;
}

static int upipe_seq_src_control(struct upipe *upipe,
                                 int command,
                                 va_list args)
{
    switch (command) {
    case UPIPE_ATTACH_UCLOCK:
        upipe_seq_src_require_uclock(upipe);
        return UBASE_ERR_NONE;
    case UPIPE_SET_URI: {
        const char *uri = va_arg(args, const char *);
        return upipe_seq_src_set_uri(upipe, uri);
    }
    case UPIPE_GET_URI: {
        const char **uri_p = va_arg(args, const char **);
        return upipe_seq_src_get_uri(upipe, uri_p);
    }
    }

    return upipe_seq_src_control_bin_output(upipe, command, args);
}

/*
 * manager
 */

static void upipe_seq_src_mgr_free(struct urefcount *urefcount);

struct upipe_mgr *upipe_seq_src_mgr_alloc(void)
{
    struct upipe_seq_src_mgr *upipe_seq_src_mgr =
        malloc(sizeof (struct upipe_seq_src_mgr));
    if (unlikely(upipe_seq_src_mgr == NULL))
        return NULL;

    urefcount_init(&upipe_seq_src_mgr->urefcount, upipe_seq_src_mgr_free);
    upipe_seq_src_mgr->mgr.refcount = &upipe_seq_src_mgr->urefcount;
    upipe_seq_src_mgr->mgr.signature = UPIPE_SEQ_SRC_SIGNATURE;
    upipe_seq_src_mgr->mgr.upipe_alloc = upipe_seq_src_alloc;
    upipe_seq_src_mgr->mgr.upipe_control = upipe_seq_src_control;
    upipe_seq_src_mgr->source_mgr = NULL;
    upipe_seq_src_mgr->lock = NULL;
    ulist_init(&upipe_seq_src_mgr->jobs);

    return upipe_seq_src_mgr_to_mgr(upipe_seq_src_mgr);
}

static void upipe_seq_src_mgr_free(struct urefcount *urefcount)
{
    struct upipe_seq_src_mgr *upipe_seq_src_mgr =
        upipe_seq_src_mgr_from_urefcount(urefcount);

    assert(ulist_empty(&upipe_seq_src_mgr->jobs));
    upipe_mgr_release(upipe_seq_src_mgr->source_mgr);
    urefcount_clean(&upipe_seq_src_mgr->urefcount);
    free(upipe_seq_src_mgr);
}

int upipe_seq_src_mgr_set_source_mgr(struct upipe_mgr *mgr,
                                     struct upipe_mgr *source_mgr)
{
    struct upipe_seq_src_mgr *upipe_seq_src_mgr =
        upipe_seq_src_mgr_from_mgr(mgr);
    if (upipe_seq_src_mgr->source_mgr)
        upipe_mgr_release(upipe_seq_src_mgr->source_mgr);
    upipe_seq_src_mgr->source_mgr = upipe_mgr_use(source_mgr);
    return UBASE_ERR_NONE;
}

static int upipe_seq_src_mgr_next(struct upipe_mgr *mgr)
{
    struct upipe_seq_src_mgr *upipe_seq_src_mgr =
        upipe_seq_src_mgr_from_mgr(mgr);

    if (unlikely(upipe_seq_src_mgr->lock))
        return UBASE_ERR_NONE;

    struct uchain *uchain = ulist_pop(&upipe_seq_src_mgr->jobs);
    if (unlikely(uchain == NULL))
        return UBASE_ERR_NONE;

    struct upipe_seq_src *upipe_seq_src = upipe_seq_src_from_uchain(uchain);
    upipe_seq_src_mgr->lock = &upipe_seq_src->inner_ref;
    return upipe_seq_src_worker(upipe_seq_src_to_upipe(upipe_seq_src));
}
