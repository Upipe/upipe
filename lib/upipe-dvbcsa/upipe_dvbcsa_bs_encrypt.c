/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
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

#include <upipe-dvbcsa/upipe_dvbcsa_bs_encrypt.h>
#include <upipe-dvbcsa/upipe_dvbcsa_common.h>

#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_uclock.h>

#include <upipe/uclock.h>
#include <upipe/upipe.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uref_block.h>
#include <upipe/uref_clock.h>

#include <bitstream/mpeg/ts.h>

#include "common.h"

/** expected input flow format */
#define EXPECTED_FLOW_DEF "block.mpegts."
/** pmt flow format */
#define PMT_FLOW_DEF "block.mpegtspsi.mpegtspmt."
/** Approximation worst dvbcsa encrypt latency on normal hardware (20ms) */
#define DVBCSA_LATENCY  (UCLOCK_FREQ / 50)

/** @internal @This is the private structure of dvbcsa encryption pipe. */
struct upipe_dvbcsa_bs_enc {
    /** public pipe structure */
    struct upipe upipe;
    /** refcount structure */
    struct urefcount urefcount;
    /** output pipe */
    struct upipe *output;
    /** output flow definition */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** requests list */
    struct uchain requests;
    /** uclock */
    struct uclock *uclock;
    /** uclock request */
    struct urequest uclock_request;
    /** list of retained urefs */
    struct uchain urefs;
    /** number of retained urefs */
    unsigned nb_urefs;
    /** maximum number of retained urefs */
    unsigned max_urefs;
    /** list of pump blockers */
    struct uchain blockers;
    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** timer */
    struct upump *upump;
    /** encryption batch size */
    unsigned batch_size;
    /** encryption key */
    dvbcsa_bs_key_t *key;
    /** batch list */
    struct dvbcsa_bs_batch_s *batch;
    /** batch current item */
    unsigned current;
    /** mapped list */
    struct uref **mapped;
    /** common dvbcsa structure */
    struct upipe_dvbcsa_common common;
};

/** @hidden */
UBASE_FROM_TO(upipe_dvbcsa_bs_enc, upipe_dvbcsa_common, common, common);

/** @hidden */
static int upipe_dvbcsa_bs_enc_check(struct upipe *upipe,
                                     struct uref *flow_def);

UPIPE_HELPER_UPIPE(upipe_dvbcsa_bs_enc, upipe, UPIPE_DVBCSA_BS_ENC_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_dvbcsa_bs_enc, urefcount,
                       upipe_dvbcsa_bs_enc_free);
UPIPE_HELPER_VOID(upipe_dvbcsa_bs_enc);
UPIPE_HELPER_INPUT(upipe_dvbcsa_bs_enc, urefs, nb_urefs, max_urefs, blockers,
                   NULL);
UPIPE_HELPER_OUTPUT(upipe_dvbcsa_bs_enc, output, flow_def, output_state,
                    requests);
UPIPE_HELPER_UCLOCK(upipe_dvbcsa_bs_enc, uclock, uclock_request,
                    upipe_dvbcsa_bs_enc_check,
                    upipe_dvbcsa_bs_enc_register_output_request,
                    upipe_dvbcsa_bs_enc_unregister_output_request);
UPIPE_HELPER_UPUMP_MGR(upipe_dvbcsa_bs_enc, upump_mgr);
UPIPE_HELPER_UPUMP(upipe_dvbcsa_bs_enc, upump, upump_mgr);

