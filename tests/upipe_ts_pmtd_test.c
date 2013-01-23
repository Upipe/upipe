/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
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
 * @short unit tests for TS pmtd module
 */

#undef NDEBUG

#include <upipe/ulog.h>
#include <upipe/ulog_stdio.h>
#include <upipe/uprobe.h>
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
#include <upipe-ts/upipe_ts_pmtd.h>
#include <upipe-ts/uref_ts_flow.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>

#include <bitstream/mpeg/psi.h>

#define UDICT_POOL_DEPTH 10
#define UREF_POOL_DEPTH 10
#define UBUF_POOL_DEPTH 10
#define ULOG_LEVEL ULOG_DEBUG

static uint8_t program = 42;
static uint16_t pcrpid = 142;
static unsigned int header_desc_size = 0;
static unsigned int pid_sum;
static unsigned int streamtype_sum;
static unsigned int desc_offset_sum;
static unsigned int desc_size_sum;
static unsigned int del_pid_sum;

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
            break;
        case UPROBE_TS_PMTD_HEADER: {
            unsigned int signature = va_arg(args, unsigned int);
            struct uref *uref = va_arg(args, struct uref *);
            unsigned int pmtd_pcrpid = va_arg(args, unsigned int);
            unsigned int pmtd_desc_offset = va_arg(args, unsigned int);
            unsigned int pmtd_desc_size = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_PMTD_SIGNATURE);
            assert(uref != NULL);
            fprintf(stdout, "ts probe: pipe %p detected new PMT header (PCR PID:%u descs: %u)\n",
                    upipe, pmtd_pcrpid, pmtd_desc_size);
            assert(pmtd_pcrpid == pcrpid);
            assert(pmtd_desc_offset == PMT_HEADER_SIZE);
            assert(pmtd_desc_size == header_desc_size);
            pcrpid = 0;
            break;
        }
        case UPROBE_TS_PMTD_ADD_ES: {
            unsigned int signature = va_arg(args, unsigned int);
            struct uref *uref = va_arg(args, struct uref *);
            unsigned int pid = va_arg(args, unsigned int);
            unsigned int streamtype = va_arg(args, unsigned int);
            unsigned int pmtd_desc_offset = va_arg(args, unsigned int);
            unsigned int pmtd_desc_size = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_PMTD_SIGNATURE);
            assert(uref != NULL);
            pid_sum -= pid;
            streamtype_sum -= streamtype;
            desc_offset_sum -= pmtd_desc_offset;
            desc_size_sum -= pmtd_desc_size;
            fprintf(stdout,
                    "ts probe: pipe %p added PID %u (stream type 0x%x descs: %u at offset %u)\n",
                    upipe, pid, streamtype, pmtd_desc_size, pmtd_desc_offset);
            break;
        }
        case UPROBE_TS_PMTD_DEL_ES: {
            unsigned int signature = va_arg(args, unsigned int);
            struct uref *uref = va_arg(args, struct uref *);
            unsigned int pid = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_PMTD_SIGNATURE);
            assert(uref != NULL);
            del_pid_sum -= pid;
            fprintf(stdout,
                    "ts probe: pipe %p deleted PID %u\n", upipe, pid);
            break;
        }
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
    struct uprobe *uprobe_log = uprobe_log_alloc(&uprobe, ULOG_DEBUG);
    assert(uprobe_log != NULL);
    struct uprobe *uprobe_ts_log = uprobe_ts_log_alloc(uprobe_log, ULOG_DEBUG);
    assert(uprobe_ts_log != NULL);

    struct upipe_mgr *upipe_ts_pmtd_mgr = upipe_ts_pmtd_mgr_alloc();
    assert(upipe_ts_pmtd_mgr != NULL);
    struct upipe *upipe_ts_pmtd = upipe_alloc(upipe_ts_pmtd_mgr,
            uprobe_ts_log, ulog_stdio_alloc(stdout, ULOG_LEVEL, "ts pmtd"));
    assert(upipe_ts_pmtd != NULL);

    struct uref *uref;
    uint8_t *buffer, *pmt_es, *desc;
    int size;
    uref = uref_block_flow_alloc_def(uref_mgr, "mpegtspsi.mpegtspmt.");
    assert(uref != NULL);
    upipe_input(upipe_ts_pmtd, uref, NULL);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            PMT_HEADER_SIZE + PMT_ES_SIZE + PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
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
    pmtn_set_streamtype(pmt_es, 42);
    pmtn_set_desclength(pmt_es, 0);
    psi_set_crc(buffer);
    uref_block_unmap(uref, 0, size);
    pid_sum = 12;
    streamtype_sum = 42;
    del_pid_sum = 0;
    desc_offset_sum = PMT_HEADER_SIZE + PMT_ES_SIZE;
    desc_size_sum = 0;
    upipe_input(upipe_ts_pmtd, uref, NULL);
    assert(!pcrpid);
    assert(!pid_sum);
    assert(!streamtype_sum);
    assert(!del_pid_sum);
    assert(!desc_offset_sum);
    assert(!desc_size_sum);

    pcrpid = 142;
    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            PMT_HEADER_SIZE + PMT_ES_SIZE + PSI_CRC_SIZE + 5);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
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
    pmtn_set_streamtype(pmt_es, 42);
    pmtn_set_desclength(pmt_es, 5);
    desc = descs_get_desc(pmtn_get_descs(pmt_es), 0);
    desc_set_tag(desc, 0x42);
    desc_set_length(desc, 3);
    desc[2] = desc[3] = desc[4] = 0xff;
    psi_set_crc(buffer);
    uref_block_unmap(uref, 0, size);
    pid_sum = 12;
    streamtype_sum = 42;
    desc_offset_sum = PMT_HEADER_SIZE + PMT_ES_SIZE;
    desc_size_sum = 5;
    upipe_input(upipe_ts_pmtd, uref, NULL);
    assert(pcrpid == 142);
    assert(!pid_sum);
    assert(!streamtype_sum);
    assert(!del_pid_sum);
    assert(!desc_offset_sum);
    assert(!desc_size_sum);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            PMT_HEADER_SIZE + PMT_ES_SIZE + PSI_CRC_SIZE + 10);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
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
    pmtn_set_streamtype(pmt_es, 43);
    pmtn_set_desclength(pmt_es, 5);
    desc = descs_get_desc(pmtn_get_descs(pmt_es), 0);
    desc_set_tag(desc, 0x42);
    desc_set_length(desc, 3);
    desc[2] = desc[3] = desc[4] = 0xff;
    psi_set_crc(buffer);
    uref_block_unmap(uref, 0, size);
    header_desc_size = 5;
    del_pid_sum = 12;
    pid_sum = 13;
    streamtype_sum = 43;
    desc_offset_sum = PMT_HEADER_SIZE + PMT_ES_SIZE + 5;
    desc_size_sum = 5;
    upipe_input(upipe_ts_pmtd, uref, NULL);
    assert(!pcrpid);
    assert(!pid_sum);
    assert(!streamtype_sum);
    assert(!del_pid_sum);
    assert(!desc_offset_sum);
    assert(!desc_size_sum);

    pcrpid = 143;
    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            PMT_HEADER_SIZE + PMT_ES_SIZE + PSI_CRC_SIZE + 10);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
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
    pmtn_set_streamtype(pmt_es, 43);
    pmtn_set_desclength(pmt_es, 5);
    desc = descs_get_desc(pmtn_get_descs(pmt_es), 0);
    desc_set_tag(desc, 0x42);
    desc_set_length(desc, 3);
    desc[2] = desc[3] = desc[4] = 0xff;
    psi_set_crc(buffer);
    uref_block_unmap(uref, 0, size);
    header_desc_size = 5;
    upipe_input(upipe_ts_pmtd, uref, NULL);
    assert(!pcrpid);
    assert(!pid_sum);
    assert(!streamtype_sum);
    assert(!del_pid_sum);
    assert(!desc_offset_sum);
    assert(!desc_size_sum);

    pcrpid = 143;
    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            PMT_HEADER_SIZE + 2 * PMT_ES_SIZE + PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
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
    pmtn_set_streamtype(pmt_es, 43);
    pmtn_set_desclength(pmt_es, 0);
    pmt_es = pmt_get_es(buffer, 1);
    pmtn_init(pmt_es);
    pmtn_set_pid(pmt_es, 14);
    pmtn_set_streamtype(pmt_es, 43);
    pmtn_set_desclength(pmt_es, 0);
    psi_set_crc(buffer); //set invalid CRC
    pmtn_set_streamtype(pmt_es, 44);
    uref_block_unmap(uref, 0, size);
    header_desc_size = 0;
    upipe_input(upipe_ts_pmtd, uref, NULL);
    assert(pcrpid == 143);
    assert(!pid_sum);
    assert(!streamtype_sum);
    assert(!del_pid_sum);
    assert(!desc_offset_sum);
    assert(!desc_size_sum);

    pcrpid = 143;
    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            PMT_HEADER_SIZE + 2 * PMT_ES_SIZE + PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
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
    pmtn_set_streamtype(pmt_es, 43);
    pmtn_set_desclength(pmt_es, 0);
    pmt_es = pmt_get_es(buffer, 1);
    pmtn_init(pmt_es);
    pmtn_set_pid(pmt_es, 14);
    pmtn_set_streamtype(pmt_es, 44);
    pmtn_set_desclength(pmt_es, 0);
    psi_set_crc(buffer);
    uref_block_unmap(uref, 0, size);
    header_desc_size = 0;
    pid_sum = 13 + 14;
    streamtype_sum = 43 + 44;
    del_pid_sum = 0;
    desc_offset_sum = (PMT_HEADER_SIZE + PMT_ES_SIZE) * 2 + PMT_ES_SIZE;
    desc_size_sum = 0;
    upipe_input(upipe_ts_pmtd, uref, NULL);
    assert(!pcrpid);
    assert(!pid_sum);
    assert(!streamtype_sum);
    assert(!del_pid_sum);
    assert(!desc_offset_sum);
    assert(!desc_size_sum);

    pcrpid = 143;
    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            PMT_HEADER_SIZE + 2 * PMT_ES_SIZE + PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
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
    pmtn_set_streamtype(pmt_es, 42);
    pmtn_set_desclength(pmt_es, 0);
    pmt_es = pmt_get_es(buffer, 1);
    pmtn_init(pmt_es);
    pmtn_set_pid(pmt_es, 14);
    pmtn_set_streamtype(pmt_es, 44);
    pmtn_set_desclength(pmt_es, 0);
    psi_set_crc(buffer);
    uref_block_unmap(uref, 0, size);
    header_desc_size = 0;
    pid_sum = 12;
    streamtype_sum = 42;
    del_pid_sum = 13;
    desc_offset_sum = PMT_HEADER_SIZE + PMT_ES_SIZE;
    desc_size_sum = 0;
    upipe_input(upipe_ts_pmtd, uref, NULL);
    assert(pcrpid == 143);
    assert(!pid_sum);
    assert(!streamtype_sum);
    assert(!del_pid_sum);
    assert(!desc_offset_sum);
    assert(!desc_size_sum);

    upipe_release(upipe_ts_pmtd);
    upipe_mgr_release(upipe_ts_pmtd_mgr); // nop

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_log_free(uprobe_log);
    uprobe_ts_log_free(uprobe_ts_log);

    return 0;
}
