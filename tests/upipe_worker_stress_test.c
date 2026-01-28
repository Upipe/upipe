/*
 * Copyright (C) 2021 EasyTools S.A.S.
 *
 * Authors: Clément Vasseur
 *
 * SPDX-License-Identifier: MIT
 */

#undef NDEBUG

#include "upipe/upump.h"
#include "upump-ev/upump_ev.h"
#include "upipe/umem_alloc.h"
#include "upipe/udict_inline.h"
#include "upipe/uref_std.h"
#include "upipe/uref_void_flow.h"

#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_void.h"
#include "upipe/upipe_helper_output.h"
#include "upipe/upipe_helper_upump_mgr.h"
#include "upipe/upipe_helper_upump.h"
#include "upipe/upipe_helper_uref_mgr.h"

#include "upipe/uprobe_stdio.h"
#include "upipe/uprobe_prefix.h"
#include "upipe/uprobe_uref_mgr.h"

#include "upipe-pthread/uprobe_pthread_upump_mgr.h"
#include "upipe-pthread/uprobe_pthread_assert.h"

#include "upipe-modules/upipe_transfer.h"
#include "upipe-modules/upipe_worker.h"

#include <assert.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UPUMP_POOL 0
#define UPUMP_BLOCKER_POOL 0
#define XFER_QUEUE 255
#define XFER_POOL 1
#define WORK_IN_QUEUE 1
#define UPROBE_LOG_LEVEL UPROBE_LOG_ERROR

struct uprobe *logger = NULL;
struct upipe *source = NULL;
static pthread_t remote_thread_id;

struct source {
    struct upipe upipe;
    struct urefcount urefcount;
    struct upipe *output;
    struct uref *flow_def;
    enum upipe_helper_output_state output_state;
    struct uchain requests;
    struct upump_mgr *upump_mgr;
    struct upump *upump;
    struct uref_mgr *uref_mgr;
    struct urequest uref_mgr_request;
};

static int source_check(struct upipe *upipe, struct uref *flow_def);

UPIPE_HELPER_UPIPE(source, upipe, 0);
UPIPE_HELPER_UREFCOUNT(source, urefcount, source_free);
UPIPE_HELPER_VOID(source);
UPIPE_HELPER_OUTPUT(source, output, flow_def, output_state, requests);
UPIPE_HELPER_UPUMP_MGR(source, upump_mgr);
UPIPE_HELPER_UPUMP(source, upump, upump_mgr);
UPIPE_HELPER_UREF_MGR(source, uref_mgr, uref_mgr_request,
                      source_check,
                      source_alloc_output_proxy,
                      source_free_output_proxy);

static struct upipe *source_alloc(struct upipe_mgr *mgr,
                                  struct uprobe *uprobe,
                                  uint32_t signature,
                                  va_list args)
{
    struct upipe *upipe = source_alloc_void(mgr, uprobe, signature, args);
    assert(upipe);

    source_init_urefcount(upipe);
    source_init_output(upipe);
    source_init_upump_mgr(upipe);
    source_init_upump(upipe);
    source_init_uref_mgr(upipe);

    upipe_throw_ready(upipe);

    return upipe;
}

static void source_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    source_clean_uref_mgr(upipe);
    source_clean_upump(upipe);
    source_clean_upump_mgr(upipe);
    source_clean_output(upipe);
    source_clean_urefcount(upipe);
    source_free_void(upipe);
}

static int source_control_real(struct upipe *upipe, int cmd, va_list args)
{
    switch (cmd) {
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
        case UPIPE_SET_FLOW_DEF:
            return source_control_output(upipe, cmd, args);
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_dbg(upipe, "upump manager attached");
            source_set_upump(upipe, NULL);
            source_attach_upump_mgr(upipe);
            return UBASE_ERR_NONE;
    }
    return UBASE_ERR_UNHANDLED;
}

static int source_control(struct upipe *upipe, int cmd, va_list args)
{
    UBASE_RETURN(source_control_real(upipe, cmd, args));
    return source_check(upipe, NULL);
}

