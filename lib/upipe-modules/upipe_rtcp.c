/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * Authors: Arnaud de Turckheim
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

#include <stdlib.h>

#include <upipe/uclock.h>
#include <upipe/upipe.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/urequest.h>
#include <upipe/upump.h>
#include <upipe/ustring.h>
#include <upipe/uref_clock.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe-modules/upipe_rtcp.h>

#include <bitstream/ietf/rtp.h>
#include <bitstream/ietf/rtcp_sr.h>
#include <bitstream/ietf/rtcp_sdes.h>

struct upipe_rtcp {
    /** refcount management structure */
    struct urefcount urefcount;

    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** uref manager request */
    struct urequest uref_mgr_request;

    /** pipe acting as output */
    struct upipe *output;
    /** flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** ubuf mgr structures */
    struct ubuf_mgr *ubuf_mgr;
    struct urequest ubuf_mgr_request;
    /** ubuf flow format */
    struct uref *flow_format;

    uint32_t clockrate;
    uint32_t packet_count;
    uint32_t octet_count;
    uint64_t rate;
    uint64_t last_sent;
    char *name;

    /** public upipe structure */
    struct upipe upipe;
};

static int upipe_rtcp_check(struct upipe *upipe, struct uref *flow_format);

UPIPE_HELPER_UPIPE(upipe_rtcp, upipe, UPIPE_RTCP_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_rtcp, urefcount, upipe_rtcp_free)
UPIPE_HELPER_VOID(upipe_rtcp)
UPIPE_HELPER_OUTPUT(upipe_rtcp, output, flow_def, output_state,
                    request_list)
UPIPE_HELPER_UREF_MGR(upipe_rtcp, uref_mgr, uref_mgr_request,
                      upipe_rtcp_check,
                      upipe_rtcp_register_output_request,
                      upipe_rtcp_unregister_output_request)
UPIPE_HELPER_UBUF_MGR(upipe_rtcp, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_rtcp_check,
                      upipe_rtcp_register_output_request,
                      upipe_rtcp_unregister_output_request)

static int upipe_rtcp_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_rtcp *upipe_rtcp = upipe_rtcp_from_upipe(upipe);

    if (flow_format != NULL)
        upipe_rtcp_store_flow_def(upipe, flow_format);

    if (upipe_rtcp->flow_def == NULL)
        return UBASE_ERR_NONE;

    if (upipe_rtcp->uref_mgr == NULL) {
        upipe_rtcp_require_uref_mgr(upipe);
        return UBASE_ERR_NONE;
    }

    if (upipe_rtcp->ubuf_mgr == NULL) {
        struct uref *flow_format =
            uref_block_flow_alloc_def(upipe_rtcp->uref_mgr, NULL);
        if (unlikely(flow_format == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
        upipe_rtcp_require_ubuf_mgr(upipe, flow_format);
        return UBASE_ERR_NONE;
    }

    return UBASE_ERR_NONE;
}

static void upipe_rtcp_free(struct upipe *upipe)
{
    struct upipe_rtcp *upipe_rtcp = upipe_rtcp_from_upipe(upipe);

    upipe_throw_dead(upipe);

    free(upipe_rtcp->name);

    upipe_rtcp_clean_ubuf_mgr(upipe);
    upipe_rtcp_clean_uref_mgr(upipe);
    upipe_rtcp_clean_output(upipe);
    upipe_rtcp_clean_urefcount(upipe);
    upipe_rtcp_free_void(upipe);
}

static struct upipe *upipe_rtcp_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature,
                                      va_list args)
{
    struct upipe *upipe = upipe_rtcp_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_rtcp_init_urefcount(upipe);
    upipe_rtcp_init_output(upipe);
    upipe_rtcp_init_uref_mgr(upipe);
    upipe_rtcp_init_ubuf_mgr(upipe);

    struct upipe_rtcp *upipe_rtcp = upipe_rtcp_from_upipe(upipe);
    upipe_rtcp->packet_count = 0;
    upipe_rtcp->octet_count = 0;
    upipe_rtcp->clockrate = 0;
    upipe_rtcp->rate = UCLOCK_FREQ;
    upipe_rtcp->last_sent = 0;
    upipe_rtcp->name = NULL;
    upipe_throw_ready(upipe);

    return upipe;
}

