#include <stdlib.h>

#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_clock.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_input.h>
#include <upipe-modules/upipe_stream_switcher.h>

struct upipe_stream_switcher {
    /* for urefcount helper */
    struct urefcount urefcount;

    /* for subpipe helper */
    struct uchain sub_pipes;
    struct upipe_mgr sub_mgr;

    /* for output helper */
    struct upipe *output;
    struct uref *flow_def;
    enum upipe_helper_output_state output_state;
    struct uchain request_list;

    /* private */
    struct upipe *selected;
    struct upipe *waiting;
    uint64_t pts_orig;
    uint64_t rebase_timestamp;
    bool rebase_timestamp_set;

    /* for upipe helper */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_stream_switcher, upipe,
                   UPIPE_STREAM_SWITCHER_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_stream_switcher, urefcount,
                       upipe_stream_switcher_free)
UPIPE_HELPER_VOID(upipe_stream_switcher)
UPIPE_HELPER_OUTPUT(upipe_stream_switcher, output, flow_def, output_state,
                    request_list)

struct upipe_stream_switcher_input {
    /* for urefcount helper */
    struct urefcount urefcount;

    /* for subpipe helper */
    struct uchain uchain;

    /* for input helper */
    struct uchain urefs;
    unsigned int nb_urefs;
    unsigned int max_urefs;
    struct uchain blockers;

    /* private */
    bool destroyed;

    /* super pipe ref */
    struct upipe *super;
    /* for upipe helper */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_stream_switcher_input, upipe,
                   UPIPE_STREAM_SWITCHER_SUB_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_stream_switcher_input, urefcount,
                       upipe_stream_switcher_input_free)
UPIPE_HELPER_VOID(upipe_stream_switcher_input)
static bool upipe_stream_switcher_input_output(struct upipe *, struct uref *,
                                               struct upump **);
UPIPE_HELPER_INPUT(upipe_stream_switcher_input, urefs, nb_urefs, max_urefs,
                   blockers, upipe_stream_switcher_input_output)

UPIPE_HELPER_SUBPIPE(upipe_stream_switcher, upipe_stream_switcher_input,
                     input, sub_mgr, sub_pipes, uchain)

/*
 * sub pipes
 */

static struct upipe *upipe_stream_switcher_input_alloc(struct upipe_mgr *mgr,
                                                       struct uprobe *uprobe,
                                                       uint32_t signature,
                                                       va_list args)
{
    struct upipe_stream_switcher *upipe_stream_switcher =
        upipe_stream_switcher_from_sub_mgr(mgr);

    struct upipe *upipe =
        upipe_stream_switcher_input_alloc_void(mgr, uprobe, signature, args);
    upipe_stream_switcher_input_init_urefcount(upipe);
    upipe_stream_switcher_input_init_sub(upipe);
    upipe_stream_switcher_input_init_input(upipe);

    struct upipe_stream_switcher_input *upipe_stream_switcher_input =
        upipe_stream_switcher_input_from_upipe(upipe);
    upipe_stream_switcher_input->destroyed = false;
    upipe_stream_switcher_input->super =
        upipe_use(upipe_stream_switcher_to_upipe(upipe_stream_switcher));

    upipe_throw_ready(upipe);

    if (!upipe_stream_switcher->selected)
        upipe_stream_switcher->selected = upipe;

    return upipe;
}

static void upipe_stream_switcher_input_free(struct upipe *upipe)
{
    struct upipe_stream_switcher_input *upipe_stream_switcher_input =
        upipe_stream_switcher_input_from_upipe(upipe);

    upipe_release(upipe_stream_switcher_input->super);

    upipe_throw_dead(upipe);

    upipe_stream_switcher_input_clean_input(upipe);
    upipe_stream_switcher_input_clean_sub(upipe);
    upipe_stream_switcher_input_clean_urefcount(upipe);
    upipe_stream_switcher_input_free_void(upipe);
}

