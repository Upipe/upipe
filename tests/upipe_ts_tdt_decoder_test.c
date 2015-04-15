/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
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
 * @short unit tests for TS TDT decoder module
 */

#undef NDEBUG

#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
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
#include <upipe-ts/upipe_ts_tdt_decoder.h>
#include <upipe-ts/uref_ts_flow.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>

#include <bitstream/dvb/si.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UBUF_POOL_DEPTH 0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

static uint64_t cr_sys = 0;
static uint64_t utc = 0;

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
            break;
        case UPROBE_CLOCK_UTC: {
            struct uref *uref = va_arg(args, struct uref *);
            assert(uref != NULL);
            ubase_assert(uref_clock_get_cr_sys(uref, &cr_sys));
            utc = va_arg(args, uint64_t);
            break;
        }
    }
    return UBASE_ERR_NONE;
}

int main(int argc, char *argv[])
{
    setenv("TZ", "UTC", 1);
    tzset();

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

    struct uref *uref;
    uref = uref_block_flow_alloc_def(uref_mgr, "mpegtspsi.mpegtstdt.");
    assert(uref != NULL);

    struct upipe_mgr *upipe_ts_tdtd_mgr = upipe_ts_tdtd_mgr_alloc();
    assert(upipe_ts_tdtd_mgr != NULL);
    struct upipe *upipe_ts_tdtd = upipe_void_alloc(upipe_ts_tdtd_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "ts tdtd"));
    assert(upipe_ts_tdtd != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_tdtd, uref));
    uref_free(uref);

    uint8_t *buffer;
    int size;
    struct tm tm;
    time_t time;

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, TDT_HEADER_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == TDT_HEADER_SIZE);
    tdt_init(buffer);
    tm.tm_year = 93;
    tm.tm_mon = 10 - 1;
    tm.tm_mday = 13;
    tm.tm_hour = 12;
    tm.tm_min = 45;
    tm.tm_sec = 0;
    tm.tm_isdst = 0;
    time = mktime(&tm);
    tdt_set_utc(buffer, dvb_time_encode_UTC(time));
    uref_block_unmap(uref, 0);
    uref_clock_set_cr_sys(uref, 42);
    upipe_input(upipe_ts_tdtd, uref, NULL);
    assert(cr_sys == 42);
    assert(utc == (uint64_t)time * UCLOCK_FREQ);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, TDT_HEADER_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == TDT_HEADER_SIZE);
    tdt_init(buffer);
    tm.tm_year = 115;
    tm.tm_mon = 4 - 1;
    tm.tm_mday = 15;
    tm.tm_hour = 14;
    tm.tm_min = 5;
    tm.tm_sec = 45;
    tm.tm_isdst = 0;
    time = mktime(&tm);
    tdt_set_utc(buffer, dvb_time_encode_UTC(time));
    uref_block_unmap(uref, 0);
    uref_clock_set_cr_sys(uref, UCLOCK_FREQ * 12);
    upipe_input(upipe_ts_tdtd, uref, NULL);
    assert(cr_sys == UCLOCK_FREQ * 12);
    assert(utc == (uint64_t)time * UCLOCK_FREQ);

    upipe_release(upipe_ts_tdtd);

    upipe_mgr_release(upipe_ts_tdtd_mgr); // nop

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(uprobe_stdio);
    uprobe_clean(&uprobe);

    return 0;
}