/** @internal @This frees a dvbcsa encryption pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_dvbcsa_bs_enc_free(struct upipe *upipe)
{
    struct upipe_dvbcsa_bs_enc *upipe_dvbcsa_bs_enc =
        upipe_dvbcsa_bs_enc_from_upipe(upipe);
    struct upipe_dvbcsa_common *common =
        upipe_dvbcsa_bs_enc_to_common(upipe_dvbcsa_bs_enc);

    upipe_throw_dead(upipe);

    for (unsigned i = 0; i < upipe_dvbcsa_bs_enc->current; i++)
        uref_block_unmap(upipe_dvbcsa_bs_enc->mapped[i], 0);
    dvbcsa_bs_key_free(upipe_dvbcsa_bs_enc->key);
    free(upipe_dvbcsa_bs_enc->mapped);
    free(upipe_dvbcsa_bs_enc->batch);
    upipe_dvbcsa_common_clean(common);
    upipe_dvbcsa_bs_enc_clean_upump(upipe);
    upipe_dvbcsa_bs_enc_clean_upump_mgr(upipe);
    upipe_dvbcsa_bs_enc_clean_output(upipe);
    upipe_dvbcsa_bs_enc_clean_input(upipe);
    upipe_dvbcsa_bs_enc_clean_uclock(upipe);
    upipe_dvbcsa_bs_enc_clean_urefcount(upipe);
    upipe_dvbcsa_bs_enc_free_void(upipe);
}

/** @internal @This allocates and initializes a dvbcsa encryption pipe.
 *
 * @param mgr pointer to pipe manager
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return an allocated and initialized pipe or NULL
 */
static struct upipe *upipe_dvbcsa_bs_enc_alloc(struct upipe_mgr *mgr,
                                               struct uprobe *uprobe,
                                               uint32_t signature,
                                               va_list args)
{
    struct upipe *upipe =
        upipe_dvbcsa_bs_enc_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(!upipe))
        return NULL;
    struct upipe_dvbcsa_bs_enc *upipe_dvbcsa_bs_enc =
        upipe_dvbcsa_bs_enc_from_upipe(upipe);
    struct upipe_dvbcsa_common *common =
        upipe_dvbcsa_bs_enc_to_common(upipe_dvbcsa_bs_enc);

    upipe_dvbcsa_bs_enc_init_urefcount(upipe);
    upipe_dvbcsa_bs_enc_init_uclock(upipe);
    upipe_dvbcsa_bs_enc_init_input(upipe);
    upipe_dvbcsa_bs_enc_init_output(upipe);
    upipe_dvbcsa_bs_enc_init_upump_mgr(upipe);
    upipe_dvbcsa_bs_enc_init_upump(upipe);
    upipe_dvbcsa_common_init(common);
    upipe_dvbcsa_bs_enc->key = NULL;
    unsigned bs_size = dvbcsa_bs_batch_size();
    upipe_dvbcsa_bs_enc->batch_size = bs_size;
    upipe_dvbcsa_bs_enc->batch = malloc((bs_size + 1) *
                                        sizeof (struct dvbcsa_bs_batch_s));
    upipe_dvbcsa_bs_enc->mapped = malloc(bs_size * sizeof (struct uref *));
    upipe_dvbcsa_bs_enc->current = 0;

    upipe_throw_ready(upipe);

    if (unlikely(!upipe_dvbcsa_bs_enc->batch ||
                 !upipe_dvbcsa_bs_enc->mapped)) {
        upipe_err(upipe, "allocation failed");
        upipe_release(upipe);
        return NULL;
    }

    return upipe;
}

/** @internal @This sets the flow format for real.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow format to set
 */
static int upipe_dvbcsa_bs_enc_set_flow_def_real(struct upipe *upipe,
                                                 struct uref *flow_def)
{
    struct upipe_dvbcsa_bs_enc *upipe_dvbcsa_bs_enc =
        upipe_dvbcsa_bs_enc_from_upipe(upipe);
    struct upipe_dvbcsa_common *common =
        upipe_dvbcsa_bs_enc_to_common(upipe_dvbcsa_bs_enc);
    uint64_t latency = 0;
    uref_clock_get_latency(flow_def, &latency);
    latency += common->latency + DVBCSA_LATENCY;
    uref_clock_set_latency(flow_def, latency);
    upipe_dvbcsa_bs_enc_store_flow_def(upipe, flow_def);
    return UBASE_ERR_NONE;
}

