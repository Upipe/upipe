/*
 * Copyright (C) 2016-2019 OpenHeadend S.A.R.L.
 * Copyright (C) 2021-2025 EasyTools S.A.S.
 *
 * Authors: Clément Vasseur
 *
 * SPDX-License-Identifier: MIT
 */

#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_output.h"
#include "upipe/upipe_helper_output_size.h"
#include "upipe/upipe_helper_input.h"
#include "upipe/upipe_helper_uclock.h"
#include "upipe/upipe_helper_upump_mgr.h"
#include "upipe/upipe_helper_uref_mgr.h"
#include "upipe/upipe_helper_ubuf_mgr.h"
#include "upipe/upipe_helper_inner.h"
#include "upipe/upipe_helper_bin_input.h"
#include "upipe/upipe_helper_bin_output.h"
#include "upipe/upipe_helper_sync.h"
#include "upipe/upipe_helper_uref_stream.h"
#include "upipe/upipe_helper_flow_def.h"
#include "upipe/upipe_helper_flow_format.h"
#include "upipe/upipe_helper_upump.h"

struct upipe_helper_mgr {
    struct upipe_mgr mgr;
    struct urefcount refcount;
    urefcount_cb refcount_cb;

    // uclock
    upipe_helper_uclock_check uclock_check;

    // uref_mgr
    upipe_helper_uref_mgr_check uref_mgr_check;

    // ubuf_mgr
    upipe_helper_ubuf_mgr_check ubuf_mgr_check;

    // flow_format
    upipe_helper_flow_format_check flow_format_check;

    // input
    bool (*output)(struct upipe *, struct uref *, struct upump **);

    // uref_stream
    void (*stream_append_cb)(struct upipe *);
};

struct upipe_helper {
    // upipe
    struct upipe upipe;

    // urefcount
    struct urefcount urefcount;

    // output
    struct upipe *output;
    struct uref *flow_def;
    enum upipe_helper_output_state output_state;
    struct uchain request_list;

    // output_size
    unsigned int output_size;

    // input
    struct uchain urefs;
    unsigned int nb_urefs;
    unsigned int max_urefs;
    struct uchain blockers;

    // uclock
    struct uclock *uclock;
    struct urequest uclock_request;

    // upump_mgr
    struct upump_mgr *upump_mgr;

    // uref_mgr
    struct uref_mgr *uref_mgr;
    struct urequest uref_mgr_request;

    // ubuf_mgr
    struct ubuf_mgr *ubuf_mgr;
    struct uref *flow_format;
    struct urequest ubuf_mgr_request;

    // bin_input
    struct upipe *first_inner;
    struct uchain input_request_list;

    // bin_output
    struct upipe *last_inner;
    struct upipe *bin_output;
    struct uchain output_request_list;

    // sync
    bool acquired;

    // uref_stream
    struct uref *next_uref;
    size_t next_uref_size;
    struct uchain stream_urefs;

    // flow_def
    struct uref *flow_def_input;
    struct uref *flow_def_attr;

    // flow_format
    struct urequest flow_format_request;

    // upump
    struct upump *upump;
};

static struct upipe_helper_mgr *upipe_helper_mgr(struct upipe *upipe)
{
    return container_of(upipe->mgr, struct upipe_helper_mgr, mgr);
}

bool upipe_helper_input_output(struct upipe *upipe,
                               struct uref *uref,
                               struct upump **upump_p)
{
    struct upipe_helper_mgr *mgr = upipe_helper_mgr(upipe);

    if (mgr->output != NULL)
        return mgr->output(upipe, uref, upump_p);

    return false;
}

static void upipe_helper_free(struct upipe *upipe)
{
    struct upipe_helper_mgr *mgr = upipe_helper_mgr(upipe);

    if (mgr->refcount_cb != NULL)
        return mgr->refcount_cb(upipe->refcount);
}

static int check_uclock(struct upipe *upipe, struct uref *uref)
{
    struct upipe_helper_mgr *mgr = upipe_helper_mgr(upipe);

    if (mgr->uclock_check != NULL)
        return mgr->uclock_check(upipe, uref);

    uref_free(uref);
    return UBASE_ERR_NONE;
}

static int check_uref_mgr(struct upipe *upipe, struct uref *uref)
{
    struct upipe_helper_mgr *mgr = upipe_helper_mgr(upipe);

    if (mgr->uref_mgr_check != NULL)
        return mgr->uref_mgr_check(upipe, uref);

    uref_free(uref);
    return UBASE_ERR_NONE;
}

static int check_ubuf_mgr(struct upipe *upipe, struct uref *uref)
{
    struct upipe_helper_mgr *mgr = upipe_helper_mgr(upipe);

    if (mgr->ubuf_mgr_check != NULL)
        return mgr->ubuf_mgr_check(upipe, uref);

    uref_free(uref);
    return UBASE_ERR_NONE;
}

static int check_flow_format(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_helper_mgr *mgr = upipe_helper_mgr(upipe);

    if (mgr->flow_format_check != NULL)
        return mgr->flow_format_check(upipe, flow_format);

    uref_free(flow_format);
    return UBASE_ERR_NONE;
}

static void append_cb(struct upipe *upipe)
{
    struct upipe_helper_mgr *mgr = upipe_helper_mgr(upipe);

    if (mgr->stream_append_cb != NULL)
        mgr->stream_append_cb(upipe);
}

#define static
#define inline

UPIPE_HELPER_UPIPE(upipe_helper, upipe, upipe->mgr->signature);
UPIPE_HELPER_UREFCOUNT(upipe_helper, urefcount, upipe_helper_free);
UPIPE_HELPER_OUTPUT(upipe_helper, output, flow_def, output_state, request_list);
UPIPE_HELPER_OUTPUT_SIZE(upipe_helper, output_size);
UPIPE_HELPER_INPUT(upipe_helper, urefs, nb_urefs, max_urefs, blockers, upipe_helper_input_output);
UPIPE_HELPER_UCLOCK(upipe_helper, uclock, uclock_request,
                    check_uclock,
                    upipe_helper_register_output_request,
                    upipe_helper_unregister_output_request);
UPIPE_HELPER_UPUMP_MGR(upipe_helper, upump_mgr);
UPIPE_HELPER_UREF_MGR(upipe_helper, uref_mgr, uref_mgr_request,
                      check_uref_mgr,
                      upipe_helper_register_output_request,
                      upipe_helper_unregister_output_request);
UPIPE_HELPER_UBUF_MGR(upipe_helper, ubuf_mgr, flow_format, ubuf_mgr_request,
                      check_ubuf_mgr,
                      upipe_helper_register_output_request,
                      upipe_helper_unregister_output_request);
UPIPE_HELPER_INNER(upipe_helper, first_inner);
UPIPE_HELPER_BIN_INPUT(upipe_helper, first_inner, input_request_list);
UPIPE_HELPER_INNER(upipe_helper, last_inner);
UPIPE_HELPER_BIN_OUTPUT(upipe_helper, last_inner, bin_output, output_request_list);
UPIPE_HELPER_SYNC(upipe_helper, acquired);
UPIPE_HELPER_UREF_STREAM(upipe_helper, next_uref, next_uref_size, stream_urefs,
                         append_cb);
UPIPE_HELPER_FLOW_DEF(upipe_helper, flow_def_input, flow_def_attr);
UPIPE_HELPER_FLOW_FORMAT(upipe_helper, flow_format_request,
                         check_flow_format,
                         upipe_helper_register_output_request,
                         upipe_helper_unregister_output_request);
UPIPE_HELPER_UPUMP(upipe_helper, upump, upump_mgr);
