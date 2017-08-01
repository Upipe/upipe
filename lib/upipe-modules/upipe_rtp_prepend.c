/*
 * Copyright (C) 2013-2016 OpenHeadend S.A.R.L.
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
 * @short Upipe rtp module to prepend rtp header to uref blocks
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uclock.h>
#include <upipe/uref_clock.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/upipe.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-modules/upipe_rtp_prepend.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <assert.h>

#include <bitstream/ietf/rtp.h>
#include <bitstream/ietf/rtp3551.h>
#include <bitstream/ietf/rtp6184.h>
#include <bitstream/ietf/rtp7587.h>

#define EXPECTED_FLOW_DEF "block."
#define OUT_FLOW "block.rtp."

#define DEFAULT_TYPE            96 /* first dynamic rtp type */
#define DEFAULT_TS_SYNC         UPIPE_RTP_PREPEND_TS_SYNC_CR
#define DEFAULT_CLOCKRATE       90000
#define RTP_TYPE_INVALID        UINT8_MAX

/** upipe_rtp_prepend structure */
struct upipe_rtp_prepend {
    /** refcount management structure */
    struct urefcount urefcount;

    /** output pipe */
    struct upipe *output;
    /** flow_definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** sync timestamp to */
    enum upipe_rtp_prepend_ts_sync ts_sync;
    /** timestamp sync is overwrite by user */
    bool ts_sync_overwrite;
    /** rtp sequence number */
    uint16_t seqnum;
    /** timestamp clockrate */
    uint32_t clockrate;
    /** timestamp is overwrited by user */
    bool clockrate_overwrite;
    /** rtp type */
    uint8_t type;
    /** rtp type is overwritten by user */
    bool type_overwrite;
    /** rtp type is MPA */
    bool mpa;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_rtp_prepend, upipe, UPIPE_RTP_PREPEND_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_rtp_prepend, urefcount, upipe_rtp_prepend_free)
UPIPE_HELPER_VOID(upipe_rtp_prepend);
UPIPE_HELPER_OUTPUT(upipe_rtp_prepend, output, flow_def, output_state, request_list);

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_rtp_prepend_input(struct upipe *upipe, struct uref *uref,
                                    struct upump **upump_p)
{
    struct upipe_rtp_prepend *upipe_rtp_prepend = upipe_rtp_prepend_from_upipe(upipe);
    struct ubuf *header, *payload;
    uint8_t *buf = NULL;
    uint64_t cr = 0;
    uint32_t ts;
    lldiv_t div;
    int size = -1;

    switch (upipe_rtp_prepend->ts_sync) {
    case UPIPE_RTP_PREPEND_TS_SYNC_PTS:
        /* timestamp (synced to program pts, fallback to system pts) */
        if (unlikely(!ubase_check(uref_clock_get_pts_prog(uref, &cr)))) {
            uref_clock_get_pts_sys(uref, &cr);
        }
        break;

    case UPIPE_RTP_PREPEND_TS_SYNC_CR:
        /* timestamp (synced to program clock ref,
         * fallback to system clock ref) */
        if (unlikely(!ubase_check(uref_clock_get_cr_prog(uref, &cr)))) {
            uref_clock_get_cr_sys(uref, &cr);
        }
        break;

    default:
        upipe_warn(upipe, "invalid ts sync");
    }

    div = lldiv(cr, UCLOCK_FREQ);
    ts = div.quot * upipe_rtp_prepend->clockrate
         + ((uint64_t)div.rem * upipe_rtp_prepend->clockrate)/UCLOCK_FREQ;

    /* alloc header */
    header = ubuf_block_alloc(uref->ubuf->mgr, RTP_HEADER_SIZE);
    if (unlikely(!header)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        uref_free(uref);
        return;
    }