/** @internal @This flushes the retained urefs.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_dvbcsa_bs_enc_flush(struct upipe *upipe,
                                      struct upump **upump_p)
{
    struct upipe_dvbcsa_bs_enc *upipe_dvbcsa_bs_enc =
        upipe_dvbcsa_bs_enc_from_upipe(upipe);

    upipe_dvbcsa_bs_enc_set_upump(upipe, NULL);

    /* scramble remaining packets */
    unsigned current = upipe_dvbcsa_bs_enc->current;
    if (upipe_dvbcsa_bs_enc->current) {
        upipe_dvbcsa_bs_enc->current = 0;
        upipe_dvbcsa_bs_enc->batch[current].data = NULL;
        upipe_dvbcsa_bs_enc->batch[current].len = 0;

        uint64_t before = uclock_now(upipe_dvbcsa_bs_enc->uclock);
        dvbcsa_bs_encrypt(upipe_dvbcsa_bs_enc->key,
                          upipe_dvbcsa_bs_enc->batch, 184);
        uint64_t after = uclock_now(upipe_dvbcsa_bs_enc->uclock);
        if ((after - before) > DVBCSA_LATENCY)
            upipe_warn_va(upipe, "dvbcsa latency too high %"PRIu64 "ms",
                          (after - before) / 1000);
        for (unsigned i = 0; i < current; i++)
            uref_block_unmap(upipe_dvbcsa_bs_enc->mapped[i], 0);
    }

    /* output */
    struct uref *uref;
    while ((uref = upipe_dvbcsa_bs_enc_pop_input(upipe))) {
        if (unlikely(ubase_check(uref_flow_get_def(uref, NULL))))
            /* handle flow format */
            upipe_dvbcsa_bs_enc_set_flow_def_real(upipe, uref);
        else
            upipe_dvbcsa_bs_enc_output(upipe, uref, upump_p);
    }

    /* all buffered urefs has been sent */
    upipe_release(upipe);
}

/** @internal @This is called when maximum latency is reached to flush all
 * retained urefs.
 *
 * @param upump timer
 */
static void upipe_dvbcsa_bs_enc_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    return upipe_dvbcsa_bs_enc_flush(upipe, &upump);
}

/** @internal @This handles input buffers.
 *
 * @param upipe description structure of the pipe
 * @param uref input buffer to handle
 * @param upump_p reference to the pump that generated the buffer
 */
static void upipe_dvbcsa_bs_enc_input(struct upipe *upipe,
                                      struct uref *uref,
                                      struct upump **upump_p)
{
    struct upipe_dvbcsa_bs_enc *upipe_dvbcsa_bs_enc =
        upipe_dvbcsa_bs_enc_from_upipe(upipe);
    struct upipe_dvbcsa_common *common =
        upipe_dvbcsa_bs_enc_to_common(upipe_dvbcsa_bs_enc);
    bool first = upipe_dvbcsa_bs_enc_check_input(upipe);
    int ret;

    /* handle flow format */
    if (unlikely(ubase_check(uref_flow_get_def(uref, NULL)))) {
        if (first)
            upipe_dvbcsa_bs_enc_set_flow_def_real(upipe, uref);
        else
            upipe_dvbcsa_bs_enc_hold_input(upipe, uref);
        return;
    }

    /* get TS header */
    uint32_t ts_header_size = TS_HEADER_SIZE;
    uint8_t buf[TS_HEADER_SIZE];
    const uint8_t *ts_header = uref_block_peek(uref, 0, sizeof (buf), buf);
    if (unlikely(!ts_header)) {
        upipe_err(upipe, "fail to read TS header");
        uref_free(uref);
        return;
    }
    uint8_t scrambling = ts_get_scrambling(ts_header);
    bool has_payload = ts_has_payload(ts_header);
    bool has_adaptation = ts_has_adaptation(ts_header);
    uint16_t pid = ts_get_pid(ts_header);
    uref_block_peek_unmap(uref, 0, buf, ts_header);

