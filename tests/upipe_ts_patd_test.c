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
 * @short unit tests for TS patd module
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
#include <upipe-ts/upipe_ts_patd.h>
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
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

static uint8_t tsid = 42;
static unsigned int program_sum;
static unsigned int pid_sum;
static unsigned int del_program_sum;

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
        case UPROBE_TS_PATD_TSID: {
            unsigned int signature = va_arg(args, unsigned int);
            struct uref *uref = va_arg(args, struct uref *);
            unsigned int patd_tsid = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_PATD_SIGNATURE);
            assert(uref != NULL);
            assert(patd_tsid == tsid);
            break;
        }
        case UPROBE_TS_PATD_ADD_PROGRAM: {
            unsigned int signature = va_arg(args, unsigned int);
            struct uref *uref = va_arg(args, struct uref *);
            unsigned int program = va_arg(args, unsigned int);
            unsigned int pid = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_PATD_SIGNATURE);
            assert(uref != NULL);
            program_sum -= program;
            pid_sum -= pid;
            break;
        }
        case UPROBE_TS_PATD_DEL_PROGRAM: {
            unsigned int signature = va_arg(args, unsigned int);
            struct uref *uref = va_arg(args, struct uref *);
            unsigned int program = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_PATD_SIGNATURE);
            assert(uref != NULL);
            del_program_sum -= program;
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
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_LEVEL);
    assert(uprobe_stdio != NULL);
    struct uprobe *log = uprobe_log_alloc(uprobe_stdio, UPROBE_LOG_LEVEL);
    assert(log != NULL);
    struct uprobe *uprobe_ts_log = uprobe_ts_log_alloc(log, UPROBE_LOG_DEBUG);
    assert(uprobe_ts_log != NULL);

    struct upipe_mgr *upipe_ts_patd_mgr = upipe_ts_patd_mgr_alloc();
    assert(upipe_ts_patd_mgr != NULL);
    struct upipe *upipe_ts_patd = upipe_alloc(upipe_ts_patd_mgr,
            uprobe_pfx_adhoc_alloc(uprobe_ts_log, UPROBE_LOG_LEVEL, "ts patd"));
    assert(upipe_ts_patd != NULL);

    struct uref *uref;
    uint8_t *buffer, *pat_program;
    int size;
    uref = uref_block_flow_alloc_def(uref_mgr, "mpegtspsi.mpegtspat.");
    assert(uref != NULL);
    upipe_input(upipe_ts_patd, uref, NULL);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            PAT_HEADER_SIZE + PAT_PROGRAM_SIZE + PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == PAT_HEADER_SIZE + PAT_PROGRAM_SIZE + PSI_CRC_SIZE);
    pat_init(buffer);
    pat_set_length(buffer, PAT_PROGRAM_SIZE);
    pat_set_tsid(buffer, tsid);
    psi_set_version(buffer, 0);
    psi_set_current(buffer);
    psi_set_section(buffer, 0);
    psi_set_lastsection(buffer, 0);
    pat_program = pat_get_program(buffer, 0);
    patn_init(pat_program);
    patn_set_program(pat_program, 12);
    patn_set_pid(pat_program, 42);
    psi_set_crc(buffer);
    uref_block_unmap(uref, 0, size);
    program_sum = 12;
    pid_sum = 42;
    upipe_input(upipe_ts_patd, uref, NULL);
    assert(!program_sum);
    assert(!pid_sum);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            PAT_HEADER_SIZE + PAT_PROGRAM_SIZE + PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == PAT_HEADER_SIZE + PAT_PROGRAM_SIZE + PSI_CRC_SIZE);
    pat_init(buffer);
    pat_set_length(buffer, PAT_PROGRAM_SIZE);
    pat_set_tsid(buffer, tsid);
    psi_set_version(buffer, 1);
    psi_set_current(buffer);
    psi_set_section(buffer, 0);
    psi_set_lastsection(buffer, 0);
    pat_program = pat_get_program(buffer, 0);
    patn_init(pat_program);
    patn_set_program(pat_program, 12);
    patn_set_pid(pat_program, 12);
    psi_set_crc(buffer); /* set invalid CRC */
    patn_set_pid(pat_program, 42);
    uref_block_unmap(uref, 0, size);
    upipe_input(upipe_ts_patd, uref, NULL);
    assert(!program_sum);
    assert(!pid_sum);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            PAT_HEADER_SIZE + PAT_PROGRAM_SIZE + PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == PAT_HEADER_SIZE + PAT_PROGRAM_SIZE + PSI_CRC_SIZE);
    pat_init(buffer);
    pat_set_length(buffer, PAT_PROGRAM_SIZE);
    pat_set_tsid(buffer, tsid);
    psi_set_version(buffer, 2);
    // don't set current
    psi_set_section(buffer, 0);
    psi_set_lastsection(buffer, 0);
    pat_program = pat_get_program(buffer, 0);
    patn_init(pat_program);
    patn_set_program(pat_program, 12);
    patn_set_pid(pat_program, 42);
    psi_set_crc(buffer);
    uref_block_unmap(uref, 0, size);
    upipe_input(upipe_ts_patd, uref, NULL);
    assert(!program_sum);
    assert(!pid_sum);

    tsid++;
    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            PAT_HEADER_SIZE + PAT_PROGRAM_SIZE + PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == PAT_HEADER_SIZE + PAT_PROGRAM_SIZE + PSI_CRC_SIZE);
    pat_init(buffer);
    pat_set_length(buffer, PAT_PROGRAM_SIZE);
    pat_set_tsid(buffer, tsid);
    psi_set_version(buffer, 3);
    psi_set_current(buffer);
    psi_set_section(buffer, 0);
    psi_set_lastsection(buffer, 1);
    pat_program = pat_get_program(buffer, 0);
    patn_init(pat_program);
    patn_set_program(pat_program, 12);
    patn_set_pid(pat_program, 42);
    psi_set_crc(buffer);
    uref_block_unmap(uref, 0, size);
    upipe_input(upipe_ts_patd, uref, NULL);
    assert(!program_sum);
    assert(!pid_sum);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            PAT_HEADER_SIZE + PAT_PROGRAM_SIZE + PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == PAT_HEADER_SIZE + PAT_PROGRAM_SIZE + PSI_CRC_SIZE);
    pat_init(buffer);
    pat_set_length(buffer, PAT_PROGRAM_SIZE);
    pat_set_tsid(buffer, tsid);
    psi_set_version(buffer, 3);
    psi_set_current(buffer);
    psi_set_section(buffer, 1);
    psi_set_lastsection(buffer, 1);
    pat_program = pat_get_program(buffer, 0);
    patn_init(pat_program);
    patn_set_program(pat_program, 12);
    patn_set_pid(pat_program, 43); // invalid: program defined twice
    psi_set_crc(buffer);
    uref_block_unmap(uref, 0, size);
    upipe_input(upipe_ts_patd, uref, NULL);
    assert(!program_sum);
    assert(!pid_sum);

    tsid++;
    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            PAT_HEADER_SIZE + PAT_PROGRAM_SIZE + PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == PAT_HEADER_SIZE + PAT_PROGRAM_SIZE + PSI_CRC_SIZE);
    pat_init(buffer);
    pat_set_length(buffer, PAT_PROGRAM_SIZE);
    pat_set_tsid(buffer, tsid);
    psi_set_version(buffer, 4);
    psi_set_current(buffer);
    psi_set_section(buffer, 0);
    psi_set_lastsection(buffer, 1);
    pat_program = pat_get_program(buffer, 0);
    patn_init(pat_program);
    patn_set_program(pat_program, 12);
    patn_set_pid(pat_program, 42);
    psi_set_crc(buffer);
    uref_block_unmap(uref, 0, size);
    upipe_input(upipe_ts_patd, uref, NULL);
    assert(!program_sum);
    assert(!pid_sum);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            PAT_HEADER_SIZE + PAT_PROGRAM_SIZE + PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == PAT_HEADER_SIZE + PAT_PROGRAM_SIZE + PSI_CRC_SIZE);
    pat_init(buffer);
    pat_set_length(buffer, PAT_PROGRAM_SIZE);
    pat_set_tsid(buffer, tsid);
    psi_set_version(buffer, 4);
    psi_set_current(buffer);
    psi_set_section(buffer, 1);
    psi_set_lastsection(buffer, 1);
    pat_program = pat_get_program(buffer, 0);
    patn_init(pat_program);
    patn_set_program(pat_program, 13);
    patn_set_pid(pat_program, 43);
    psi_set_crc(buffer);
    uref_block_unmap(uref, 0, size);
    program_sum = 13; // the first program already exists
    pid_sum = 43;
    upipe_input(upipe_ts_patd, uref, NULL);
    assert(!program_sum);
    assert(!pid_sum);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            PAT_HEADER_SIZE + PAT_PROGRAM_SIZE + PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == PAT_HEADER_SIZE + PAT_PROGRAM_SIZE + PSI_CRC_SIZE);
    pat_init(buffer);
    pat_set_length(buffer, PAT_PROGRAM_SIZE);
    pat_set_tsid(buffer, tsid);
    psi_set_version(buffer, 5);
    psi_set_current(buffer);
    psi_set_section(buffer, 0);
    psi_set_lastsection(buffer, 0);
    pat_program = pat_get_program(buffer, 0);
    patn_init(pat_program);
    patn_set_program(pat_program, 13);
    patn_set_pid(pat_program, 43);
    psi_set_crc(buffer);
    uref_block_unmap(uref, 0, size);
    del_program_sum = 12;
    upipe_input(upipe_ts_patd, uref, NULL);
    assert(!del_program_sum);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            PAT_HEADER_SIZE + PAT_PROGRAM_SIZE * 2 +
                            PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == PAT_HEADER_SIZE + PAT_PROGRAM_SIZE * 2 + PSI_CRC_SIZE);
    pat_init(buffer);
    pat_set_length(buffer, PAT_PROGRAM_SIZE * 2);
    pat_set_tsid(buffer, tsid);
    psi_set_version(buffer, 5); // voluntarily set the same version
    psi_set_current(buffer);
    psi_set_section(buffer, 0);
    psi_set_lastsection(buffer, 0);
    pat_program = pat_get_program(buffer, 0);
    patn_init(pat_program);
    patn_set_program(pat_program, 13);
    patn_set_pid(pat_program, 43);
    pat_program = pat_get_program(buffer, 1);
    patn_init(pat_program);
    patn_set_program(pat_program, 14);
    patn_set_pid(pat_program, 44);
    psi_set_crc(buffer);
    uref_block_unmap(uref, 0, size);
    program_sum = 14;
    pid_sum = 44;
    upipe_input(upipe_ts_patd, uref, NULL);
    assert(!del_program_sum);

    upipe_release(upipe_ts_patd);
    upipe_mgr_release(upipe_ts_patd_mgr); // nop

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_log_free(log);
    uprobe_ts_log_free(uprobe_ts_log);
    uprobe_stdio_free(uprobe_stdio);

    return 0;
}