static int upipe_stream_switcher_input_control(struct upipe *upipe,
                                               int command,
                                               va_list args)
{
    struct upipe_mgr *upipe_mgr = upipe->mgr;
    assert(upipe_mgr);
    struct upipe_stream_switcher *upipe_stream_switcher =
        upipe_stream_switcher_from_sub_mgr(upipe_mgr);
    struct upipe *super = upipe_stream_switcher_to_upipe(upipe_stream_switcher);

    switch (command) {
    case UPIPE_REGISTER_REQUEST: {
        struct urequest *request = va_arg(args, struct urequest *);
        return upipe_throw_provide_request(upipe, request);
    }
    case UPIPE_UNREGISTER_REQUEST:
        return UBASE_ERR_NONE;
    case UPIPE_SUB_GET_SUPER: {
        struct upipe **p = va_arg(args, struct upipe **);
        return upipe_stream_switcher_input_get_super(upipe, p);
    }
    case UPIPE_GET_MAX_LENGTH: {
        unsigned int *p = va_arg(args, unsigned int *);
        return upipe_stream_switcher_input_get_max_length(upipe, p);
    }
    case UPIPE_SET_MAX_LENGTH: {
        unsigned int max_length = va_arg(args, unsigned int);
        return upipe_stream_switcher_input_set_max_length(upipe, max_length);
    }
    case UPIPE_SET_FLOW_DEF: {
        struct uref *uref = va_arg(args, struct uref *);
        return upipe_set_flow_def(super, uref);
    }
    default:
        return UBASE_ERR_UNHANDLED;
    }
}

/*
 * drop uref
 */
static bool upipe_stream_switcher_drop(struct uref *uref)
{
    uref_free(uref);
    return true;
}

/*
 * forward uref to the super pipe
 */
static bool upipe_stream_switcher_fwd(struct upipe *upipe,
                                      struct uref *uref,
                                      struct upump **upump_p)
{
    struct upipe_mgr *upipe_mgr = upipe->mgr;
    assert(upipe_mgr);
    struct upipe_stream_switcher *upipe_stream_switcher =
        upipe_stream_switcher_from_sub_mgr(upipe_mgr);
    struct upipe *super = upipe_stream_switcher_to_upipe(upipe_stream_switcher);
    uint64_t dts_prog = 0, dts_orig = 0;

    assert(ubase_check(uref_clock_get_dts_orig(uref, &dts_orig)));
    if (!ubase_check(uref_clock_get_dts_prog(uref, &dts_prog)))
        dts_prog = 0;
    if (!upipe_stream_switcher->rebase_timestamp_set) {
        upipe_stream_switcher->rebase_timestamp_set = true;
        upipe_stream_switcher->rebase_timestamp = dts_orig;
    }
    assert(upipe_stream_switcher->rebase_timestamp <= dts_orig);
    dts_orig -= upipe_stream_switcher->rebase_timestamp;
    upipe_verbose_va(upipe, "DTS rebase %"PRIu64"(%"PRIu64"ms) "
                     "-> %"PRIu64" (%"PRIu64"ms)",
                     dts_prog, dts_prog / 27000, dts_orig, dts_orig / 27000);
    uref_clock_set_dts_prog(uref, dts_orig);

    upipe_stream_switcher_output(super, uref, upump_p);
    return true;
}

/*
 * set the upipe as waiting, return false to save the uref
 */
static bool upipe_stream_switcher_wait(struct upipe_stream_switcher *super,
                                       struct upipe *upipe, struct uref *uref)
{
    /* waiting pipe is already set ? */
    if (super->waiting) {
        assert(super->waiting == upipe);
        return false;
    }

    uint64_t pts_orig = 0;
    if (!ubase_check(uref_clock_get_pts_orig(uref, &pts_orig))) {
        /* fail to get pts, dropping... */
        upipe_warn(upipe, "fail to get pts");
        return upipe_stream_switcher_drop(uref);
    }
    upipe_dbg_va(upipe, "found a key frame at %"PRIu64, pts_orig);
    super->waiting = upipe;
    super->pts_orig = pts_orig;
    return false;
}

