/*
 * Copyright (C) 2025 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module to parse pcap files
 */

/* TODO:
    - avoid memcpy? custom ubuf_mgr to delay pcap_close?
    - filtering? pcap_setfilter() or something else
    - set uref source ip?
 */

#include <stdlib.h>

#include "upipe/upipe.h"
#include "upipe/upump.h"
#include "upipe/uref_block.h"
#include "upipe/uref_clock.h"
#include "upipe/uclock.h"
#include "upipe/uref_block_flow.h"
#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_void.h"
#include "upipe/upipe_helper_upump_mgr.h"
#include "upipe/upipe_helper_upump.h"
#include "upipe/upipe_helper_output.h"
#include "upipe/upipe_helper_uref_mgr.h"
#include "upipe/upipe_helper_ubuf_mgr.h"
#include "upipe/upipe_helper_uclock.h"
#include "upipe-pcap/upipe_pcap_src.h"

#include <pcap/pcap.h>

#include <bitstream/ietf/ip.h>
#include <bitstream/ietf/udp.h>
#include <bitstream/ieee/ethernet.h>

struct upipe_pcap_src {
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

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** read watcher */
    struct upump *upump;

    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** uref manager request */
    struct urequest uref_mgr_request;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** uclock structure, if not NULL we are in live mode */
    struct uclock *uclock;
    /** uclock request */
    struct urequest uclock_request;

    pcap_t *pcap;
    char errbuf[PCAP_ERRBUF_SIZE];
    uint64_t cr_offset;
    struct uref *uref;

    /** public upipe structure */
    struct upipe upipe;
};

/** @hidden */
static int upipe_pcap_src_check(struct upipe *upipe, struct uref *flow_format);

UPIPE_HELPER_UPIPE(upipe_pcap_src, upipe, UPIPE_PCAP_SRC_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_pcap_src, urefcount, upipe_pcap_src_free)
UPIPE_HELPER_VOID(upipe_pcap_src)
UPIPE_HELPER_OUTPUT(upipe_pcap_src, output, flow_def, output_state,
                    request_list)
UPIPE_HELPER_UREF_MGR(upipe_pcap_src, uref_mgr, uref_mgr_request, upipe_pcap_src_check,
                      upipe_pcap_src_register_output_request,
                      upipe_pcap_src_unregister_output_request)
UPIPE_HELPER_UBUF_MGR(upipe_pcap_src, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_pcap_src_check,
                      upipe_pcap_src_register_output_request,
                      upipe_pcap_src_unregister_output_request)
UPIPE_HELPER_UPUMP_MGR(upipe_pcap_src, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_pcap_src, upump, upump_mgr)
UPIPE_HELPER_UCLOCK(upipe_pcap_src, uclock, uclock_request, upipe_pcap_src_check,
                    upipe_pcap_src_register_output_request,
                    upipe_pcap_src_unregister_output_request)

/* Skip straight to UDP data */
static size_t upipe_pcap_skip(struct upipe *upipe, const uint8_t *buf, size_t len)
{
    if (len < ETHERNET_HEADER_LEN + ETHERNET_VLAN_LEN)
        return 0;

    const uint8_t *ip = ethernet_payload((uint8_t*)buf);

    len -= ip - buf;

    if (len < IP_HEADER_MINSIZE)
        return 0;

    if (len < 4 * ip_get_ihl(ip))
        return 0;

    if (ip_get_proto(ip) != IP_PROTO_UDP)
        return 0;

    len -= 4 * ip_get_ihl(ip);
    if (len < UDP_HEADER_SIZE)
        return 0;

    return len - UDP_HEADER_SIZE;
}

