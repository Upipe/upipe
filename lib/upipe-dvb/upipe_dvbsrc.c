/*
 * Copyright (C) 2017-2018 Open Broadcast Systems Ltd
 *
 * Author: Rafaël Carré
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe source module for DVB receivers
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/upump.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>

#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_uclock.h>

#include <upipe-dvb/upipe_dvbsrc.h>

#include <libdvbv5/dvb-dev.h>
#include <libdvbv5/dvb-fe.h>
#include <libdvbv5/dvb-demux.h>

#include <ctype.h>

#define MTU (7 * 188)

/** @hidden */
static int upipe_dvbsrc_check(struct upipe *upipe, struct uref *flow_format);

/** @internal @This is the private context of a DVB receiver source pipe. */
struct upipe_dvbsrc {
    /** refcount management structure */
    struct urefcount urefcount;

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

    /** pipe acting as output */
    struct upipe *output;
    /** flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** read watcher */
    struct upump *upump;

    /** DVB receiver uri */
    char *uri;

    /** DVB device */
    struct dvb_device *dvb;

    /** DVB demux */
    struct dvb_open_descriptor *demux;

    /** DVB frontend */
    struct dvb_open_descriptor *frontend;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_dvbsrc, upipe, UPIPE_DVBSRC_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_dvbsrc, urefcount, upipe_dvbsrc_free)
UPIPE_HELPER_VOID(upipe_dvbsrc)

UPIPE_HELPER_OUTPUT(upipe_dvbsrc, output, flow_def, output_state, request_list)
UPIPE_HELPER_UREF_MGR(upipe_dvbsrc, uref_mgr, uref_mgr_request,
                      upipe_dvbsrc_check,
                      upipe_dvbsrc_register_output_request,
                      upipe_dvbsrc_unregister_output_request)
UPIPE_HELPER_UBUF_MGR(upipe_dvbsrc, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_dvbsrc_check,
                      upipe_dvbsrc_register_output_request,
                      upipe_dvbsrc_unregister_output_request)
UPIPE_HELPER_UCLOCK(upipe_dvbsrc, uclock, uclock_request, upipe_dvbsrc_check,
                    upipe_dvbsrc_register_output_request,
                    upipe_dvbsrc_unregister_output_request)

UPIPE_HELPER_UPUMP_MGR(upipe_dvbsrc, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_dvbsrc, upump, upump_mgr)

static void upipe_dvbsrc_log(void *priv, int level, const char *fmt,...)
{
    int loglevel;
    switch (level) {
    case LOG_WARNING:
    case LOG_ERR:
    default:
        loglevel = UPROBE_LOG_WARNING;
        break;
    case LOG_NOTICE:
        loglevel = UPROBE_LOG_NOTICE;
        break;
    case LOG_DEBUG:
    case LOG_INFO:
        loglevel = UPROBE_LOG_DEBUG;
        break;
        break;
    }

    struct uprobe *uprobe = priv;

    va_list args;
    va_start(args, fmt);

    size_t len = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (len > 0) {
        char str[len + 1];
        va_start(args, fmt);
        vsnprintf(str, len + 1, fmt, args);
        va_end(args);
        char *end = str + len - 1;
        if (isspace(*end)) {
            *end = '\0';
        }
        uprobe_log(uprobe, NULL, loglevel, str);
    }
}

/** @internal @This allocates a DVB receiver source pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_dvbsrc_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_dvbsrc_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_dvbsrc *upipe_dvbsrc = upipe_dvbsrc_from_upipe(upipe);

    upipe_dvbsrc->dvb = dvb_dev_alloc();
    if (!upipe_dvbsrc->dvb) {
        upipe_err(upipe, "Can't allocate DVB device");
        upipe_dvbsrc_free_void(upipe);
        return NULL;
    }

    if (dvb_dev_find(upipe_dvbsrc->dvb, NULL, NULL) < 0) {
        upipe_err(upipe, "No DVB devices found");
        dvb_dev_free(upipe_dvbsrc->dvb);
        upipe_dvbsrc_free_void(upipe);
        return NULL;
    }

    upipe_dvbsrc_init_urefcount(upipe);
    upipe_dvbsrc_init_uref_mgr(upipe);
    upipe_dvbsrc_init_ubuf_mgr(upipe);
    upipe_dvbsrc_init_output(upipe);
    upipe_dvbsrc_init_upump_mgr(upipe);
    upipe_dvbsrc_init_upump(upipe);
    upipe_dvbsrc_init_uclock(upipe);
    upipe_dvbsrc->uri = NULL;
    upipe_dvbsrc->demux = NULL;
    upipe_dvbsrc->frontend = NULL;

    dvb_dev_set_logpriv(upipe_dvbsrc->dvb, 2, upipe_dvbsrc_log, uprobe);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This reads data from the source and outputs it.
 * It is called either when the idler triggers (permanent storage mode) or
 * when data is available on the DVB receiver descriptor (live stream mode).
 *
 * @param upump description structure of the read watcher
 */