static void upipe_stream_switcher_switch(struct upipe_stream_switcher *super,
                                         struct upipe *upipe,
                                         struct uref *uref)
{
    struct upipe_stream_switcher_input *upipe_stream_switcher_input =
        upipe_stream_switcher_input_from_upipe(upipe);

    upipe_notice_va(upipe, "switching streams at %"PRIu64, super->pts_orig);

    /* switch the old stream with the new one */
    super->selected = super->waiting;
    super->waiting = NULL;

    /* wake up the new one */
    upipe_stream_switcher_input_output_input(super->selected);
    upipe_stream_switcher_input_unblock_input(super->selected);

    /* destroy the old one */
    upipe_stream_switcher_input->destroyed = true;
    upipe_throw_sink_end(upipe);
}

static bool upipe_stream_switcher_input_output(struct upipe *upipe,
                                               struct uref *uref,
                                               struct upump **upump_p)
{
    struct upipe_stream_switcher_input *upipe_stream_switcher_input =
        upipe_stream_switcher_input_from_upipe(upipe);
    struct upipe_mgr *upipe_mgr = upipe->mgr;
    assert(upipe_mgr);
    struct upipe_stream_switcher *upipe_stream_switcher =
        upipe_stream_switcher_from_sub_mgr(upipe_mgr);

    if (upipe_stream_switcher_input->destroyed)
        /* previous stream, drop */
        return upipe_stream_switcher_drop(uref);

    if (upipe_stream_switcher->selected == upipe) {
        /* current selected stream */

        if (!upipe_stream_switcher->waiting)
            /* no waiting stream, forward */
            return upipe_stream_switcher_fwd(upipe, uref, upump_p);

        uint64_t pts_orig = 0;
        if (!ubase_check(uref_clock_get_pts_orig(uref, &pts_orig))) {
            /* fail to get pts, dropping... */
            upipe_warn(upipe, "fail to get pts");
            return upipe_stream_switcher_drop(uref);
        }

        if (pts_orig < upipe_stream_switcher->pts_orig)
            /* previous frame, forward */
            return upipe_stream_switcher_fwd(upipe, uref, upump_p);

        /* the selected stream meet the waiting stream, switch */
        upipe_stream_switcher_switch(upipe_stream_switcher, upipe, uref);
        /* drop */
    }
    else if (upipe_stream_switcher->waiting) {
        /* a stream is waiting for switch */

        if (upipe_stream_switcher->waiting == upipe)
            /* this is me, wait */
            return upipe_stream_switcher_wait(upipe_stream_switcher,
                                              upipe, uref);

        /* not me, drop */
    }
    else {
        /* nobody is waiting */
        const char *flow_def = "(none)";
        uref_flow_get_def(upipe_stream_switcher->flow_def, &flow_def);

        if (strstr(flow_def, ".pic.")) {
            if (ubase_check(uref_pic_get_key(uref)))
                /* key frame found, wait */
                return upipe_stream_switcher_wait(upipe_stream_switcher,
                                                  upipe, uref);
        }
        else if (strstr(flow_def, ".sound.")) {
            return upipe_stream_switcher_wait(upipe_stream_switcher,
                                              upipe, uref);
        }

        /* not a key frame, drop */
    }
    return upipe_stream_switcher_drop(uref);
}

static void upipe_stream_switcher_input_input(struct upipe *upipe,
                                              struct uref *uref,
                                              struct upump **upump_p)
{
    if (!upipe_stream_switcher_input_output(upipe, uref, upump_p)) {
        upipe_stream_switcher_input_hold_input(upipe, uref);
        upipe_stream_switcher_input_block_input(upipe, upump_p);
    }
}

static void upipe_stream_switcher_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_stream_switcher *upipe_stream_switcher =
        upipe_stream_switcher_from_upipe(upipe);
    struct upipe_mgr *sub_mgr =
        upipe_stream_switcher_to_sub_mgr(upipe_stream_switcher);

    memset(sub_mgr, 0, sizeof (*sub_mgr));
    sub_mgr->signature = UPIPE_STREAM_SWITCHER_SUB_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_stream_switcher_input_alloc;
    sub_mgr->upipe_control = upipe_stream_switcher_input_control;
    sub_mgr->upipe_input = upipe_stream_switcher_input_input;
}