static void upipe_pcap_src_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_pcap_src *upipe_pcap_src = upipe_pcap_src_from_upipe(upipe);

    pcap_t *pcap = upipe_pcap_src->pcap;

    if (upipe_pcap_src->uref) {
        upipe_pcap_src_output(upipe, upipe_pcap_src->uref, &upipe_pcap_src->upump);
        upipe_pcap_src->uref = NULL;
    }

    struct pcap_pkthdr *hdr;
    const u_char *data;
    switch (pcap_next_ex(pcap, &hdr, &data)) {
    case 1: /* no problem */
        break;

    case PCAP_ERROR_BREAK:
        upipe_throw_source_end(upipe);
        upipe_pcap_src_set_upump(upipe, NULL);
        return;

    case PCAP_ERROR:
        upipe_err_va(upipe, "%s", pcap_geterr(pcap));
        return;

    default: /* 0, PCAP_ERROR_NOT_ACTIVATED, only for live capture */
        return;
    }

    size_t len = hdr->len;
    if (hdr->len != hdr->caplen) {
        upipe_warn_va(upipe, "Length captured (%d) is not packet len (%d)",
            hdr->caplen, hdr->len);
        if (len > hdr->caplen)
            len = hdr->caplen;
    }

    size_t udp_len = upipe_pcap_skip(upipe, data, len);
    assert(udp_len < len);
    if (udp_len == 0) {
        return;
    }

    struct uref *uref = uref_block_alloc(upipe_pcap_src->uref_mgr,
                                         upipe_pcap_src->ubuf_mgr,
                                         udp_len);
    if (unlikely(!uref)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    uint8_t *buffer;
    int output_size = -1;
    if (unlikely(!ubase_check(uref_block_write(uref, 0, &output_size,
                                               &buffer)))) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    memcpy(buffer, &data[len - udp_len], udp_len);

    uref_block_unmap(uref, 0);

    uint64_t ts = hdr->ts.tv_sec * UCLOCK_FREQ + hdr->ts.tv_usec * (UCLOCK_FREQ / 1000000);

    if (upipe_pcap_src->uclock) {
        uint64_t now = uclock_now(upipe_pcap_src->uclock);

        if (upipe_pcap_src->cr_offset == 0)
            upipe_pcap_src->cr_offset = now - ts;

        ts += upipe_pcap_src->cr_offset;

        uref_clock_set_cr_sys(uref, ts);
        upipe_pcap_src->uref = uref;

        uint64_t ticks = 0;
        if (now < ts)
            ticks = ts - now;

        upipe_pcap_src_wait_upump(upipe, ticks, upipe_pcap_src_worker);
        return;
    }

    uref_clock_set_cr_sys(uref, ts);

    upipe_pcap_src_output(upipe, uref, &upipe_pcap_src->upump);
}

static int upipe_pcap_src_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_pcap_src *upipe_pcap_src = upipe_pcap_src_from_upipe(upipe);

    if (flow_format)
        upipe_pcap_src_store_flow_def(upipe, flow_format);

    upipe_pcap_src_check_upump_mgr(upipe);
    if (!upipe_pcap_src->upump_mgr)
        return UBASE_ERR_NONE;

    if (!upipe_pcap_src->uref_mgr) {
        upipe_pcap_src_require_uref_mgr(upipe);
        return UBASE_ERR_NONE;
    }

    if (!upipe_pcap_src->ubuf_mgr) {
        struct uref *flow_format = uref_block_flow_alloc_def(upipe_pcap_src->uref_mgr, NULL);
        if (unlikely(!flow_format)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
        upipe_pcap_src_require_ubuf_mgr(upipe, flow_format);
        return UBASE_ERR_NONE;
    }

    if (upipe_pcap_src->pcap && !upipe_pcap_src->upump) {
        struct upump *upump;
            upump = upump_alloc_idler(upipe_pcap_src->upump_mgr,
                                      upipe_pcap_src_worker, upipe,
                                      upipe->refcount);
        if (unlikely(!upump)) {
            upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
            return UBASE_ERR_UPUMP;
        }
        upipe_pcap_src_set_upump(upipe, upump);
        upump_start(upump);
    }

    return UBASE_ERR_NONE;
}