    /* write header */
    ubuf_block_write(header, 0, &size, &buf);
    memset(buf, 0, RTP_HEADER_SIZE);
    rtp_set_hdr(buf);
    rtp_set_type(buf, upipe_rtp_prepend->type);
    rtp_set_seqnum(buf, upipe_rtp_prepend->seqnum);
    rtp_set_timestamp(buf, ts);
    ubuf_block_unmap(header, 0);
    upipe_rtp_prepend->seqnum++;

    if (upipe_rtp_prepend->mpa) {
        /* alloc mpa header */
        struct ubuf *mpa_header = ubuf_block_alloc(uref->ubuf->mgr, 4);
        if (unlikely(!mpa_header)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            uref_free(uref);
            ubuf_free(header);
            return;
        }

        /* write mpa mpa_header */
        ubuf_block_write(mpa_header, 0, &size, &buf);
        memset(buf, 0, 4); // frag_offset = 0
        ubuf_block_unmap(mpa_header, 0);

        if (unlikely(!ubase_check(ubuf_block_append(header, mpa_header)))) {
            upipe_warn(upipe, "could not append mpa header to header");
            ubuf_free(header);
            ubuf_free(mpa_header);
            uref_free(uref);
            return;
        }
    }

    /* append payload (current ubuf) to header to form segmented ubuf */
    payload = uref_detach_ubuf(uref);
    if (unlikely(!ubase_check(ubuf_block_append(header, payload)))) {
        upipe_warn(upipe, "could not append payload to header");
        ubuf_free(header);
        ubuf_free(payload);
        uref_free(uref);
        return;
    }
    uref_attach_ubuf(uref, header);

    upipe_rtp_prepend_output(upipe, uref, upump_p);
}

/* @internal @This try to infer RTP playload type.
 * Do nothing if RTP type was set by user
 * (i.e. by calling upipe_rtp_prepend_set_type).
 * Use hard coded values defined here.
 *
 * @param upipe description structure of the pipe
 * @param def flow definition attribute of the input flow def
 * @return an error code
 */
static int upipe_rtp_prepend_infer_type(struct upipe *upipe, const char *def)
{
    static const struct {
        const char *match;
        uint8_t type;
    } values[] = {
        { "mpegts", RTP_TYPE_MP2T },
        { "opus", DEFAULT_TYPE },
    };

    struct upipe_rtp_prepend *upipe_rtp_prepend =
        upipe_rtp_prepend_from_upipe(upipe);

    if (upipe_rtp_prepend->type_overwrite)
        return UBASE_ERR_NONE;

    for (unsigned i = 0; i < UBASE_ARRAY_SIZE(values); i++) {
        char match[strlen(values[i].match) + 2];
        snprintf(match, sizeof (match), ".%s.", values[i].match);

        if (ubase_ncmp(def, match + 1) && !strstr(def, match))
            continue;

        upipe_rtp_prepend->type = values[i].type;
        return UBASE_ERR_NONE;
    }

    upipe_warn_va(upipe, "cannot infer rtp type from %s", def);
    upipe_rtp_prepend->type = DEFAULT_TYPE;

    return UBASE_ERR_NONE;
}

/* @internal @This try to infer timestamp clock sync.
 * Do nothing if clock sync was defined by user
 * (i.e. by calling upipe_rtp_prepend_set_ts_sync).
 * Use hard coded values defined here.
 *
 * @param upipe description structure of the pipe
 * @param def flow definition attribute of the input flow def
 * @return an error code
 */
static int upipe_rtp_prepend_infer_ts_sync(struct upipe *upipe, const char *def)
{
    static const struct {
        const char *match;
        enum upipe_rtp_prepend_ts_sync sync;
    } values[] = {
        { "h264.pic", UPIPE_RTP_PREPEND_TS_SYNC_PTS },
        { "sound", UPIPE_RTP_PREPEND_TS_SYNC_PTS },
        { "mpegts", UPIPE_RTP_PREPEND_TS_SYNC_CR },
    };

    struct upipe_rtp_prepend *upipe_rtp_prepend =
        upipe_rtp_prepend_from_upipe(upipe);

    if (upipe_rtp_prepend->ts_sync_overwrite)
        return UBASE_ERR_NONE;

    for (unsigned i = 0; i < UBASE_ARRAY_SIZE(values); i++) {
        char match[strlen(values[i].match) + 2];
        snprintf(match, sizeof (match), ".%s.", values[i].match);

        if (ubase_ncmp(def, match + 1) && !strstr(def, match))
            continue;

        upipe_rtp_prepend->ts_sync = values[i].sync;
        return UBASE_ERR_NONE;
    }

    upipe_warn_va(upipe, "cannot infer timestamp sync from %s", def);
    upipe_rtp_prepend->ts_sync = DEFAULT_TS_SYNC;

    return UBASE_ERR_NONE;
}