static void upipe_dvbsrc_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_dvbsrc *upipe_dvbsrc = upipe_dvbsrc_from_upipe(upipe);
    uint64_t systime = 0; /* to keep gcc quiet */

    if (upipe_dvbsrc->uclock != NULL)
        systime = uclock_now(upipe_dvbsrc->uclock);

    struct uref *uref = uref_block_alloc(upipe_dvbsrc->uref_mgr,
            upipe_dvbsrc->ubuf_mgr, MTU);

    if (unlikely(uref == NULL)) {
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

    ssize_t ret = dvb_dev_read(upipe_dvbsrc->demux, buffer, MTU);
    uref_block_unmap(uref, 0);

    if (unlikely(ret < 0)) {
        if (ret != -EAGAIN)
            upipe_err_va(upipe, "read failed: %m");
        uref_free(uref);
        return;
    } else if (ret < MTU) {
        upipe_verbose_va(upipe, "incomplete read: %zd < %u", ret, MTU);
        uref_block_resize(uref, 0, ret);
    }

    upipe_dvbsrc_output(upipe, uref, &upipe_dvbsrc->upump);
}

/** @internal @This checks if the pump may be allocated.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_dvbsrc_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_dvbsrc *upipe_dvbsrc = upipe_dvbsrc_from_upipe(upipe);
    if (flow_format != NULL) {
        uref_flow_set_def(flow_format, "block.mpegts.");
        upipe_dvbsrc_store_flow_def(upipe, flow_format);
    }

    upipe_dvbsrc_check_upump_mgr(upipe);
    if (upipe_dvbsrc->upump_mgr == NULL)
        return UBASE_ERR_NONE;

    if (upipe_dvbsrc->uref_mgr == NULL) {
        upipe_dvbsrc_require_uref_mgr(upipe);
        return UBASE_ERR_NONE;
    }

    if (upipe_dvbsrc->ubuf_mgr == NULL) {
        struct uref *flow_format =
            uref_block_flow_alloc_def(upipe_dvbsrc->uref_mgr, NULL);
        uref_block_flow_set_size(flow_format, MTU);
        if (unlikely(flow_format == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
        upipe_dvbsrc_require_ubuf_mgr(upipe, flow_format);
        return UBASE_ERR_NONE;
    }

    if (upipe_dvbsrc->uclock == NULL &&
        urequest_get_opaque(&upipe_dvbsrc->uclock_request, struct upipe *)
            != NULL)
        return UBASE_ERR_NONE;

    if (upipe_dvbsrc->upump == NULL) {
        if (upipe_dvbsrc->demux == NULL)
            return UBASE_ERR_NONE;

        int fd = dvb_dev_get_fd(upipe_dvbsrc->demux);
        if (fd < 0)
            return UBASE_ERR_NONE;

        struct upump *upump = upump_alloc_fd_read(upipe_dvbsrc->upump_mgr,
                                    upipe_dvbsrc_worker, upipe, upipe->refcount,
                                    fd);
        if (unlikely(upump == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
            return UBASE_ERR_UPUMP;
        }
        upipe_dvbsrc_set_upump(upipe, upump);
        upump_start(upump);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This returns the uri of the currently opened DVB receiver.
 *
 * @param upipe description structure of the pipe
 * @param uri_p filled in with the uri of the DVB receiver
 * @return an error code
 */
