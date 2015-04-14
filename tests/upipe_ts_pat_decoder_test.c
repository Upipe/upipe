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
 * @short unit tests for TS PAT decoder module
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
#include <upipe-ts/upipe_ts_pat_decoder.h>
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

static uint64_t tsid = 42;
static unsigned int program_sum;
static unsigned int pid_sum;
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
            uint64_t patd_systime;
            ubase_assert(uref_clock_get_rap_sys(uref, &patd_systime));
            assert(patd_systime == systime);
            systime = 0;
            break;
        }
        case UPROBE_NEW_FLOW_DEF: {
            struct uref *uref = va_arg(args, struct uref *);
            assert(uref != NULL);
            uint64_t patd_tsid;
            ubase_assert(uref_flow_get_id(uref, &patd_tsid));
            assert(patd_tsid == tsid);
            break;
        }
        case UPROBE_SPLIT_UPDATE: {
            struct uref *flow_def = NULL;
            while (ubase_check(upipe_split_iterate(upipe, &flow_def)) &&
                   flow_def != NULL) {
                uint64_t id;
                ubase_assert(uref_flow_get_id(flow_def, &id));
                uint64_t pid;
                ubase_assert(uref_ts_flow_get_pid(flow_def, &pid));
                program_sum -= id;
                pid_sum -= pid;
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
    uref = uref_block_flow_alloc_def(uref_mgr, "mpegtspsi.mpegtspat.");
    assert(uref != NULL);

    struct upipe_mgr *upipe_ts_patd_mgr = upipe_ts_patd_mgr_alloc();
    assert(upipe_ts_patd_mgr != NULL);
    struct upipe *upipe_ts_patd = upipe_void_alloc(upipe_ts_patd_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "ts patd"));
    assert(upipe_ts_patd != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_patd, uref));
    uref_free(uref);

    uint8_t *buffer, *pat_program;
    int size;

    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            PAT_HEADER_SIZE + PAT_PROGRAM_SIZE + PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
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
    uref_block_unmap(uref, 0);
    program_sum = 12;
    pid_sum = 42;
    uref_clock_set_cr_sys(uref, systime);
    upipe_input(upipe_ts_patd, uref, NULL);
    assert(!program_sum);
    assert(!pid_sum);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            PAT_HEADER_SIZE + PAT_PROGRAM_SIZE + PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
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
    uref_block_unmap(uref, 0);
    uref_clock_set_cr_sys(uref, systime);
    upipe_input(upipe_ts_patd, uref, NULL);
    assert(!program_sum);
    assert(!pid_sum);
    assert(!systime);

    tsid++;
    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            PAT_HEADER_SIZE + PAT_PROGRAM_SIZE + PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
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
    uref_block_unmap(uref, 0);
    systime = UINT32_MAX;
    uref_clock_set_cr_sys(uref, systime);
    upipe_input(upipe_ts_patd, uref, NULL);
    assert(!program_sum);
    assert(!pid_sum);
    assert(systime);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            PAT_HEADER_SIZE + PAT_PROGRAM_SIZE + PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
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
    uref_block_unmap(uref, 0);
    uref_clock_set_cr_sys(uref, systime);
    upipe_input(upipe_ts_patd, uref, NULL);
    assert(!program_sum);
    assert(!pid_sum);
    assert(systime);

    tsid++;
    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            PAT_HEADER_SIZE + PAT_PROGRAM_SIZE + PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
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
    uref_block_unmap(uref, 0);
    uref_clock_set_cr_sys(uref, systime);
    upipe_input(upipe_ts_patd, uref, NULL);
    assert(!program_sum);
    assert(!pid_sum);
    assert(systime);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            PAT_HEADER_SIZE + PAT_PROGRAM_SIZE + PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
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
    uref_block_unmap(uref, 0);
    program_sum = 12 + 13;
    pid_sum = 42 + 43;
    systime += UINT32_MAX;
    uref_clock_set_cr_sys(uref, systime);
    systime = UINT32_MAX;
    upipe_input(upipe_ts_patd, uref, NULL);
    assert(!program_sum);
    assert(!pid_sum);
    assert(!systime);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            PAT_HEADER_SIZE + PAT_PROGRAM_SIZE + PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
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
    uref_block_unmap(uref, 0);
    program_sum = 13;
    pid_sum = 43;
    upipe_input(upipe_ts_patd, uref, NULL);
    assert(!program_sum);
    assert(!pid_sum);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            PAT_HEADER_SIZE + PAT_PROGRAM_SIZE * 2 +
                            PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
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
    uref_block_unmap(uref, 0);
    program_sum = 13 + 14;
    pid_sum = 43 + 44;
    upipe_input(upipe_ts_patd, uref, NULL);
    assert(!program_sum);
    assert(!pid_sum);

    upipe_release(upipe_ts_patd);
    assert(!program_sum);
    assert(!pid_sum);

    upipe_mgr_release(upipe_ts_patd_mgr); // nop

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(uprobe_stdio);
    uprobe_clean(&uprobe);

    return 0;
}