    bool scramble =
        upipe_dvbcsa_bs_enc->key != NULL &&
        has_payload && !scrambling &&
        upipe_dvbcsa_common_check_pid(common, pid);
    if (!scramble) {
        if (first)
            upipe_dvbcsa_bs_enc_output(upipe, uref, upump_p);
        else
            upipe_dvbcsa_bs_enc_hold_input(upipe, uref);
        return;
    }

    if (unlikely(has_adaptation)) {
        uint8_t af_length;
        ret = uref_block_extract(uref, ts_header_size, 1, &af_length);
        if (unlikely(!ubase_check(ret))) {
            upipe_err(upipe, "fail to get adaptation field length");
            uref_free(uref);
            return;
        }
        if (unlikely(af_length >= 183)) {
            upipe_err(upipe, "invalid adaptation field");
            uref_free(uref);
            return;
        }
        ts_header_size += af_length + 1;
    }

    /* copy buffer */
    struct ubuf *ubuf = ubuf_block_copy(uref->ubuf->mgr, uref->ubuf, 0, -1);
    if (unlikely(!ubuf)) {
        uref_free(uref);
        UBASE_FATAL(upipe, UBASE_ERR_ALLOC);
        return;
    }
    uref_attach_ubuf(uref, ubuf);

    /* prepare write */
    int size = -1;
    uint8_t *ts;
    ret = ubuf_block_write(ubuf, 0, &size, &ts);
    if (unlikely(!ubase_check(ret))) {
        upipe_err(upipe, "fail to write block");
        uref_free(uref);
        return;
    }

    if (unlikely(size <= ts_header_size)) {
        upipe_err(upipe, "invalid size");
        uref_block_unmap(uref, 0);
        uref_free(uref);
        return;
    }

    uint8_t current = upipe_dvbcsa_bs_enc->current;
    ts_set_scrambling(ts, 0x2);
    upipe_dvbcsa_bs_enc->batch[current].data = ts + ts_header_size;
    upipe_dvbcsa_bs_enc->batch[current].len = size - ts_header_size;
    upipe_dvbcsa_bs_enc->mapped[current] = uref;
    upipe_dvbcsa_bs_enc->current++;

    /* hold uref */
    upipe_dvbcsa_bs_enc_hold_input(upipe, uref);
    if (unlikely(first)) {
        /* make sure to send all buffered urefs */
        upipe_use(upipe);
        upipe_dvbcsa_bs_enc_wait_upump(upipe, common->latency,
                                       upipe_dvbcsa_bs_enc_worker);
    }

    /* scramble if we have enough packets */
    if (upipe_dvbcsa_bs_enc->current >= upipe_dvbcsa_bs_enc->batch_size)
        upipe_dvbcsa_bs_enc_flush(upipe, upump_p);
}

/** @internal @This allocates a new pump if needed.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_dvbcsa_bs_enc_check(struct upipe *upipe, struct uref *flow_def)
{
    struct upipe_dvbcsa_bs_enc *upipe_dvbcsa_bs_enc =
        upipe_dvbcsa_bs_enc_from_upipe(upipe);

    if (unlikely(!upipe_dvbcsa_bs_enc->uclock))
        upipe_dvbcsa_bs_enc_require_uclock(upipe);

    UBASE_RETURN(upipe_dvbcsa_bs_enc_check_upump_mgr(upipe));
    if (unlikely(!upipe_dvbcsa_bs_enc->upump_mgr))
        return UBASE_ERR_NONE;

    return UBASE_ERR_NONE;
}

/** @internal @This handles a new input flow format.
 *
 * @param upipe description structure of the pipe
 * @param flow_def new flow format to set
 * @return an error code
 */
