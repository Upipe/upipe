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

#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>

#include <upipe/upipe.h>
#include <upipe/uref_block.h>
#include <upipe/uprobe_prefix.h>

#include <upipe-ts/upipe_ts_demux.h>
#include <upipe-ts/upipe_ts_split.h>

#include <upipe-dvbcsa/upipe_dvbcsa_encrypt.h>
#include <upipe-dvbcsa/upipe_dvbcsa_common.h>

#include <bitstream/mpeg/ts.h>
#include <bitstream/mpeg/psi/pat.h>
#include <bitstream/mpeg/psi/pmt.h>
#include <dvbcsa/dvbcsa.h>

#include "common.h"

/** expected input flow format */
#define EXPECTED_FLOW_DEF "block.mpegts."
/** pmt flow format */
#define PMT_FLOW_DEF "block.mpegtspsi.mpegtspmt."

/** @internal @This is the private structure of dvbcsa encryption pipe. */
struct upipe_dvbcsa_enc {
    /** public pipe structure */
    struct upipe upipe;
    /** refcount structure */
    struct urefcount urefcount;
    /** real refcount structure */
    struct urefcount urefcount_real;
    /** output pipe */
    struct upipe *output;
    /** output flow format */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** requests list */
    struct uchain requests;
    /** dvbcsa key */
    dvbcsa_key_t *key;
    /** common dvbcsa structure */
    struct upipe_dvbcsa_common common;
};

/** @hidden */
UBASE_FROM_TO(upipe_dvbcsa_enc, upipe_dvbcsa_common, common, common);

UPIPE_HELPER_UPIPE(upipe_dvbcsa_enc, upipe, UPIPE_DVBCSA_ENC_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_dvbcsa_enc, urefcount, upipe_dvbcsa_enc_free);
UPIPE_HELPER_VOID(upipe_dvbcsa_enc);
UPIPE_HELPER_OUTPUT(upipe_dvbcsa_enc, output, flow_def, output_state, requests);

/** @internal @This frees a dvbcsa encryption pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_dvbcsa_enc_free(struct upipe *upipe)
{
    struct upipe_dvbcsa_enc *upipe_dvbcsa_enc =
        upipe_dvbcsa_enc_from_upipe(upipe);
    struct upipe_dvbcsa_common *common =
        upipe_dvbcsa_enc_to_common(upipe_dvbcsa_enc);

    upipe_throw_dead(upipe);

    dvbcsa_key_free(upipe_dvbcsa_enc->key);
    upipe_dvbcsa_common_clean(common);
    upipe_dvbcsa_enc_clean_output(upipe);
    upipe_dvbcsa_enc_clean_urefcount(upipe);
    upipe_dvbcsa_enc_free_void(upipe);
}

/** @internal @This allocates and initializes a dvbcsa encryption pipe.
 *
 * @param mgr pointer to pipe manager
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return an allocated and initialized pipe
 */
static struct upipe *upipe_dvbcsa_enc_alloc(struct upipe_mgr *mgr,
                                            struct uprobe *uprobe,
                                            uint32_t signature,
                                            va_list args)
{
    struct upipe *upipe =
        upipe_dvbcsa_enc_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(!upipe))
        return NULL;
    struct upipe_dvbcsa_enc *upipe_dvbcsa_enc =
        upipe_dvbcsa_enc_from_upipe(upipe);
    struct upipe_dvbcsa_common *common =
        upipe_dvbcsa_enc_to_common(upipe_dvbcsa_enc);

    upipe_dvbcsa_enc_init_urefcount(upipe);
    upipe_dvbcsa_enc_init_output(upipe);
    upipe_dvbcsa_common_init(common);
    upipe_dvbcsa_enc->key = NULL;

    upipe_throw_ready(upipe);

    return upipe;
}

/** @internal @This handles input buffers.
 *
 * @param upipe description structure of the pipe
 * @param uref input buffer to handle
 * @param upump_p reference to the pump that generated the buffer
 */
static void upipe_dvbcsa_enc_input(struct upipe *upipe,
                                   struct uref *uref,
                                   struct upump **upump_p)
{
    struct upipe_dvbcsa_enc *upipe_dvbcsa_enc =
        upipe_dvbcsa_enc_from_upipe(upipe);
    struct upipe_dvbcsa_common *common =
        upipe_dvbcsa_enc_to_common(upipe_dvbcsa_enc);

    if (unlikely(!upipe_dvbcsa_enc->key))
        /* no key set, forward */
        return upipe_dvbcsa_enc_output(upipe, uref, upump_p);

    uint32_t ts_header_size = TS_HEADER_SIZE;
    uint8_t buf[TS_HEADER_SIZE];
    const uint8_t *ts_header = uref_block_peek(uref, 0, sizeof (buf), buf);
    if (unlikely(!ts_header)) {
        upipe_err(upipe, "fail to read ts header");
        uref_free(uref);
        return;
    }
    uint8_t scrambling = ts_get_scrambling(ts_header);
    bool has_payload = ts_has_payload(ts_header);
    bool has_adaptation = ts_has_adaptation(ts_header);
    uint16_t pid = ts_get_pid(ts_header);
    uref_block_peek_unmap(uref, 0, buf, ts_header);