/* @internal @This try to infer clock rate.
 * Do nothing if clockrate was defined by user
 * (i.e. by calling upipe_rtp_prepend_set_clock_rate).
 * First try clock rate for RTP type defined in rfc 3551.
 * Then use the sound flow rate if possible.
 * Finally use hard coded value.
 *
 * @param upipe description structure of the pipe
 * @param flow_def input flow definition format.
 * @return an error code
 */
static int upipe_rtp_prepend_infer_clockrate(struct upipe *upipe,
                                             struct uref *flow_def)
{
    static const struct {
        const char *match;
        uint32_t clockrate;
    } values[] = {
        { "h264.pic", RTP_6184_CLOCKRATE },
        { "opus.sound", RTP_7587_CLOCKRATE },
    };

    struct upipe_rtp_prepend *upipe_rtp_prepend =
        upipe_rtp_prepend_from_upipe(upipe);

    /* user defined? */
    if (upipe_rtp_prepend->clockrate_overwrite)
        return UBASE_ERR_NONE;

    /* clock rate is defined in rtp 3551? */
    uint32_t clockrate = rtp_3551_get_clock_rate(upipe_rtp_prepend->type);
    if (clockrate) {
        upipe_rtp_prepend->clockrate = clockrate;
        return UBASE_ERR_NONE;
    }

    /* sound flow rate is defined? */
    uint64_t rate;
    int ret = uref_sound_flow_get_rate(flow_def, &rate);
    if (ubase_check(ret)) {
        if (rate > UINT32_MAX) {
            upipe_err_va(upipe, "invalid rate: %"PRIu64, rate);
            return UBASE_ERR_INVALID;
        }
        upipe_rtp_prepend->clockrate = rate;
        return UBASE_ERR_NONE;
    }

    /* clock rate is defined here? */
    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def))
    for (unsigned i = 0; i < UBASE_ARRAY_SIZE(values); i++) {
        char match[strlen(values[i].match) + 2];
        snprintf(match, sizeof (match), ".%s.", values[i].match);

        if (ubase_ncmp(def, match + 1) && !strstr(def, match))
            continue;

        upipe_rtp_prepend->clockrate = values[i].clockrate;
        return UBASE_ERR_NONE;
    }

    upipe_warn_va(upipe, "cannot infer rtp clock rate from %s", def);
    upipe_rtp_prepend->clockrate = DEFAULT_CLOCKRATE;
    return UBASE_ERR_NONE;
}