/*
 * super pipe
 */

static struct upipe *upipe_stream_switcher_alloc(struct upipe_mgr *mgr,
                                                 struct uprobe *uprobe,
                                                 uint32_t signature,
                                                 va_list args)
{
    struct upipe *upipe =
        upipe_stream_switcher_alloc_void(mgr, uprobe, signature, args);
    upipe_stream_switcher_init_urefcount(upipe);
    upipe_stream_switcher_init_output(upipe);
    upipe_stream_switcher_init_sub_inputs(upipe);
    upipe_stream_switcher_init_sub_mgr(upipe);

    struct upipe_stream_switcher *upipe_stream_switcher =
        upipe_stream_switcher_from_upipe(upipe);
    upipe_stream_switcher->selected = NULL;
    upipe_stream_switcher->waiting = NULL;
    upipe_stream_switcher->pts_orig = 0;
    upipe_stream_switcher->rebase_timestamp_set = false;
    upipe_stream_switcher->rebase_timestamp = 0;

    upipe_throw_ready(upipe);

    return upipe;
}

static void upipe_stream_switcher_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_stream_switcher_clean_sub_inputs(upipe);
    upipe_stream_switcher_clean_output(upipe);
    upipe_stream_switcher_clean_urefcount(upipe);
    upipe_stream_switcher_free_void(upipe);
}

static int upipe_stream_switcher_set_flow_def(struct upipe *upipe,
                                              struct uref *flow_def)
{
    struct upipe_stream_switcher *upipe_stream_switcher =
        upipe_stream_switcher_from_upipe(upipe);

    if (upipe_stream_switcher->flow_def == NULL) {
        struct uref *flow_def_dup = uref_dup(flow_def);
        if (unlikely(flow_def_dup == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }

        upipe_stream_switcher_store_flow_def(upipe, flow_def_dup);
    }
    else if (uref_flow_cmp_def(upipe_stream_switcher->flow_def, flow_def))
        return UBASE_ERR_INVALID;

    return UBASE_ERR_NONE;
}

static int upipe_stream_switcher_control(struct upipe *upipe,
                                         int command, va_list args)
{
    switch (command) {
    case UPIPE_REGISTER_REQUEST: {
        struct urequest *request = va_arg(args, struct urequest *);
        return upipe_throw_provide_request(upipe, request);
    }
    case UPIPE_UNREGISTER_REQUEST:
        return UBASE_ERR_NONE;

    case UPIPE_GET_FLOW_DEF: {
        struct uref **p = va_arg(args, struct uref **);
        return upipe_stream_switcher_get_flow_def(upipe, p);
    }
    case UPIPE_SET_FLOW_DEF: {
        struct uref *flow_def = va_arg(args, struct uref *);
        return upipe_stream_switcher_set_flow_def(upipe, flow_def);
    }

    case UPIPE_GET_OUTPUT: {
        struct upipe **p = va_arg(args, struct upipe **);
        return upipe_stream_switcher_get_output(upipe, p);
    }
    case UPIPE_SET_OUTPUT: {
        struct upipe *output = va_arg(args, struct upipe *);
        return upipe_stream_switcher_set_output(upipe, output);
    }

    case UPIPE_GET_SUB_MGR: {
        struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);
        return upipe_stream_switcher_get_sub_mgr(upipe, p);
    }
    case UPIPE_ITERATE_SUB: {
        struct upipe **p = va_arg(args, struct upipe **);
        return upipe_stream_switcher_iterate_sub(upipe, p);
    }

    default:
        return UBASE_ERR_UNHANDLED;
    }
}

struct upipe_mgr upipe_stream_switcher_mgr = {
    .signature = UPIPE_STREAM_SWITCHER_SIGNATURE,
    .upipe_alloc = upipe_stream_switcher_alloc,
    .upipe_control = upipe_stream_switcher_control,
};

struct upipe_mgr *upipe_stream_switcher_mgr_alloc(void)
{
    return &upipe_stream_switcher_mgr;
}