    if (!has_payload || scrambling ||
        !upipe_dvbcsa_common_check_pid(common, pid))
        return upipe_dvbcsa_enc_output(upipe, uref, upump_p);

    if (unlikely(has_adaptation)) {
        uint8_t af_length;
        int ret = uref_block_extract(uref, ts_header_size, 1, &af_length);
        if (unlikely(!ubase_check(ret))) {
            upipe_err(upipe, "fail to extract adaptation field length");
            uref_free(uref);
            return;
        }
        if (unlikely(af_length >= 183)) {
            upipe_warn(upipe, "invalid adaptation field received");
            uref_free(uref);
            return;
        }
        ts_header_size += af_length + 1;
    }

    struct ubuf *ubuf = ubuf_block_copy(uref->ubuf->mgr, uref->ubuf, 0, -1);
    if (unlikely(!ubuf)) {
        upipe_err(upipe, "fail to allocate buffer");
        uref_free(uref);
        return;
    }
    uref_attach_ubuf(uref, ubuf);
    int size = -1;
    uint8_t *ts;
    int ret = ubuf_block_write(ubuf, 0, &size, &ts);
    if (unlikely(!ubase_check(ret))) {
        upipe_err(upipe, "fail to write buffer");
        uref_free(uref);
        return;
    }
    ts_set_scrambling(ts, 0x2);
    dvbcsa_encrypt(upipe_dvbcsa_enc->key,
                   ts + ts_header_size,
                   size - ts_header_size);
    return upipe_dvbcsa_enc_output(upipe, uref, upump_p);
}

/** @internal @This sets the output flow format.
 *
 * @param upipe description structure of the pipe
 * @param flow_def new flow format to set
 * @return an error code
 */
static int upipe_dvbcsa_enc_set_flow_def(struct upipe *upipe,
                                         struct uref *flow_def)
{
    struct upipe_dvbcsa_enc *upipe_dvbcsa_enc =
        upipe_dvbcsa_enc_from_upipe(upipe);
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF));
    struct uref *flow_def_dup = uref_dup(flow_def);
    UBASE_ALLOC_RETURN(flow_def_dup);
    upipe_dvbcsa_enc_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This sets the dvbcsa key.
 *
 * @param upipe description structure of the pipe
 * @param key dvbcsa key to set
 * @return an error code
 */
static int upipe_dvbcsa_enc_set_key(struct upipe *upipe, const char *key)
{
    struct upipe_dvbcsa_enc *upipe_dvbcsa_enc =
        upipe_dvbcsa_enc_from_upipe(upipe);

    dvbcsa_key_free(upipe_dvbcsa_enc->key);
    upipe_dvbcsa_enc->key = NULL;

    if (!key)
        return UBASE_ERR_NONE;

    struct ustring_dvbcsa_cw cw = ustring_to_dvbcsa_cw(ustring_from_str(key));
    if (unlikely(ustring_is_empty(cw.str) || strlen(key) != cw.str.len))
        return UBASE_ERR_INVALID;

    upipe_notice(upipe, "key chanhed");
    upipe_dvbcsa_enc->key = dvbcsa_key_alloc();
    UBASE_ALLOC_RETURN(upipe_dvbcsa_enc->key);
    dvbcsa_key_set(cw.value, upipe_dvbcsa_enc->key);
    return UBASE_ERR_NONE;
}

/** @internal @This handles the pipe control commands.
 *
 * @param upipe description structure of the pipe
 * @param command control command to handle
 * @param args optional arguments
 * @return an error code
 */
static int upipe_dvbcsa_enc_control(struct upipe *upipe,
                                    int command, va_list args)
{
    struct upipe_dvbcsa_enc *upipe_dvbcsa_enc =
        upipe_dvbcsa_enc_from_upipe(upipe);
    struct upipe_dvbcsa_common *common =
        upipe_dvbcsa_enc_to_common(upipe_dvbcsa_enc);

    UBASE_HANDLED_RETURN(upipe_dvbcsa_enc_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def= va_arg(args, struct uref *);
            return upipe_dvbcsa_enc_set_flow_def(upipe, flow_def);
        }

        case UPIPE_DVBCSA_SET_KEY: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_DVBCSA_COMMON_SIGNATURE);
            const char *key = va_arg(args, const char *);
            return upipe_dvbcsa_enc_set_key(upipe, key);
        }

        case UPIPE_DVBCSA_ADD_PID:
        case UPIPE_DVBCSA_DEL_PID:
            return upipe_dvbcsa_common_control(common, command, args);
    }
    return UBASE_ERR_UNHANDLED;
}

/** @internal @This is the dvbcsa encryption pipe management structure. */
static struct upipe_mgr upipe_dvbcsa_enc_mgr = {
    .signature = UPIPE_DVBCSA_ENC_SIGNATURE,
    .refcount = NULL,
    .upipe_alloc = upipe_dvbcsa_enc_alloc,
    .upipe_input = upipe_dvbcsa_enc_input,
    .upipe_control = upipe_dvbcsa_enc_control,
};

/** @This returns the dvbcsa encryption pipe management structure.
 *
 * @return a pointer to the management structure
 */
struct upipe_mgr *upipe_dvbcsa_enc_mgr_alloc(void)
{
    return &upipe_dvbcsa_enc_mgr;
}