/** @internal @This prints a notice on the parameters.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_rtp_prepend_notice(struct upipe *upipe)
{
    struct upipe_rtp_prepend *upipe_rtp_prepend =
        upipe_rtp_prepend_from_upipe(upipe);
    upipe_notice_va(upipe, "using type %"PRIu8" with rate %"PRIu32" Hz",
                    upipe_rtp_prepend->type, upipe_rtp_prepend->clockrate);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_rtp_prepend_set_flow_def(struct upipe *upipe,
                                          struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def))
    if (ubase_ncmp(def, EXPECTED_FLOW_DEF))
        return UBASE_ERR_INVALID;

    UBASE_RETURN(upipe_rtp_prepend_infer_type(upipe, def))
    UBASE_RETURN(upipe_rtp_prepend_infer_ts_sync(upipe, def))
    UBASE_RETURN(upipe_rtp_prepend_infer_clockrate(upipe, flow_def))
    upipe_rtp_prepend_notice(upipe);

    struct upipe_rtp_prepend *upipe_rtp_prepend =
        upipe_rtp_prepend_from_upipe(upipe);
    upipe_rtp_prepend->mpa = !ubase_ncmp(def, "block.mp2.sound.") ||
                             !ubase_ncmp(def, "block.mp3.sound.");

    struct uref *flow_def_dup;
    if ((flow_def_dup = uref_dup(flow_def)) == NULL)
        return UBASE_ERR_ALLOC;
    if (!ubase_check(uref_flow_set_def_va(flow_def_dup, OUT_FLOW"%s",
                              def + strlen(EXPECTED_FLOW_DEF)))) {
        uref_free(flow_def_dup);
        return UBASE_ERR_ALLOC;
    }

    upipe_rtp_prepend_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This sets the rtp payload type.
 *
 * @param upipe description structure of the pipe
 * @param type rtp payload type according to rfc 3551 if null)
 * @return an error code
 */
static int _upipe_rtp_prepend_set_type(struct upipe *upipe, uint8_t type)
{
    struct upipe_rtp_prepend *upipe_rtp_prepend =
        upipe_rtp_prepend_from_upipe(upipe);
    upipe_rtp_prepend->type_overwrite = true;
    upipe_rtp_prepend->type = type & 0x7f;
    upipe_rtp_prepend_notice(upipe);
    return UBASE_ERR_NONE;
}

/** @internal @This returns the configured RTP type.
 *
 * @param upipe description structure of the pipe
 * @param type_p rtp type
 * @return an error code
 */
static int _upipe_rtp_prepend_get_type(struct upipe *upipe, uint8_t *type)
{
    struct upipe_rtp_prepend *upipe_rtp_prepend =
                       upipe_rtp_prepend_from_upipe(upipe);
    if (type)
        *type = upipe_rtp_prepend->type;
    return UBASE_ERR_NONE;
}

/** @internal @This overwrite the RTP clock rate value.
 *
 * @param upipe description structure of the pipe
 * @param clockrate rtp clock rate value
 * @return an error code
 */
static int _upipe_rtp_prepend_set_clockrate(struct upipe *upipe,
                                            uint32_t clockrate)
{
    struct upipe_rtp_prepend *upipe_rtp_prepend =
        upipe_rtp_prepend_from_upipe(upipe);

    upipe_rtp_prepend->clockrate_overwrite = true;
    upipe_rtp_prepend->clockrate = clockrate;
    upipe_rtp_prepend_notice(upipe);
    return UBASE_ERR_NONE;
}

/** @internal @This get the current RTP clock rate value.
 *
 * @param upipe description structure of the pipe
 * @param clockrate_p rtp clock rate value
 * @return an error code
 */
static int _upipe_rtp_prepend_get_clockrate(struct upipe *upipe,
                                            uint32_t *clockrate_p)
{
    struct upipe_rtp_prepend *upipe_rtp_prepend =
        upipe_rtp_prepend_from_upipe(upipe);

    if (clockrate_p)
        *clockrate_p = upipe_rtp_prepend->clockrate;
    return UBASE_ERR_NONE;
}

static int
_upipe_rtp_prepend_get_ts_sync(struct upipe *upipe,
                               enum upipe_rtp_prepend_ts_sync *ts_sync)
{
    struct upipe_rtp_prepend *upipe_rtp_prepend =
                       upipe_rtp_prepend_from_upipe(upipe);
    if (ts_sync)
        *ts_sync = upipe_rtp_prepend->ts_sync;
    return UBASE_ERR_NONE;
}

static int
_upipe_rtp_prepend_set_ts_sync(struct upipe *upipe,
                               enum upipe_rtp_prepend_ts_sync ts_sync)
{
    struct upipe_rtp_prepend *upipe_rtp_prepend =
                       upipe_rtp_prepend_from_upipe(upipe);