static void source_idle(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct source *source = source_from_upipe(upipe);

    struct uref *uref = uref_alloc_control(source->uref_mgr);
    assert(uref);
    upipe_use(upipe);
    source_output(upipe, uref, &source->upump);
    assert(!upipe_single(upipe));
    upipe_release(upipe);

    upipe_throw_source_end(upipe);
}

static int source_check(struct upipe *upipe, struct uref *flow_def)
{
    struct source *source = source_from_upipe(upipe);

    if (!source->uref_mgr) {
        source_require_uref_mgr(upipe);
        return UBASE_ERR_NONE;
    }

    if (!source->flow_def) {
        struct uref *flow_def = uref_void_flow_alloc_def(source->uref_mgr);
        assert(flow_def);
        source_store_flow_def(upipe, flow_def);
    }

    if (!ubase_check(source_check_upump_mgr(upipe)))
        return UBASE_ERR_NONE;

    if (!source->upump) {
        struct upump *upump = upump_alloc_idler(source->upump_mgr,
                                                source_idle, upipe,
                                                upipe->refcount);
        assert(upump);
        upump_start(upump);
        source_set_upump(upipe, upump);
    }

    return UBASE_ERR_NONE;
}

static struct upipe_mgr source_mgr = {
    .refcount = NULL,
    .signature = 0,
    .upipe_alloc = source_alloc,
    .upipe_input = NULL,
    .upipe_control = source_control,
};

struct sink {
    struct upipe upipe;
    struct urefcount urefcount;
    unsigned int count;
};

UPIPE_HELPER_UPIPE(sink, upipe, 0);
UPIPE_HELPER_UREFCOUNT(sink, urefcount, sink_free);
UPIPE_HELPER_VOID(sink);

static struct upipe *sink_alloc(struct upipe_mgr *mgr,
                                  struct uprobe *uprobe,
                                  uint32_t signature,
                                  va_list args)
{
    struct upipe *upipe = sink_alloc_void(mgr, uprobe, signature, args);
    assert(upipe);

    sink_init_urefcount(upipe);

    struct sink *sink = sink_from_upipe(upipe);
    sink->count = 0;

    upipe_throw_ready(upipe);

    return upipe;
}

static void sink_free(struct upipe *upipe)
{
    struct sink *sink = sink_from_upipe(upipe);

    upipe_throw_dead(upipe);

    assert(sink->count == 1);
    sink_clean_urefcount(upipe);
    sink_free_void(upipe);
}

static void sink_input(struct upipe *upipe,
                       struct uref *uref,
                       struct upump **upump_p)
{
    struct sink *sink = sink_from_upipe(upipe);
    sink->count++;
    uref_free(uref);
}

static int sink_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    ubase_assert(uref_flow_match_def(flow_def, UREF_VOID_FLOW_DEF));
    return UBASE_ERR_NONE;
}

static int sink_control(struct upipe *upipe, int cmd, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_control_provide_request(upipe, cmd, args));

    switch (cmd) {
        case UPIPE_ATTACH_UPUMP_MGR:
            return UBASE_ERR_NONE;

        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return sink_set_flow_def(upipe, flow_def);
        }
    }
    return UBASE_ERR_UNHANDLED;
}

static struct upipe_mgr sink_mgr = {
    .refcount = NULL,
    .signature = 0,
    .upipe_alloc = sink_alloc,
    .upipe_input = sink_input,
    .upipe_control = sink_control,
};

static void *thread(void *user_data)
{
    struct upipe_mgr *upipe_xfer_mgr = (struct upipe_mgr *)user_data;

    struct upump_mgr *upump_mgr =
        upump_ev_mgr_alloc_loop(UPUMP_POOL, UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);
    uprobe_pthread_upump_mgr_set(logger, upump_mgr);

    ubase_assert(upipe_xfer_mgr_attach(upipe_xfer_mgr, upump_mgr));
    upipe_mgr_release(upipe_xfer_mgr);

    upump_mgr_run(upump_mgr, NULL);

    upump_mgr_release(upump_mgr);

    return NULL;
}

