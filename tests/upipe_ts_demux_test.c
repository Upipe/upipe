/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
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
 * @short unit tests for TS demux module
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_log.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_std.h>
#include <upipe/upipe.h>
#include <upipe-ts/uprobe_ts_log.h>
#include <upipe-ts/upipe_ts_demux.h>
#include <upipe-ts/upipe_ts_patd.h>
#include <upipe-ts/upipe_ts_pmtd.h>
#include <upipe-ts/uref_ts_flow.h>
#include <upipe-ts/upipe_ts_split.h>
#include <upipe-framers/upipe_mp2v_framer.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>

#include <bitstream/mpeg/ts.h>
#include <bitstream/mpeg/psi.h>
#include <bitstream/mpeg/pes.h>
#include <bitstream/mpeg/mp2v.h>

#define UDICT_POOL_DEPTH 10
#define UREF_POOL_DEPTH 10
#define UBUF_POOL_DEPTH 10
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

static struct upipe *upipe_ts_demux;
static struct upipe *upipe_ts_demux_output_pmt = NULL;
static struct upipe *upipe_ts_demux_output_video = NULL;
static struct uprobe *uprobe_ts_log;
static uint64_t wanted_flow_id, deleted_flow_id;
static bool expect_need_output = false;

/** definition of our uprobe */
static bool catch(struct uprobe *uprobe, struct upipe *upipe,
                  enum uprobe_event event, va_list args)
{
    switch (event) {
        default:
            assert(0);
            break;
        case UPROBE_READY:
        case UPROBE_DEAD:
        case UPROBE_SYNC_ACQUIRED:
        case UPROBE_SYNC_LOST:
        case UPROBE_TS_SPLIT_SET_PID:
        case UPROBE_TS_SPLIT_UNSET_PID:
        case UPROBE_TS_PATD_TSID:
        case UPROBE_TS_PATD_ADD_PROGRAM:
        case UPROBE_TS_PATD_DEL_PROGRAM:
        case UPROBE_TS_PMTD_HEADER:
        case UPROBE_TS_PMTD_ADD_ES:
        case UPROBE_TS_PMTD_DEL_ES:
        case UPROBE_READ_END:
            break;
        case UPROBE_SPLIT_ADD_FLOW: {
            uint64_t flow_id = va_arg(args, uint64_t);
            struct uref *uref = va_arg(args, struct uref *);
            assert(uref != NULL);
            const char *def;
            assert(uref_flow_get_def(uref, &def));
            assert(flow_id == wanted_flow_id);
            if (!ubase_ncmp(def, "internal.")) {
                if (upipe_ts_demux_output_pmt != NULL)
                    upipe_release(upipe_ts_demux_output_pmt);
                upipe_ts_demux_output_pmt = upipe_alloc_output(upipe_ts_demux,
                        uprobe_pfx_adhoc_alloc(uprobe_ts_log, UPROBE_LOG_LEVEL,
                                               "ts demux pmt"));
                assert(upipe_ts_demux_output_pmt != NULL);
                assert(upipe_set_flow_def(upipe_ts_demux_output_pmt, uref));
            } else if (!ubase_ncmp(def, "block.mpeg2video")) {
                if (upipe_ts_demux_output_video != NULL)
                    upipe_release(upipe_ts_demux_output_video);
                upipe_ts_demux_output_video =
                    upipe_alloc_output(upipe_ts_demux_output_pmt,
                        uprobe_pfx_adhoc_alloc(uprobe_ts_log, UPROBE_LOG_LEVEL,
                                               "ts demux video"));
                assert(upipe_ts_demux_output_video != NULL);
                assert(upipe_set_flow_def(upipe_ts_demux_output_video, uref));
            }
            break;
        }
        case UPROBE_SPLIT_DEL_FLOW: {
            uint64_t flow_id = va_arg(args, uint64_t);
            assert(flow_id == deleted_flow_id);
            break;
        }
        case UPROBE_NEED_OUTPUT:
            assert(expect_need_output);
            expect_need_output = false;
            break;
    }
    return true;
}