    upipe_rtp_prepend->ts_sync_overwrite = true;
    upipe_rtp_prepend->ts_sync = ts_sync;
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a rtp_prepend pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_rtp_prepend_control(struct upipe *upipe,
                                     int command, va_list args)
{
    UBASE_HANDLED_RETURN(
        upipe_rtp_prepend_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_rtp_prepend_set_flow_def(upipe, flow_def);
        }

        case UPIPE_RTP_PREPEND_GET_TYPE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_RTP_PREPEND_SIGNATURE)
            uint8_t *type_p = va_arg(args, uint8_t *);
            return _upipe_rtp_prepend_get_type(upipe, type_p);
        }
        case UPIPE_RTP_PREPEND_SET_TYPE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_RTP_PREPEND_SIGNATURE)
            uint8_t type = (uint8_t) va_arg(args, int);
            return _upipe_rtp_prepend_set_type(upipe, type);
        }

        case UPIPE_RTP_PREPEND_SET_CLOCKRATE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_RTP_PREPEND_SIGNATURE)
            uint32_t clockrate = va_arg(args, uint32_t);
            return _upipe_rtp_prepend_set_clockrate(upipe, clockrate);
        }
        case UPIPE_RTP_PREPEND_GET_CLOCKRATE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_RTP_PREPEND_SIGNATURE)
            uint32_t *clockrate_p = va_arg(args, uint32_t *);
            return _upipe_rtp_prepend_get_clockrate(upipe, clockrate_p);
        }

        case UPIPE_RTP_PREPEND_GET_TS_SYNC: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_RTP_PREPEND_SIGNATURE);
            enum upipe_rtp_prepend_ts_sync *ts_sync =
                va_arg(args, enum upipe_rtp_prepend_ts_sync *);
            return _upipe_rtp_prepend_get_ts_sync(upipe, ts_sync);
        }
        case UPIPE_RTP_PREPEND_SET_TS_SYNC: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_RTP_PREPEND_SIGNATURE);
            enum upipe_rtp_prepend_ts_sync ts_sync =
                va_arg(args, enum upipe_rtp_prepend_ts_sync);
            return _upipe_rtp_prepend_set_ts_sync(upipe, ts_sync);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This allocates a rtp_prepend pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_rtp_prepend_alloc(struct upipe_mgr *mgr,
                                             struct uprobe *uprobe,
                                             uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_rtp_prepend_alloc_void(mgr, uprobe, signature,
                                                       args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_rtp_prepend *upipe_rtp_prepend =
        upipe_rtp_prepend_from_upipe(upipe);
    upipe_rtp_prepend_init_urefcount(upipe);
    upipe_rtp_prepend_init_output(upipe);

    upipe_rtp_prepend->ts_sync_overwrite = false;
    upipe_rtp_prepend->ts_sync = DEFAULT_TS_SYNC;
    upipe_rtp_prepend->clockrate_overwrite = false;
    upipe_rtp_prepend->clockrate = 0;
    upipe_rtp_prepend->type_overwrite = false;
    upipe_rtp_prepend->type = RTP_TYPE_INVALID;
    upipe_rtp_prepend->seqnum = 0; /* FIXME random init?*/
    upipe_rtp_prepend->mpa = false;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_rtp_prepend_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_rtp_prepend_clean_output(upipe);
    upipe_rtp_prepend_clean_urefcount(upipe);
    upipe_rtp_prepend_free_void(upipe);
}

static struct upipe_mgr upipe_rtp_prepend_mgr = {
    .refcount = NULL,
    .signature = UPIPE_RTP_PREPEND_SIGNATURE,

    .upipe_alloc = upipe_rtp_prepend_alloc,
    .upipe_input = upipe_rtp_prepend_input,
    .upipe_control = upipe_rtp_prepend_control
};

/** @This returns the management structure for rtp_prepend pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_rtp_prepend_mgr_alloc(void)
{
    return &upipe_rtp_prepend_mgr;
}