static int catch_wsrc(struct uprobe *uprobe,
                      struct upipe *upipe,
                      int event, va_list args)
{
    switch (event) {
        case UPROBE_SOURCE_END:
            upipe_notice(upipe, "source ended");
            upipe_release(source);
            return UBASE_ERR_NONE;
    }

    return uprobe_throw_next(uprobe, upipe, event, args);
}

int main(int argc, char *argv[])
{
    struct upump_mgr *upump_mgr =
        upump_ev_mgr_alloc_default(UPUMP_POOL, UPUMP_BLOCKER_POOL);

    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);

    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);

    struct uref_mgr *uref_mgr =
        uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    assert(uref_mgr != NULL);

    logger = uprobe_stdio_alloc(NULL, stdout, UPROBE_LOG_LEVEL);
    assert(logger);

    logger = uprobe_uref_mgr_alloc(logger, uref_mgr);
    assert(logger);

    logger = uprobe_pthread_upump_mgr_alloc(logger);
    assert(logger);
    uprobe_pthread_upump_mgr_set(logger, upump_mgr);

    struct uprobe *uprobe_main =
        uprobe_pthread_assert_alloc(uprobe_use(logger));
    assert(uprobe_main);
    uprobe_pthread_assert_set(uprobe_main, pthread_self());

    struct uprobe *uprobe_remote =
        uprobe_pthread_assert_alloc(uprobe_use(logger));
    assert(uprobe_remote);

    struct upipe_mgr *upipe_xfer_mgr =
        upipe_xfer_mgr_alloc(XFER_QUEUE, XFER_POOL, NULL);
    assert(upipe_xfer_mgr != NULL);

    upipe_mgr_use(upipe_xfer_mgr);
    assert(!pthread_create(&remote_thread_id, NULL, thread, upipe_xfer_mgr));
    uprobe_pthread_assert_set(uprobe_remote, remote_thread_id);
    struct upipe_mgr *upipe_work_mgr = upipe_work_mgr_alloc(upipe_xfer_mgr);
    upipe_mgr_release(upipe_xfer_mgr);
    assert(upipe_work_mgr);

    while (1) {
        source =
            upipe_void_alloc(&source_mgr,
                             uprobe_pfx_alloc(
                                uprobe_alloc(catch_wsrc,
                                             uprobe_use(uprobe_main)),
                                UPROBE_LOG_LEVEL, "src"));
        assert(source);

        uprobe_throw(uprobe_main, NULL, UPROBE_FREEZE_UPUMP_MGR);

        struct upipe *sink =
            upipe_void_alloc(&sink_mgr,
                             uprobe_pfx_alloc(uprobe_use(uprobe_remote),
                                              UPROBE_LOG_LEVEL, "sink"));
        assert(sink);

        struct upipe *worker =
            upipe_work_alloc(upipe_work_mgr,
                             uprobe_pfx_alloc(uprobe_use(uprobe_main),
                                              UPROBE_LOG_LEVEL, "wsrc"),
                             sink,
                             uprobe_pfx_alloc(uprobe_use(uprobe_remote),
                                              UPROBE_LOG_LEVEL, "wsrc_x"),
                             WORK_IN_QUEUE, 0);
        assert(worker);

        uprobe_throw(uprobe_main, NULL, UPROBE_THAW_UPUMP_MGR);
        upipe_attach_upump_mgr(worker);

        ubase_assert(upipe_set_output(source, worker));
        upipe_release(worker);

        upump_mgr_run(upump_mgr, NULL);
    }

    upipe_mgr_release(upipe_work_mgr);
    assert(!pthread_join(remote_thread_id, NULL));

    uprobe_release(uprobe_remote);
    uprobe_release(uprobe_main);
    uprobe_release(logger);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    upump_mgr_release(upump_mgr);
}