static int upipe_rtcp_set_flow_def(struct upipe *upipe,
                                   struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    struct uref *flow_def_dup = uref_dup(flow_def);
    if (unlikely(flow_def_dup == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    upipe_rtcp_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

static int _upipe_rtcp_get_clockrate(struct upipe *upipe,
                                     uint32_t *clockrate)
{
    struct upipe_rtcp *upipe_rtcp = upipe_rtcp_from_upipe(upipe);
    *clockrate = upipe_rtcp->clockrate;
    return UBASE_ERR_NONE;
}

static int _upipe_rtcp_set_clockrate(struct upipe *upipe,
                                     uint32_t clockrate)
{
    struct upipe_rtcp *upipe_rtcp = upipe_rtcp_from_upipe(upipe);
    upipe_rtcp->clockrate = clockrate;
    return UBASE_ERR_NONE;
}

static int _upipe_rtcp_get_rate(struct upipe *upipe,
                                uint64_t *rate)
{
    struct upipe_rtcp *upipe_rtcp = upipe_rtcp_from_upipe(upipe);
    *rate = upipe_rtcp->rate;
    return UBASE_ERR_NONE;
}

static int _upipe_rtcp_set_rate(struct upipe *upipe, uint64_t rate)
{
    struct upipe_rtcp *upipe_rtcp = upipe_rtcp_from_upipe(upipe);
    upipe_rtcp->rate = rate;
    return UBASE_ERR_NONE;
}

static int _upipe_rtcp_get_name(struct upipe *upipe, const char **name_p)
{
    struct upipe_rtcp *upipe_rtcp = upipe_rtcp_from_upipe(upipe);
    *name_p = upipe_rtcp->name;
    return UBASE_ERR_NONE;
}

static int _upipe_rtcp_set_name(struct upipe *upipe, const char *name)
{
    struct upipe_rtcp *upipe_rtcp = upipe_rtcp_from_upipe(upipe);
    free(upipe_rtcp->name);
    upipe_rtcp->name = name ? strdup(name) : NULL;
    return UBASE_ERR_NONE;
}

static int upipe_rtcp_control_handle(struct upipe *upipe, int command,
                                     va_list args)
{
    UBASE_HANDLED_RETURN(upipe_rtcp_control_output(upipe, command, args));
    switch (command) {
    case UPIPE_SET_FLOW_DEF: {
        struct uref *p = va_arg(args, struct uref *);
        return upipe_rtcp_set_flow_def(upipe, p);
    }

    case UPIPE_RTCP_GET_CLOCKRATE: {
        unsigned int signature = va_arg(args, unsigned int);
        assert(signature == UPIPE_RTCP_SIGNATURE);
        uint32_t *rate_p = va_arg(args, uint32_t *);
        return _upipe_rtcp_get_clockrate(upipe, rate_p);
    }
    case UPIPE_RTCP_SET_CLOCKRATE: {
        unsigned int signature = va_arg(args, unsigned int);
        assert(signature == UPIPE_RTCP_SIGNATURE);
        uint32_t rate = va_arg(args, uint32_t);
        return _upipe_rtcp_set_clockrate(upipe, rate);
    }

    case UPIPE_RTCP_GET_RATE: {
        unsigned int signature = va_arg(args, unsigned int);
        assert(signature == UPIPE_RTCP_SIGNATURE);
        uint64_t *rate_p = va_arg(args, uint64_t *);
        return _upipe_rtcp_get_rate(upipe, rate_p);
    }
    case UPIPE_RTCP_SET_RATE: {
        unsigned int signature = va_arg(args, unsigned int);
        assert(signature == UPIPE_RTCP_SIGNATURE);
        uint64_t rate = va_arg(args, uint64_t);
        return _upipe_rtcp_set_rate(upipe, rate);
    }

    case UPIPE_RTCP_GET_NAME: {
        unsigned int signature = va_arg(args, unsigned int);
        assert(signature == UPIPE_RTCP_SIGNATURE);
        const char **name_p = va_arg(args, const char **);
        return _upipe_rtcp_get_name(upipe, name_p);
    }
    case UPIPE_RTCP_SET_NAME: {
        unsigned int signature = va_arg(args, unsigned int);
        assert(signature == UPIPE_RTCP_SIGNATURE);
        const char *name = va_arg(args, const char *);
        return _upipe_rtcp_set_name(upipe, name);
    }

    default:
        return UBASE_ERR_UNHANDLED;
    }
}

static int upipe_rtcp_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(upipe_rtcp_control_handle(upipe, command, args))
    return upipe_rtcp_check(upipe, NULL);
}

static void upipe_rtcp_send_sr(struct upipe *upipe, struct upump **upump_p,
                                uint64_t cr, uint64_t cr_prog)
{
    struct upipe_rtcp *upipe_rtcp = upipe_rtcp_from_upipe(upipe);

    lldiv_t div = lldiv(cr, UCLOCK_FREQ);
    uint64_t ntp_time =  ((uint64_t)div.quot << 32) +
        ((uint64_t)div.rem << 32) / UCLOCK_FREQ;

    div = lldiv(cr_prog, UCLOCK_FREQ);
    uint32_t ts = div.quot * upipe_rtcp->clockrate
         + ((uint64_t)div.rem * upipe_rtcp->clockrate)/UCLOCK_FREQ;

    struct ustring name = ustring_from_str(upipe_rtcp->name);

    int sdes_size = RTCP_SDES_SIZE + name.len + 1; /* 0 byte */
    sdes_size = (sdes_size + 3) & ~3; /* align to 32 bits */

    int size = RTCP_SR_SIZE + sdes_size;
    struct uref *pkt = uref_block_alloc(upipe_rtcp->uref_mgr,
                                        upipe_rtcp->ubuf_mgr, size);
    if (unlikely(pkt == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    uint8_t *buf;
    uref_block_write(pkt, 0, &size, &buf);
    memset(buf, 0, size);

    rtcp_sr_set_rtp_version(buf);
    rtcp_sr_set_pt(buf);
    rtcp_sr_set_length(buf, 6);
    rtcp_sr_set_ntp_time_msw(buf, ntp_time >> 32);
    rtcp_sr_set_ntp_time_lsw(buf, ntp_time);
    rtcp_sr_set_rtp_time(buf, ts);
    rtcp_sr_set_packet_count(buf, upipe_rtcp->packet_count);
    rtcp_sr_set_octet_count(buf, upipe_rtcp->octet_count);

    uint8_t *sdes = &buf[RTCP_SR_SIZE];
    rtcp_set_rtp_version(sdes);
    rtp_set_cc(sdes, 1);
    rtcp_set_length(sdes, (sdes_size / 4) - 1);
    rtcp_sdes_set_pt(sdes);
    rtcp_sdes_set_cname(sdes, 1);
    rtcp_sdes_set_name_length(sdes, name.len);
    if (!ustring_is_null(name))
        memcpy(&sdes[RTCP_SDES_SIZE], name.at, name.len);

    uref_block_unmap(pkt, 0);

    uref_clock_set_date_sys(pkt, cr, UREF_DATE_CR);
    upipe_rtcp_output(upipe, pkt, upump_p);
}

static void upipe_rtcp_input(struct upipe *upipe, struct uref *uref,
                             struct upump **upump_p)
{
    struct upipe_rtcp *upipe_rtcp = upipe_rtcp_from_upipe(upipe);

    size_t block_size = 0;
    if (unlikely(!ubase_check(uref_block_size(uref, &block_size)))) {
        upipe_err(upipe, "fail to get uref block size");
        uref_free(uref);
        return;
    }
    upipe_rtcp->packet_count++;
    upipe_rtcp->octet_count += block_size;

    uint64_t cr = 0;
    if (unlikely(!ubase_check(uref_clock_get_cr_sys(uref, &cr)))) {
        upipe_warn_va(upipe, "non-dated buffer %p", uref);
        uref_free(uref);
        return;
    }

    if (upipe_rtcp->last_sent == 0)
        upipe_rtcp->last_sent = cr;

    if ((cr - upipe_rtcp->last_sent) < upipe_rtcp->rate) {
        uref_free(uref);
        return;
    }

    upipe_rtcp->last_sent = cr;

    /* timestamp (synced to program clock ref, fallback to system clock ref) */
    uint64_t cr_prog = 0;
    if (unlikely(!ubase_check(uref_clock_get_pts_prog(uref, &cr_prog)))) {
        uref_clock_get_pts_sys(uref, &cr_prog);
    }

    uref_free(uref);
    upipe_rtcp_send_sr(upipe, upump_p, cr, cr_prog);
}

static struct upipe_mgr upipe_rtcp_mgr = {
    .refcount = NULL,
    .signature = UPIPE_RTCP_SIGNATURE,

    .upipe_alloc = upipe_rtcp_alloc,
    .upipe_input = upipe_rtcp_input,
    .upipe_control = upipe_rtcp_control,
    .upipe_mgr_control = NULL,
};

struct upipe_mgr *upipe_rtcp_mgr_alloc(void)
{
    return &upipe_rtcp_mgr;
}
