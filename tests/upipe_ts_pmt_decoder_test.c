/*
 * Copyright (C) 2012-2015 OpenHeadend S.A.R.L.
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
 * @short unit tests for TS PMT decoder module
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_ubuf_mem.h>
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
#include <upipe/uref_clock.h>
#include <upipe/uref_std.h>
#include <upipe/upipe.h>
#include <upipe-ts/upipe_ts_pmt_decoder.h>
#include <upipe-ts/uref_ts_flow.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>

#include <bitstream/mpeg/psi.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UBUF_POOL_DEPTH 0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

static uint8_t program = 42;
static uint16_t pcrpid = 142;
static unsigned int header_desc_size = 0;
static unsigned int pid_sum;
static unsigned int desc_size_sum;
static uint64_t systime = UINT32_MAX;

/** definition of our uprobe */
static int catch(struct uprobe *uprobe, struct upipe *upipe,
                 int event, va_list args)
{
    switch (event) {
        default:
            assert(0);
            break;
        case UPROBE_READY:
        case UPROBE_DEAD:
        case UPROBE_NEED_OUTPUT:
            break;
        case UPROBE_NEW_RAP: {
            struct uref *uref = va_arg(args, struct uref *);
            assert(uref != NULL);
            uint64_t pmtd_systime;
            ubase_assert(uref_clock_get_cr_sys(uref, &pmtd_systime));
            assert(pmtd_systime == systime);
            systime = 0;
            break;
        }
        case UPROBE_NEW_FLOW_DEF: {
            struct uref *uref = va_arg(args, struct uref *);
            assert(uref != NULL);
            uint64_t pmtd_pcrpid;
            ubase_assert(uref_ts_flow_get_pcr_pid(uref, &pmtd_pcrpid));
            const uint8_t *pmtd_desc;
            size_t pmtd_desc_size;
            ubase_assert(uref_ts_flow_get_descriptor(uref, &pmtd_desc,
                                                     &pmtd_desc_size, 0));
            fprintf(stdout, "ts probe: pipe %p detected new PMT header (PCR PID:%"PRIu64" descs: %zu)\n",
                    upipe, pmtd_pcrpid, pmtd_desc_size);
            assert(pmtd_pcrpid == pcrpid);
            assert(pmtd_desc_size == header_desc_size);
            pcrpid = 0;
            break;
        }
        case UPROBE_SPLIT_UPDATE: {
            struct uref *flow_def = NULL;
            while (ubase_check(upipe_split_iterate(upipe, &flow_def)) &&
                   flow_def != NULL) {
                uint64_t id;
                ubase_assert(uref_flow_get_id(flow_def, &id));
                const uint8_t *pmtd_desc;
                size_t pmtd_desc_size = 0;
                uref_ts_flow_get_descriptor(flow_def, &pmtd_desc,
                                            &pmtd_desc_size, 0);
                pid_sum -= id;
                desc_size_sum -= pmtd_desc_size;
            }
            break;
        }
    }
    return UBASE_ERR_NONE;
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
                                                         umem_mgr, -1, 0);
    assert(ubuf_mgr != NULL);
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_LEVEL);
    assert(uprobe_stdio != NULL);
    uprobe_stdio = uprobe_ubuf_mem_alloc(uprobe_stdio, umem_mgr,
                                         UBUF_POOL_DEPTH, UBUF_POOL_DEPTH);
    assert(uprobe_stdio != NULL);

    struct uref *uref;
    uref = uref_block_flow_alloc_def(uref_mgr, "mpegtspsi.mpegtspmt.");
    assert(uref != NULL);

    struct upipe_mgr *upipe_ts_pmtd_mgr = upipe_ts_pmtd_mgr_alloc();
    assert(upipe_ts_pmtd_mgr != NULL);
    struct upipe *upipe_ts_pmtd = upipe_void_alloc(upipe_ts_pmtd_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "ts pmtd"));
    assert(upipe_ts_pmtd != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_pmtd, uref));
    uref_free(uref);

    uint8_t *buffer, *pmt_es, *desc;
    int size;
    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            PMT_HEADER_SIZE + PMT_ES_SIZE + PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == PMT_HEADER_SIZE + PMT_ES_SIZE + PSI_CRC_SIZE);
    pmt_init(buffer);
    pmt_set_length(buffer, PMT_ES_SIZE);
    pmt_set_program(buffer, program);
    psi_set_version(buffer, 0);
    psi_set_current(buffer);
    pmt_set_pcrpid(buffer, pcrpid);
    pmt_set_desclength(buffer, 0);
    pmt_es = pmt_get_es(buffer, 0);
    pmtn_init(pmt_es);
    pmtn_set_pid(pmt_es, 12);
    pmtn_set_streamtype(pmt_es, PMT_STREAMTYPE_VIDEO_MPEG2);
    pmtn_set_desclength(pmt_es, 0);
    psi_set_crc(buffer);
    uref_block_unmap(uref, 0);
    pid_sum = 12;
    desc_size_sum = 0;
    uref_clock_set_cr_sys(uref, systime);
    upipe_input(upipe_ts_pmtd, uref, NULL);
    assert(!pcrpid);
    assert(!pid_sum);
    assert(!desc_size_sum);
    assert(!systime);

    pcrpid = 142;
    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            PMT_HEADER_SIZE + PMT_ES_SIZE + PSI_CRC_SIZE + 5);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == PMT_HEADER_SIZE + PMT_ES_SIZE + PSI_CRC_SIZE + 5);
    pmt_init(buffer);
    pmt_set_length(buffer, PMT_ES_SIZE + 5);
    pmt_set_program(buffer, program);
    psi_set_version(buffer, 1);
    psi_set_current(buffer);
    pmt_set_pcrpid(buffer, pcrpid);
    pmt_set_desclength(buffer, 0);
    pmt_es = pmt_get_es(buffer, 0);
    pmtn_init(pmt_es);
    pmtn_set_pid(pmt_es, 12);
    pmtn_set_streamtype(pmt_es, PMT_STREAMTYPE_VIDEO_MPEG2);
    pmtn_set_desclength(pmt_es, 5);
    desc = descs_get_desc(pmtn_get_descs(pmt_es), 0);
    desc_set_tag(desc, 0x42);
    desc_set_length(desc, 3);
    desc[2] = desc[3] = desc[4] = 0xff;
    psi_set_crc(buffer);
    uref_block_unmap(uref, 0);
    pid_sum = 12;
    desc_size_sum = 5;
    systime = 2 * UINT32_MAX;
    uref_clock_set_cr_sys(uref, systime);
    upipe_input(upipe_ts_pmtd, uref, NULL);
    assert(pcrpid == 142);
    assert(!pid_sum);
    assert(!desc_size_sum);
    assert(!systime);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            PMT_HEADER_SIZE + PMT_ES_SIZE + PSI_CRC_SIZE + 10);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == PMT_HEADER_SIZE + PMT_ES_SIZE + PSI_CRC_SIZE + 10);
    pmt_init(buffer);
    pmt_set_length(buffer, PMT_ES_SIZE + 10);
    pmt_set_program(buffer, program);
    psi_set_version(buffer, 2);
    psi_set_current(buffer);
    pmt_set_pcrpid(buffer, pcrpid);
    pmt_set_desclength(buffer, 5);
    desc = descs_get_desc(pmt_get_descs(buffer), 0);
    desc_set_tag(desc, 0x42);
    desc_set_length(desc, 3);
    desc[2] = desc[3] = desc[4] = 0xff;
    pmt_es = pmt_get_es(buffer, 0);
    pmtn_init(pmt_es);
    pmtn_set_pid(pmt_es, 13);
    pmtn_set_streamtype(pmt_es, PMT_STREAMTYPE_AUDIO_MPEG2);
    pmtn_set_desclength(pmt_es, 5);
    desc = descs_get_desc(pmtn_get_descs(pmt_es), 0);
    desc_set_tag(desc, 0x42);
    desc_set_length(desc, 3);
    desc[2] = desc[3] = desc[4] = 0xff;
    psi_set_crc(buffer);
    uref_block_unmap(uref, 0);
    header_desc_size = 5;
    pid_sum = 13;
    desc_size_sum = 5;
    systime = 3 * UINT32_MAX;
    uref_clock_set_cr_sys(uref, systime);
    upipe_input(upipe_ts_pmtd, uref, NULL);
    assert(!pcrpid);
    assert(!pid_sum);
    assert(!desc_size_sum);
    assert(!systime);

    pcrpid = 143;
    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            PMT_HEADER_SIZE + PMT_ES_SIZE + PSI_CRC_SIZE + 10);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == PMT_HEADER_SIZE + PMT_ES_SIZE + PSI_CRC_SIZE + 10);
    pmt_init(buffer);
    pmt_set_length(buffer, PMT_ES_SIZE + 10);
    pmt_set_program(buffer, program);
    psi_set_version(buffer, 2); //keep same version
    psi_set_current(buffer);
    pmt_set_pcrpid(buffer, pcrpid);
    pmt_set_desclength(buffer, 5);
    desc = descs_get_desc(pmt_get_descs(buffer), 0);
    desc_set_tag(desc, 0x42);
    desc_set_length(desc, 3);
    desc[2] = desc[3] = desc[4] = 0xff;
    pmt_es = pmt_get_es(buffer, 0);
    pmtn_init(pmt_es);
    pmtn_set_pid(pmt_es, 13);
    pmtn_set_streamtype(pmt_es, PMT_STREAMTYPE_AUDIO_MPEG2);
    pmtn_set_desclength(pmt_es, 5);
    desc = descs_get_desc(pmtn_get_descs(pmt_es), 0);
    desc_set_tag(desc, 0x42);
    desc_set_length(desc, 3);
    desc[2] = desc[3] = desc[4] = 0xff;
    psi_set_crc(buffer);
    uref_block_unmap(uref, 0);
    header_desc_size = 5;
    pid_sum = 13;
    desc_size_sum = 5;
    systime = 4 * UINT32_MAX;
    uref_clock_set_cr_sys(uref, systime);
    upipe_input(upipe_ts_pmtd, uref, NULL);
    assert(!pcrpid);
    assert(!pid_sum);
    assert(!desc_size_sum);
    assert(!systime);

    pcrpid = 143;
    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            PMT_HEADER_SIZE + 2 * PMT_ES_SIZE + PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == PMT_HEADER_SIZE + 2 * PMT_ES_SIZE + PSI_CRC_SIZE);
    pmt_init(buffer);
    pmt_set_length(buffer, 2 * PMT_ES_SIZE);
    pmt_set_program(buffer, program);
    psi_set_version(buffer, 3);
    psi_set_current(buffer);
    pmt_set_pcrpid(buffer, pcrpid);
    pmt_set_desclength(buffer, 0);
    pmt_es = pmt_get_es(buffer, 0);
    pmtn_init(pmt_es);
    pmtn_set_pid(pmt_es, 13);
    pmtn_set_streamtype(pmt_es, PMT_STREAMTYPE_AUDIO_MPEG2);
    pmtn_set_desclength(pmt_es, 0);
    pmt_es = pmt_get_es(buffer, 1);
    pmtn_init(pmt_es);
    pmtn_set_pid(pmt_es, 14);
    pmtn_set_streamtype(pmt_es, PMT_STREAMTYPE_AUDIO_MPEG2);
    pmtn_set_desclength(pmt_es, 0);
    psi_set_crc(buffer); //set invalid CRC
    pmtn_set_streamtype(pmt_es, PMT_STREAMTYPE_AUDIO_ADTS);
    uref_block_unmap(uref, 0);
    header_desc_size = 0;
    systime = 5 * UINT32_MAX;
    uref_clock_set_cr_sys(uref, systime);
    upipe_input(upipe_ts_pmtd, uref, NULL);
    assert(pcrpid == 143);
    assert(!pid_sum);
    assert(!desc_size_sum);
    assert(systime);

    pcrpid = 143;
    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            PMT_HEADER_SIZE + 2 * PMT_ES_SIZE + PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == PMT_HEADER_SIZE + 2 * PMT_ES_SIZE + PSI_CRC_SIZE);
    pmt_init(buffer);
    pmt_set_length(buffer, 2 * PMT_ES_SIZE);
    pmt_set_program(buffer, program);
    psi_set_version(buffer, 3);
    psi_set_current(buffer);
    pmt_set_pcrpid(buffer, pcrpid);
    pmt_set_desclength(buffer, 0);
    pmt_es = pmt_get_es(buffer, 0);
    pmtn_init(pmt_es);
    pmtn_set_pid(pmt_es, 13);
    pmtn_set_streamtype(pmt_es, PMT_STREAMTYPE_AUDIO_MPEG2);
    pmtn_set_desclength(pmt_es, 0);
    pmt_es = pmt_get_es(buffer, 1);
    pmtn_init(pmt_es);
    pmtn_set_pid(pmt_es, 14);
    pmtn_set_streamtype(pmt_es, PMT_STREAMTYPE_AUDIO_ADTS);
    pmtn_set_desclength(pmt_es, 0);
    psi_set_crc(buffer);
    uref_block_unmap(uref, 0);
    header_desc_size = 0;
    pid_sum = 13 + 14;
    desc_size_sum = 0;
    uref_clock_set_cr_sys(uref, systime);
    upipe_input(upipe_ts_pmtd, uref, NULL);
    assert(!pcrpid);
    assert(!pid_sum);
    assert(!desc_size_sum);
    assert(!systime);

    pcrpid = 143;
    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            PMT_HEADER_SIZE + 2 * PMT_ES_SIZE + PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == PMT_HEADER_SIZE + 2 * PMT_ES_SIZE + PSI_CRC_SIZE);
    pmt_init(buffer);
    pmt_set_length(buffer, 2 * PMT_ES_SIZE);
    pmt_set_program(buffer, program);
    psi_set_version(buffer, 3);
    psi_set_current(buffer);
    pmt_set_pcrpid(buffer, pcrpid);
    pmt_set_desclength(buffer, 0);
    pmt_es = pmt_get_es(buffer, 0);
    pmtn_init(pmt_es);
    pmtn_set_pid(pmt_es, 12);
    pmtn_set_streamtype(pmt_es, PMT_STREAMTYPE_VIDEO_MPEG2);
    pmtn_set_desclength(pmt_es, 0);
    pmt_es = pmt_get_es(buffer, 1);
    pmtn_init(pmt_es);
    pmtn_set_pid(pmt_es, 14);
    pmtn_set_streamtype(pmt_es, PMT_STREAMTYPE_AUDIO_ADTS);
    pmtn_set_desclength(pmt_es, 0);
    psi_set_crc(buffer);
    uref_block_unmap(uref, 0);
    header_desc_size = 0;
    pid_sum = 12 + 14;
    desc_size_sum = 0;
    systime = 6 * UINT32_MAX;
    uref_clock_set_cr_sys(uref, systime);
    upipe_input(upipe_ts_pmtd, uref, NULL);
    assert(pcrpid == 143);
    assert(!pid_sum);
    assert(!desc_size_sum);
    assert(!systime);

    upipe_release(upipe_ts_pmtd);
    assert(!pid_sum);
    assert(!desc_size_sum);
    assert(!systime);

    upipe_mgr_release(upipe_ts_pmtd_mgr); // nop

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(uprobe_stdio);
    uprobe_clean(&uprobe);

    return 0;
}