static int upipe_pcap_set_uri(struct upipe *upipe, const char *uri)
{
    struct upipe_pcap_src *upipe_pcap_src = upipe_pcap_src_from_upipe(upipe);

    if (!uri) {
        if (upipe_pcap_src->pcap) {
            pcap_close(upipe_pcap_src->pcap);
            upipe_pcap_src_set_upump(upipe, NULL);
        }
        upipe_pcap_src->pcap = NULL;
        return UBASE_ERR_NONE;
    }

    upipe_pcap_src->pcap = pcap_open_offline(uri, upipe_pcap_src->errbuf);
    if (!upipe_pcap_src->pcap) {
        upipe_err_va(upipe, "%s: %s", uri, upipe_pcap_src->errbuf);
        return UBASE_ERR_UNHANDLED;
    }

    return UBASE_ERR_NONE;
}

static int _upipe_pcap_src_control(struct upipe *upipe, int command,
                                  va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_pcap_src_set_upump(upipe, NULL);
            return upipe_pcap_src_attach_upump_mgr(upipe);
        case UPIPE_ATTACH_UCLOCK:
            upipe_pcap_src_require_uclock(upipe);
            return UBASE_ERR_NONE;
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
        case UPIPE_GET_FLOW_DEF:
            return upipe_pcap_src_control_output(upipe, command, args);
        case UPIPE_SET_URI:
            return upipe_pcap_set_uri(upipe, va_arg(args, const char *));
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

static int upipe_pcap_src_control(struct upipe *upipe, int command,
                                  va_list args)
{
    UBASE_RETURN(_upipe_pcap_src_control(upipe, command, args))

    return upipe_pcap_src_check(upipe, NULL);
}

static void upipe_pcap_src_free(struct upipe *upipe)
{
    struct upipe_pcap_src *upipe_pcap_src = upipe_pcap_src_from_upipe(upipe);
    upipe_throw_dead(upipe);

    if (upipe_pcap_src->pcap)
        pcap_close(upipe_pcap_src->pcap);
    if (upipe_pcap_src->uref)
        uref_free(upipe_pcap_src->uref);

    upipe_pcap_src_clean_output(upipe);
    upipe_pcap_src_clean_urefcount(upipe);
    upipe_pcap_src_clean_ubuf_mgr(upipe);
    upipe_pcap_src_clean_uref_mgr(upipe);
    upipe_pcap_src_clean_upump_mgr(upipe);
    upipe_pcap_src_clean_upump(upipe);
    upipe_pcap_src_clean_uclock(upipe);
    upipe_pcap_src_free_void(upipe);
}

static struct upipe *upipe_pcap_src_alloc(struct upipe_mgr *mgr,
                                           struct uprobe *uprobe,
                                           uint32_t signature,
                                           va_list args)
{
    struct upipe *upipe =
        upipe_pcap_src_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(!upipe))
        return NULL;

    struct upipe_pcap_src *upipe_pcap_src = upipe_pcap_src_from_upipe(upipe);
    upipe_pcap_src->pcap = NULL;
    upipe_pcap_src->uref = NULL;
    upipe_pcap_src->cr_offset = 0;

    upipe_pcap_src_init_urefcount(upipe);
    upipe_pcap_src_init_ubuf_mgr(upipe);
    upipe_pcap_src_init_uref_mgr(upipe);
    upipe_pcap_src_init_output(upipe);
    upipe_pcap_src_init_upump_mgr(upipe);
    upipe_pcap_src_init_upump(upipe);
    upipe_pcap_src_init_uclock(upipe);

    upipe_throw_ready(upipe);

    return upipe;
}

static struct upipe_mgr upipe_pcap_src_mgr = {
    .refcount = NULL,
    .signature = UPIPE_PCAP_SRC_SIGNATURE,

    .upipe_alloc = upipe_pcap_src_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_pcap_src_control,

    .upipe_mgr_control = NULL,
};

struct upipe_mgr *upipe_pcap_src_mgr_alloc(void)
{
    return &upipe_pcap_src_mgr;
}