int main(int argc, char *argv[])
{
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    assert(uref_mgr != NULL);
    struct ubuf_mgr *ubuf_mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH,
                                                         UBUF_POOL_DEPTH,
                                                         umem_mgr, -1, -1,
                                                         -1, 0);
    assert(ubuf_mgr != NULL);
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_LEVEL);
    assert(uprobe_stdio != NULL);
    struct uprobe *log = uprobe_log_alloc(uprobe_stdio, UPROBE_LOG_LEVEL);
    assert(log != NULL);
    uprobe_ts_log = uprobe_ts_log_alloc(log, UPROBE_LOG_DEBUG);
    assert(uprobe_ts_log != NULL);

    struct upipe_mgr *upipe_mp2vf_mgr = upipe_mp2vf_mgr_alloc();
    assert(upipe_mp2vf_mgr != NULL);

    struct upipe_mgr *upipe_ts_demux_mgr = upipe_ts_demux_mgr_alloc();
    assert(upipe_ts_demux_mgr != NULL);
    assert(upipe_ts_demux_mgr_set_mp2vf_mgr(upipe_ts_demux_mgr,
                                            upipe_mp2vf_mgr));
    upipe_ts_demux = upipe_alloc(upipe_ts_demux_mgr,
            uprobe_pfx_adhoc_alloc(uprobe_ts_log, UPROBE_LOG_LEVEL,
                                   "ts demux"));
    assert(upipe_ts_demux != NULL);
    assert(upipe_set_uref_mgr(upipe_ts_demux, uref_mgr));

    struct uref *uref;
    uint8_t *buffer, *payload, *pat_program, *pmt_es;
    int size;
    uref = uref_block_flow_alloc_def(uref_mgr, "mpegts.");
    assert(uref != NULL);
    upipe_input(upipe_ts_demux, uref, NULL);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, TS_SIZE);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == TS_SIZE);
    ts_init(buffer);
    ts_set_unitstart(buffer);
    ts_set_pid(buffer, 0);
    ts_set_cc(buffer, 0);
    ts_set_payload(buffer);
    payload = ts_payload(buffer);
    *payload++ = 0; /* pointer_field */
    pat_init(payload);
    pat_set_length(payload, PAT_PROGRAM_SIZE);
    pat_set_tsid(payload, 42);
    psi_set_version(payload, 0);
    psi_set_current(payload);
    psi_set_section(payload, 0);
    psi_set_lastsection(payload, 0);
    pat_program = pat_get_program(payload, 0);
    patn_init(pat_program);
    patn_set_program(pat_program, 12);
    patn_set_pid(pat_program, 42);
    psi_set_crc(payload);
    payload += PAT_HEADER_SIZE + PAT_PROGRAM_SIZE + PSI_CRC_SIZE;
    *payload = 0xff;
    uref_block_unmap(uref, 0, size);
    wanted_flow_id = 12;
    upipe_input(upipe_ts_demux, uref, NULL);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, TS_SIZE);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == TS_SIZE);
    ts_init(buffer);
    ts_set_unitstart(buffer);
    ts_set_pid(buffer, 42);
    ts_set_cc(buffer, 0);
    ts_set_payload(buffer);
    payload = ts_payload(buffer);
    *payload++ = 0; /* pointer_field */
    pmt_init(payload);
    pmt_set_length(payload, PMT_ES_SIZE);
    pmt_set_program(payload, 12);
    psi_set_version(payload, 0);
    psi_set_current(payload);
    psi_set_section(payload, 0);
    psi_set_lastsection(payload, 0);
    pmt_set_pcrpid(payload, 43);
    pmt_set_desclength(payload, 0);
    pmt_es = pmt_get_es(payload, 0);
    pmtn_init(pmt_es);
    pmtn_set_pid(pmt_es, 43);
    pmtn_set_streamtype(pmt_es, 2);
    pmtn_set_desclength(pmt_es, 0);
    psi_set_crc(payload);
    payload += PMT_HEADER_SIZE + PMT_ES_SIZE + PSI_CRC_SIZE;
    *payload = 0xff;
    uref_block_unmap(uref, 0, size);
    wanted_flow_id = 43;
    upipe_input(upipe_ts_demux, uref, NULL);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, TS_SIZE);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == TS_SIZE);
    ts_init(buffer);
    ts_set_unitstart(buffer);
    ts_set_pid(buffer, 0);
    ts_set_cc(buffer, 1);
    ts_set_payload(buffer);
    payload = ts_payload(buffer);
    *payload++ = 0; /* pointer_field */
    pat_init(payload);
    pat_set_length(payload, PAT_PROGRAM_SIZE);
    pat_set_tsid(payload, 42);
    psi_set_version(payload, 1);
    psi_set_current(payload);
    psi_set_section(payload, 0);
    psi_set_lastsection(payload, 0);
    pat_program = pat_get_program(payload, 0);
    patn_init(pat_program);
    patn_set_program(pat_program, 13);
    patn_set_pid(pat_program, 42);
    psi_set_crc(payload);
    payload += PAT_HEADER_SIZE + PAT_PROGRAM_SIZE + PSI_CRC_SIZE;
    *payload = 0xff;
    uref_block_unmap(uref, 0, size);
    wanted_flow_id = 13;
    deleted_flow_id = 12;
    upipe_input(upipe_ts_demux, uref, NULL);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, TS_SIZE);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == TS_SIZE);
    ts_init(buffer);
    ts_set_unitstart(buffer);
    ts_set_pid(buffer, 42);
    ts_set_cc(buffer, 1);
    ts_set_payload(buffer);
    payload = ts_payload(buffer);
    *payload++ = 0; /* pointer_field */
    pmt_init(payload);
    pmt_set_length(payload, PMT_ES_SIZE);
    pmt_set_program(payload, 13);
    psi_set_version(payload, 0);
    psi_set_current(payload);
    psi_set_section(payload, 0);
    psi_set_lastsection(payload, 0);
    pmt_set_pcrpid(payload, 43);
    pmt_set_desclength(payload, 0);
    pmt_es = pmt_get_es(payload, 0);
    pmtn_init(pmt_es);
    pmtn_set_pid(pmt_es, 43);
    pmtn_set_streamtype(pmt_es, 2);
    pmtn_set_desclength(pmt_es, 0);
    psi_set_crc(payload);
    payload += PMT_HEADER_SIZE + PMT_ES_SIZE + PSI_CRC_SIZE;
    *payload = 0xff;
    uref_block_unmap(uref, 0, size);
    wanted_flow_id = 43;
    upipe_input(upipe_ts_demux, uref, NULL);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, TS_SIZE);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == TS_SIZE);
    ts_init(buffer);
    ts_set_unitstart(buffer);
    ts_set_pid(buffer, 43);
    ts_set_cc(buffer, 0);
    ts_set_adaptation(buffer, TS_SIZE - TS_HEADER_SIZE - PES_HEADER_SIZE_NOPTS -
            MP2VSEQ_HEADER_SIZE - MP2VSEQX_HEADER_SIZE - MP2VPIC_HEADER_SIZE -
            MP2VPICX_HEADER_SIZE - 4 - MP2VEND_HEADER_SIZE - 1);
    ts_set_payload(buffer);
    payload = ts_payload(buffer);
    pes_init(payload);
    pes_set_streamid(payload, PES_STREAM_ID_VIDEO_MPEG);
    pes_set_headerlength(payload, 0);
    pes_set_length(payload, MP2VSEQ_HEADER_SIZE + MP2VSEQX_HEADER_SIZE +
            MP2VPIC_HEADER_SIZE + MP2VPICX_HEADER_SIZE + 4 +
            MP2VEND_HEADER_SIZE + PES_HEADER_SIZE_NOPTS - PES_HEADER_SIZE);
    pes_set_dataalignment(payload);
    payload = pes_payload(payload);
    mp2vseq_init(payload);
    mp2vseq_set_horizontal(payload, 720);
    mp2vseq_set_vertical(payload, 576);
    mp2vseq_set_aspect(payload, MP2VSEQ_ASPECT_16_9);
    mp2vseq_set_framerate(payload, MP2VSEQ_FRAMERATE_25);
    mp2vseq_set_bitrate(payload, 2000000/400);
    mp2vseq_set_vbvbuffer(payload, 1835008/16/1024);
    payload += MP2VSEQ_HEADER_SIZE;

    mp2vseqx_init(payload);
    mp2vseqx_set_profilelevel(payload,
                              MP2VSEQX_PROFILE_MAIN | MP2VSEQX_LEVEL_MAIN);
    mp2vseqx_set_chroma(payload, MP2VSEQX_CHROMA_420);
    mp2vseqx_set_horizontal(payload, 0);
    mp2vseqx_set_vertical(payload, 0);
    mp2vseqx_set_bitrate(payload, 0);
    mp2vseqx_set_vbvbuffer(payload, 0);
    payload += MP2VSEQX_HEADER_SIZE;

    mp2vpic_init(payload);
    mp2vpic_set_temporalreference(payload, 0);
    mp2vpic_set_codingtype(payload, MP2VPIC_TYPE_I);
    mp2vpic_set_vbvdelay(payload, UINT16_MAX);
    payload += MP2VPIC_HEADER_SIZE;

    mp2vpicx_init(payload);
    mp2vpicx_set_fcode00(payload, 0);
    mp2vpicx_set_fcode01(payload, 0);
    mp2vpicx_set_fcode10(payload, 0);
    mp2vpicx_set_fcode11(payload, 0);
    mp2vpicx_set_intradc(payload, 0);
    mp2vpicx_set_structure(payload, MP2VPICX_FRAME_PICTURE);
    mp2vpicx_set_tff(payload);
    payload += MP2VPICX_HEADER_SIZE;

    mp2vstart_init(payload, 1);
    payload += 4;

    mp2vend_init(payload);
    uref_block_unmap(uref, 0, size);
    expect_need_output = true;
    upipe_input(upipe_ts_demux, uref, NULL);
    assert(!expect_need_output);

    upipe_release(upipe_ts_demux_output_video);
    upipe_release(upipe_ts_demux_output_pmt);
    upipe_release(upipe_ts_demux);
    upipe_mgr_release(upipe_ts_demux_mgr);
    upipe_mgr_release(upipe_mp2vf_mgr);

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_log_free(log);
    uprobe_ts_log_free(uprobe_ts_log);
    uprobe_stdio_free(uprobe_stdio);

    return 0;
}
