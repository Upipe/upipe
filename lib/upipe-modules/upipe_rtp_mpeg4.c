#include <stdlib.h>

#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_uref_stream.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe-modules/upipe_rtp_mpeg4.h>

struct upipe_rtp_mpeg4 {
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

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_rtp_mpeg4, upipe, UPIPE_RTP_MPEG4_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_rtp_mpeg4, urefcount, upipe_rtp_mpeg4_free)
UPIPE_HELPER_VOID(upipe_rtp_mpeg4)
UPIPE_HELPER_OUTPUT(upipe_rtp_mpeg4, output, flow_def, output_state,
                    request_list)

static int upipe_rtp_mpeg4_set_flow_def(struct upipe *upipe,
                                       struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, "block.aac.sound."))

    struct uref *flow_def_dup = uref_dup(flow_def);
    if (unlikely(flow_def_dup == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    upipe_rtp_mpeg4_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

static int upipe_rtp_mpeg4_control(struct upipe *upipe, int command,
                                  va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_rtp_mpeg4_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_rtp_mpeg4_free_output_proxy(upipe, request);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_rtp_mpeg4_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_rtp_mpeg4_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_rtp_mpeg4_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_rtp_mpeg4_set_output(upipe, output);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

static void upipe_rtp_mpeg4_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_rtp_mpeg4_clean_output(upipe);
    upipe_rtp_mpeg4_clean_urefcount(upipe);
    upipe_rtp_mpeg4_free_void(upipe);
}

static struct upipe *upipe_rtp_mpeg4_alloc(struct upipe_mgr *mgr,
                                           struct uprobe *uprobe,
                                           uint32_t signature,
                                           va_list args)
{
    struct upipe *upipe =
        upipe_rtp_mpeg4_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_rtp_mpeg4_init_urefcount(upipe);
    upipe_rtp_mpeg4_init_output(upipe);

    upipe_throw_ready(upipe);

    return upipe;
}

static void upipe_rtp_mpeg4_input(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p)
{
    size_t block_size = 0;
    if (!ubase_check(uref_block_size(uref, &block_size))) {
        upipe_warn(upipe, "fail to get uref block size");
        uref_free(uref);
        return;
    }
    if (block_size <= 7) {
        upipe_warn(upipe, "invalid packet");
        uref_free(uref);
        return;
    }
    block_size = (block_size - 7) << 3;

    struct ubuf *au = ubuf_block_alloc(uref->ubuf->mgr, 4);
    if (unlikely(au == NULL)) {
        uref_free(uref);
        return;
    }

    int size = -1;
    uint8_t *buf;
    ubuf_block_write(au, 0, &size, &buf);
    buf[0] = 0;
    buf[1] = 16;
    buf[2] = block_size >> 8;
    buf[3] = block_size;
    ubuf_block_unmap(au, 0);

    if (!ubase_check(uref_block_truncate(uref, 7))) {
        upipe_err(upipe, "could not truncate uref");
        uref_free(uref);
        return;
    }

    struct ubuf *tmp = uref_detach_ubuf(uref);
    if (unlikely(!ubase_check(ubuf_block_append(au, tmp)))) {
        upipe_warn(upipe, "could not append payload to header");
        ubuf_free(au);
        ubuf_free(tmp);
        uref_free(uref);
        return;
    }
    uref_attach_ubuf(uref, au);
    uref_clock_set_cr_dts_delay(uref, 0);
    upipe_rtp_mpeg4_output(upipe, uref, upump_p);
}

static struct upipe_mgr upipe_rtp_mpeg4_mgr = {
    .refcount = NULL,
    .signature = UPIPE_RTP_MPEG4_SIGNATURE,

    .upipe_alloc = upipe_rtp_mpeg4_alloc,
    .upipe_input = upipe_rtp_mpeg4_input,
    .upipe_control = upipe_rtp_mpeg4_control,

    .upipe_mgr_control = NULL,
};

struct upipe_mgr *upipe_rtp_mpeg4_mgr_alloc(void)
{
    return &upipe_rtp_mpeg4_mgr;
}