static int upipe_dvbcsa_bs_enc_set_flow_def(struct upipe *upipe,
                                            struct uref *flow_def)
{
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF));
    struct uref *flow_def_dup = uref_dup(flow_def);
    UBASE_ALLOC_RETURN(flow_def_dup);
    upipe_input(upipe, flow_def_dup, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This sets the encryption key.
 *
 * @param upipe description structure of the pipe
 * @param key dvbcsa key to set
 * @return an error code
 */
static int upipe_dvbcsa_bs_enc_set_key(struct upipe *upipe,
                                       const char *key)
{
    struct upipe_dvbcsa_bs_enc *upipe_dvbcsa_bs_enc =
        upipe_dvbcsa_bs_enc_from_upipe(upipe);

    dvbcsa_bs_key_free(upipe_dvbcsa_bs_enc->key);
    upipe_dvbcsa_bs_enc->key = NULL;
    if (!key)
        return UBASE_ERR_NONE;

    struct ustring_dvbcsa_cw cw = ustring_to_dvbcsa_cw(ustring_from_str(key));
    if (unlikely(ustring_is_empty(cw.str) || strlen(key) != cw.str.len))
        return UBASE_ERR_INVALID;

    upipe_notice(upipe, "key changed");
    upipe_dvbcsa_bs_enc->key = dvbcsa_bs_key_alloc();
    UBASE_ALLOC_RETURN(upipe_dvbcsa_bs_enc->key);
    dvbcsa_bs_key_set(cw.value, upipe_dvbcsa_bs_enc->key);
    return UBASE_ERR_NONE;

}

/** @internal @This handles the dvbcsa encryption pipe control commands.
 *
 * @param upipe description structure of the pipe
 * @param cmd control command to handle
 * @param args optional arguments
 * @return an error code
 */
static int upipe_dvbcsa_bs_enc_control_real(struct upipe *upipe,
                                            int cmd, va_list args)
{
    struct upipe_dvbcsa_bs_enc *upipe_dvbcsa_bs_enc =
        upipe_dvbcsa_bs_enc_from_upipe(upipe);
    struct upipe_dvbcsa_common *common =
        upipe_dvbcsa_bs_enc_to_common(upipe_dvbcsa_bs_enc);

    UBASE_HANDLED_RETURN(upipe_dvbcsa_bs_enc_control_output(upipe, cmd, args));

    switch (cmd) {
        case UPIPE_ATTACH_UPUMP_MGR:
            return upipe_dvbcsa_bs_enc_attach_upump_mgr(upipe);

        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_dvbcsa_bs_enc_set_flow_def(upipe, flow_def);
        }

        case UPIPE_DVBCSA_SET_KEY: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_DVBCSA_COMMON_SIGNATURE);
            const char *key = va_arg(args, const char *);
            return upipe_dvbcsa_bs_enc_set_key(upipe, key);
        }

        case UPIPE_DVBCSA_ADD_PID:
        case UPIPE_DVBCSA_DEL_PID:
        case UPIPE_DVBCSA_SET_MAX_LATENCY:
            return upipe_dvbcsa_common_control(common, cmd, args);
    }
    return UBASE_ERR_UNHANDLED;
}

/** @internal @This handles control commands and check the pipe state.
 *
 * @param upipe description structure of the pipe
 * @param cmd control command to handle
 * @param args optional arguments
 * @return an error code
 */
static int upipe_dvbcsa_bs_enc_control(struct upipe *upipe,
                                       int cmd, va_list args)
{
    UBASE_RETURN(upipe_dvbcsa_bs_enc_control_real(upipe, cmd, args));
    return upipe_dvbcsa_bs_enc_check(upipe, NULL);
}

static struct upipe_mgr upipe_dvbcsa_bs_enc_mgr = {
    .signature = UPIPE_DVBCSA_BS_ENC_SIGNATURE,
    .refcount = NULL,
    .upipe_alloc = upipe_dvbcsa_bs_enc_alloc,
    .upipe_input = upipe_dvbcsa_bs_enc_input,
    .upipe_control = upipe_dvbcsa_bs_enc_control,
};

struct upipe_mgr *upipe_dvbcsa_bs_enc_mgr_alloc(void)
{
    return &upipe_dvbcsa_bs_enc_mgr;
}
