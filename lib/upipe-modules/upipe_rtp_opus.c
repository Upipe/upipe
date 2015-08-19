#include <stdlib.h>
#include <limits.h>

#include <upipe/upipe.h>
#include <upipe/uclock.h>
#include <upipe/uref_clock.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_uref_stream.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe-modules/upipe_rtp_opus.h>
#include <upipe-modules/upipe_rtp_decaps.h>
#include <upipe/uref_dump.h>

#define TS_MULTIPLIER (UCLOCK_FREQ/48000)

struct upipe_rtp_opus {
    /** refcount management structure */
    struct urefcount urefcount;

    /* output stuff */
    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** last RTP timestamp */
    uint64_t last_rtp_timestamp;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_rtp_opus, upipe, UPIPE_RTP_OPUS_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_rtp_opus, urefcount, upipe_rtp_opus_free)
UPIPE_HELPER_VOID(upipe_rtp_opus)
UPIPE_HELPER_OUTPUT(upipe_rtp_opus, output, flow_def, output_state,
                    request_list)

static int upipe_rtp_opus_set_flow_def(struct upipe *upipe,
                                       struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    //UBASE_RETURN(uref_flow_match_def(flow_def, "block.opus.sound."))

    struct uref *flow_def_dup = uref_dup(flow_def);
    if (unlikely(flow_def_dup == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    uref_flow_set_def(flow_def_dup, "block.opus.sound.");

    upipe_rtp_opus_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

static int upipe_rtp_opus_control(struct upipe *upipe, int command,
                                  va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_rtp_opus_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_rtp_opus_free_output_proxy(upipe, request);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_rtp_opus_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_rtp_opus_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_rtp_opus_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_rtp_opus_set_output(upipe, output);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

static void upipe_rtp_opus_free(struct upipe *upipe)
{
    upipe_rtp_opus_clean_output(upipe);
    upipe_rtp_opus_clean_urefcount(upipe);
    upipe_rtp_opus_free_void(upipe);
}

static struct upipe *upipe_rtp_opus_alloc(struct upipe_mgr *mgr,
                                           struct uprobe *uprobe,
                                           uint32_t signature,
                                           va_list args)
{
    struct upipe *upipe =
        upipe_rtp_opus_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;
    struct upipe_rtp_opus *upipe_rtp_opus = upipe_rtp_opus_from_upipe(upipe);

    upipe_rtp_opus_init_urefcount(upipe);
    upipe_rtp_opus_init_output(upipe);

    upipe_rtp_opus->last_rtp_timestamp = UINT_MAX;

    return upipe;
}

static void upipe_rtp_opus_input(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p)
{
    struct upipe_rtp_opus *upipe_rtp_opus = upipe_rtp_opus_from_upipe(upipe);

    uint64_t timestamp = 0;
    size_t block_size = 0;
    if (!ubase_check(uref_block_size(uref, &block_size))) {
        upipe_warn(upipe, "fail to get uref block size");
        return;
    }

    uref_rtp_get_timestamp(uref, &timestamp);
    uref_clock_set_pts_orig(uref, timestamp * TS_MULTIPLIER);

    uint64_t delta =
        (UINT_MAX + timestamp -
         (upipe_rtp_opus->last_rtp_timestamp % UINT_MAX)) % UINT_MAX;
    upipe_rtp_opus->last_rtp_timestamp += delta;
    uref_clock_set_pts_prog(uref, upipe_rtp_opus->last_rtp_timestamp * TS_MULTIPLIER);

    upipe_throw_clock_ref(upipe, uref, upipe_rtp_opus->last_rtp_timestamp * TS_MULTIPLIER, 0);
    upipe_throw_clock_ts(upipe, uref);

    upipe_rtp_opus_output(upipe, uref, upump_p);
}

static struct upipe_mgr upipe_rtp_opus_mgr = {
    .refcount = NULL,
    .signature = UPIPE_RTP_OPUS_SIGNATURE,

    .upipe_alloc = upipe_rtp_opus_alloc,
    .upipe_input = upipe_rtp_opus_input,
    .upipe_control = upipe_rtp_opus_control,

    .upipe_mgr_control = NULL,
};

struct upipe_mgr *upipe_rtp_opus_mgr_alloc(void)
{
    return &upipe_rtp_opus_mgr;
}