static int upipe_dvbsrc_get_uri(struct upipe *upipe, const char **uri_p)
{
    struct upipe_dvbsrc *upipe_dvbsrc = upipe_dvbsrc_from_upipe(upipe);
    assert(uri_p != NULL);
    *uri_p = upipe_dvbsrc->uri;
    return UBASE_ERR_NONE;
}

/** @internal @This asks to open the given DVB receiver.
 *
 * @param upipe description structure of the pipe
 * @param uri relative or absolute uri of the DVB receiver
 * @return an error code
 */
static int upipe_dvbsrc_set_uri(struct upipe *upipe, const char *uri)
{
    struct upipe_dvbsrc *upipe_dvbsrc = upipe_dvbsrc_from_upipe(upipe);

    if (likely(upipe_dvbsrc->uri != NULL)) {
        upipe_notice_va(upipe, "closing DVB receiver %s", upipe_dvbsrc->uri);
        ubase_clean_str(&upipe_dvbsrc->uri);

        if (upipe_dvbsrc->demux) {
            dvb_dev_dmx_stop(upipe_dvbsrc->demux);
            dvb_dev_close(upipe_dvbsrc->demux);
            upipe_dvbsrc->demux = NULL;
        }

        if (upipe_dvbsrc->frontend) {
            dvb_dev_close(upipe_dvbsrc->frontend);
            upipe_dvbsrc->frontend = NULL;
        }
    }

    upipe_dvbsrc_set_upump(upipe, NULL);

    if (unlikely(uri == NULL))
        return UBASE_ERR_NONE;

    unsigned int adapter = 0;
    unsigned int num = 0;
    if (sscanf(uri, "%u:%u", &adapter, &num) != 2) {
        upipe_err_va(upipe, "invalid URI %s", uri);
        return UBASE_ERR_INVALID;
    }

    upipe_dvbsrc->uri = strdup(uri);
    if (unlikely(upipe_dvbsrc->uri == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    struct dvb_dev_list *dvb_dev;

    /* demux */

    dvb_dev = dvb_dev_seek_by_adapter(upipe_dvbsrc->dvb, adapter, num,
            DVB_DEVICE_DEMUX);
    if (dvb_dev == NULL) {
        upipe_err(upipe, "could not find demux");
        return UBASE_ERR_INVALID;
    }

    upipe_dvbsrc->demux = dvb_dev_open(upipe_dvbsrc->dvb, dvb_dev->sysname,
            O_NONBLOCK|O_RDWR);
    if (upipe_dvbsrc->demux == NULL) {
        upipe_err(upipe, "opening demux failed");
        return UBASE_ERR_INVALID;
    }

    /* frontend */

    dvb_dev = dvb_dev_seek_by_adapter(upipe_dvbsrc->dvb, adapter, num,
            DVB_DEVICE_FRONTEND);
    if (dvb_dev == NULL) {
        upipe_err(upipe, "could not find frontend");
        goto free_demux;
    }

    upipe_dvbsrc->frontend = dvb_dev_open(upipe_dvbsrc->dvb, dvb_dev->sysname,
            O_RDWR);
    if (!upipe_dvbsrc->frontend) {
        upipe_err(upipe, "opening frontend failed");
        goto free_demux;
    }

    struct dvb_v5_fe_parms *parms = upipe_dvbsrc->dvb->fe_parms;
    dvb_fe_get_parms(parms);

    if (dvb_dev_dmx_set_pesfilter(upipe_dvbsrc->demux, 0x2000, DMX_PES_OTHER,
                DMX_OUT_TAP, 2*96*MTU) < 0) {
        upipe_err(upipe, "could not setup demux filter");
        dvb_dev_close(upipe_dvbsrc->frontend);
        upipe_dvbsrc->frontend = NULL;
        goto free_demux;
    }

    return UBASE_ERR_NONE;

free_demux:
    dvb_dev_close(upipe_dvbsrc->demux);
    upipe_dvbsrc->demux = NULL;
    return UBASE_ERR_INVALID;
}

/** @internal */
static enum fe_code_rate get_fec_code(const char *val)
{
    static const char fec[][5] = {
        [FEC_NONE]  = "NONE",
        [FEC_1_2]   = "1_2",
        [FEC_2_3]   = "2_3",
        [FEC_3_4]   = "3_4",
        [FEC_4_5]   = "4_5",
        [FEC_5_6]   = "5_6",
        [FEC_6_7]   = "6_7",
        [FEC_7_8]   = "7_8",
        [FEC_8_9]   = "8_9",
        [FEC_AUTO]  = "AUTO",
        [FEC_3_5]   = "3_5",
        [FEC_9_10]  = "9_10",
        [FEC_2_5]   = "2_5",
    };

    for (size_t i = 0; i < sizeof(fec) / sizeof(*fec); i++)
        if (!strcmp(fec[i], val))
            return i;

    return -1;
}

/** @internal @This sets the content of a dvbsrc option.
 * @param upipe description structure of the pipe
 * @param option name of the option
 * @param content content of the option
 * @return an error code
 */
static int upipe_dvbsrc_set_option(struct upipe *upipe,
        const char *key, const char *val)
{
    struct upipe_dvbsrc *upipe_dvbsrc = upipe_dvbsrc_from_upipe(upipe);
    if (!key || !val)
        return UBASE_ERR_INVALID;

    struct dvb_v5_fe_parms *parms = upipe_dvbsrc->dvb->fe_parms;
    dvb_fe_get_parms(parms);

    // XXX
    parms->sat_number = 0;
    parms->lna = LNA_AUTO;

    if (!strcmp(key, "frequency")) {
        dvb_fe_store_parm(parms, DTV_FREQUENCY, atoi(val));
    } else if (!strcmp(key, "symbol-rate")) {
        dvb_fe_store_parm(parms, DTV_SYMBOL_RATE, atoi(val));
    } else if (!strcmp(key, "inner-fec")) {
        enum fe_code_rate code = get_fec_code(val);
        if (code == -1) {
            upipe_err_va(upipe, "unknown inner fec %s", val);
            return UBASE_ERR_INVALID;
        }
        dvb_fe_store_parm(parms, DTV_INNER_FEC, code);
    } else if (!strcmp(key, "polarization")) {
        if (!strcmp(val, "H")) {
            dvb_fe_store_parm(parms, DTV_POLARIZATION, POLARIZATION_H);
        } else if (!strcmp(val, "V")) {
            dvb_fe_store_parm(parms, DTV_POLARIZATION, POLARIZATION_V);
        } else if (!strcmp(val, "L")) {
            dvb_fe_store_parm(parms, DTV_POLARIZATION, POLARIZATION_L);
        } else if (!strcmp(val, "R")) {
            dvb_fe_store_parm(parms, DTV_POLARIZATION, POLARIZATION_R);
        } else {
            upipe_err_va(upipe, "unknown polarization %s", val);
            return UBASE_ERR_INVALID;
        }
    } else if (!strcmp(key, "lnb")) {
        int lnb = dvb_sat_search_lnb(val);
        if (lnb < 0) {
            upipe_err(upipe, "Unknown LNB");
            dvb_print_all_lnb();
            return UBASE_ERR_EXTERNAL;
        }
        parms->lnb = dvb_sat_get_lnb(lnb);
    } else if (!strcmp(key, "sys")) {
        if (!strcmp(val, "DVBS")) {
            dvb_set_sys(parms, SYS_DVBS);
        } else if (!strcmp(val, "DVBS2")) {
            dvb_set_sys(parms, SYS_DVBS2);
        } else {
            upipe_err_va(upipe, "unknown system %s", val);
            return UBASE_ERR_INVALID;
        }
    } else {
        upipe_err_va(upipe, "unknown option %s", key);
        return UBASE_ERR_INVALID;
    }

    dvb_fe_set_parms(parms);

    return UBASE_ERR_NONE;
}


/** @internal @This processes control commands on a DVB receiver source pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_dvbsrc_control(struct upipe *upipe,
                                 int command, va_list args)
{
    struct upipe_dvbsrc *upipe_dvbsrc = upipe_dvbsrc_from_upipe(upipe);

    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_dvbsrc_set_upump(upipe, NULL);
            return upipe_dvbsrc_attach_upump_mgr(upipe);
        case UPIPE_ATTACH_UCLOCK:
            upipe_dvbsrc_set_upump(upipe, NULL);
            upipe_dvbsrc_require_uclock(upipe);
            return UBASE_ERR_NONE;

        case UPIPE_SET_OPTION: {
            const char *k = va_arg(args, const char *);
            const char *v = va_arg(args, const char *);
            return upipe_dvbsrc_set_option(upipe, k, v);
        }

        case UPIPE_DVBSRC_GET_FRONTEND_STATUS: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_DVBSRC_SIGNATURE)
            unsigned int *status = va_arg(args, unsigned int *);
            struct dtv_properties *props = va_arg(args, struct dtv_properties *);

            struct dvb_v5_fe_parms *parms = upipe_dvbsrc->dvb->fe_parms;
            if (dvb_fe_get_stats(parms) < 0) {
                upipe_err(upipe, "Could not get frontend stats");
                return UBASE_ERR_EXTERNAL;
            }

            if (dvb_fe_retrieve_stats(parms, DTV_STATUS, status) < 0) {
                upipe_err(upipe, "Could not get signal status");
                return UBASE_ERR_EXTERNAL;
            }

            for (int i = 0; i < props->num; i++) {
                struct dtv_property *prop = &props->props[i];
                struct dtv_fe_stats *fe_st = &prop->u.st;
                struct dtv_stats *st;
                if (prop->cmd >= DTV_STAT_SIGNAL_STRENGTH && prop->cmd <= DTV_MAX_COMMAND) {
                    st  = dvb_fe_retrieve_stats_layer(parms, prop->cmd, 0);
                    if (!st) {
                        fe_st->len = 0;
                    } else {
                        fe_st->stat[0] = *st;
                        fe_st->len = 1;
                    }
                } else {
                    unsigned int u;
                    if (dvb_fe_retrieve_parm(parms, prop->cmd, &u)) {
                        fe_st->len = 0;
                    } else {
                        fe_st->stat[0].uvalue = u;
                        fe_st->stat[0].scale = FE_SCALE_NOT_AVAILABLE;
                        fe_st->len = 1;
                    }
                }
            }

            return UBASE_ERR_NONE;
        }

        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_dvbsrc_control_output(upipe, command, args);

        case UPIPE_GET_URI: {
            const char **uri_p = va_arg(args, const char **);
            return upipe_dvbsrc_get_uri(upipe, uri_p);
        }
        case UPIPE_SET_URI: {
            const char *uri = va_arg(args, const char *);
            return upipe_dvbsrc_set_uri(upipe, uri);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This processes control commands on a DVB receiver source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_dvbsrc_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(_upipe_dvbsrc_control(upipe, command, args));

    return upipe_dvbsrc_check(upipe, NULL);
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_dvbsrc_free(struct upipe *upipe)
{
    struct upipe_dvbsrc *upipe_dvbsrc = upipe_dvbsrc_from_upipe(upipe);

    upipe_throw_dead(upipe);

    if (upipe_dvbsrc->demux) {
        dvb_dev_dmx_stop(upipe_dvbsrc->demux);
        dvb_dev_close(upipe_dvbsrc->demux);
    }

    if (upipe_dvbsrc->frontend) {
        dvb_dev_close(upipe_dvbsrc->frontend);
    }

    dvb_dev_free(upipe_dvbsrc->dvb);

    free(upipe_dvbsrc->uri);

    upipe_dvbsrc_clean_uclock(upipe);
    upipe_dvbsrc_clean_upump(upipe);
    upipe_dvbsrc_clean_upump_mgr(upipe);
    upipe_dvbsrc_clean_output(upipe);
    upipe_dvbsrc_clean_ubuf_mgr(upipe);
    upipe_dvbsrc_clean_uref_mgr(upipe);
    upipe_dvbsrc_clean_urefcount(upipe);
    upipe_dvbsrc_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_dvbsrc_mgr = {
    .refcount = NULL,
    .signature = UPIPE_DVBSRC_SIGNATURE,

    .upipe_alloc = upipe_dvbsrc_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_dvbsrc_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all DVB receiver sources
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_dvbsrc_mgr_alloc(void)
{
    return &upipe_dvbsrc_mgr;
}
