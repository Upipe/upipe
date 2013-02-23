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
 * @short unit tests for TS psim module
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
#include <upipe-ts/upipe_ts_psi_merge.h>

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

static unsigned int nb_packets = 0;
static uint8_t tableid = 42;
static size_t payload_size = 12;
static bool expect_lost = false;
static bool expect_acquired = true;

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
        case UPROBE_SYNC_ACQUIRED:
            assert(expect_acquired);
            expect_acquired = false;
            break;
        case UPROBE_SYNC_LOST:
            assert(expect_lost);
            expect_lost = false;
            break;
    }
    return true;
}

/** helper phony pipe to test upipe_ts_psim */
static struct upipe *ts_test_alloc(struct upipe_mgr *mgr,
                                   struct uprobe *uprobe)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_init(upipe, mgr, uprobe);
    return upipe;
}

/** helper phony pipe to test upipe_ts_psim */
static void ts_test_input(struct upipe *upipe, struct uref *uref,
                          struct upump *upump)
{
    assert(uref != NULL);
    const char *def;
    if (uref_flow_get_def(uref, &def)) {
        uref_free(uref);
        return;
    }

    size_t size;
    assert(uref_block_size(uref, &size));
    assert(size == PSI_HEADER_SIZE + payload_size);
    const uint8_t *buffer;
    int wanted = 1;
    assert(uref_block_read(uref, 0, &wanted, &buffer));
    assert(wanted == 1);
    assert(*buffer == tableid);
    assert(uref_block_unmap(uref, 0, 1));
    uref_free(uref);
    nb_packets--;
}

/** helper phony pipe to test upipe_ts_psim */
static void ts_test_free(struct upipe *upipe)
{
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe to test upipe_ts_psim */
static struct upipe_mgr ts_test_mgr = {
    .upipe_alloc = ts_test_alloc,
    .upipe_input = ts_test_input,
    .upipe_control = NULL,
    .upipe_free = NULL,

    .upipe_mgr_free = NULL
};

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

    struct upipe *upipe_sink = upipe_alloc(&ts_test_mgr, log);
    assert(upipe_sink != NULL);

    struct upipe_mgr *upipe_ts_psim_mgr = upipe_ts_psim_mgr_alloc();
    assert(upipe_ts_psim_mgr != NULL);
    struct upipe *upipe_ts_psim = upipe_alloc(upipe_ts_psim_mgr,
            uprobe_pfx_adhoc_alloc(log, UPROBE_LOG_LEVEL, "ts psim"));
    assert(upipe_ts_psim != NULL);
    assert(upipe_set_output(upipe_ts_psim, upipe_sink));

    struct uref *uref;
    uint8_t *buffer;
    int size;
    uref = uref_block_flow_alloc_def(uref_mgr, "mpegtspsi.");
    assert(uref != NULL);
    upipe_input(upipe_ts_psim, uref, NULL);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 1 + PSI_HEADER_SIZE + 12);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == 1 + PSI_HEADER_SIZE + 12);
    buffer[0] = 0; /* pointer_field */
    psi_init(buffer + 1, false);
    psi_set_tableid(buffer + 1, tableid);
    psi_set_length(buffer + 1, 12);
    uref_block_unmap(uref, 0, size);
    assert(uref_block_set_start(uref));
    nb_packets++;
    upipe_input(upipe_ts_psim, uref, NULL);
    assert(!nb_packets);
    assert(!expect_acquired);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 12 + PSI_HEADER_SIZE + 12);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == 12 + PSI_HEADER_SIZE + 12);
    buffer[0] = 11; /* pointer_field */
    psi_init(buffer + 12, false);
    tableid++;
    psi_set_tableid(buffer + 12, tableid);
    psi_set_length(buffer + 12, 1);
    memset(buffer + 12 + PSI_HEADER_SIZE + 1, 0xff, 11);
    payload_size = 1;
    uref_block_unmap(uref, 0, size);
    assert(uref_block_set_start(uref));
    assert(uref_flow_set_discontinuity(uref));
    nb_packets++;
    expect_lost = true;
    expect_acquired = true;
    upipe_input(upipe_ts_psim, uref, NULL);
    assert(!nb_packets);
    assert(!expect_lost);
    assert(!expect_acquired);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 1 + PSI_HEADER_SIZE + 12);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == 1 + PSI_HEADER_SIZE + 12);
    buffer[0] = 0; /* pointer_field */
    psi_init(buffer + 1, false);
    tableid++;
    psi_set_tableid(buffer + 1, tableid);
    psi_set_length(buffer + 1, 12);
    uref_block_unmap(uref, 0, size);
    payload_size = 12;

    /* now cut it into pieces - two by two because the pointer_byte must be in
     * the same packet as table_id */
    nb_packets++;
    for (int i = 0; i < 1 + PSI_HEADER_SIZE + 12; i += 2) {
        struct uref *dup = uref_dup(uref);
        assert(dup != NULL);
        assert(uref_block_resize(dup, i, 2));
        if (!i)
            assert(uref_block_set_start(dup));
        upipe_input(upipe_ts_psim, dup, NULL);
    }
    assert(!nb_packets);
    uref_free(uref);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 42);
    assert(uref != NULL);
    /* do not increment nb_packets */
    expect_lost = true;
    upipe_input(upipe_ts_psim, uref, NULL);
    assert(!nb_packets);
    assert(!expect_lost);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 1 + PSI_HEADER_SIZE + 6);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == 1 + PSI_HEADER_SIZE + 6);
    buffer[0] = 0; /* pointer_field */
    psi_init(buffer + 1, false);
    tableid++;
    psi_set_tableid(buffer + 1, tableid);
    psi_set_length(buffer + 1, 12);
    uref_block_unmap(uref, 0, size);
    assert(uref_block_set_start(uref));
    /* do not increment nb_packets */
    expect_acquired = true;
    upipe_input(upipe_ts_psim, uref, NULL);
    assert(!nb_packets);
    assert(!expect_acquired);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 1 + 6 + PSI_HEADER_SIZE + 12);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == 1 + 6 + PSI_HEADER_SIZE + 12);
    buffer[0] = 6; /* pointer_field */
    psi_init(buffer + 1 + 6, false);
    psi_set_tableid(buffer + 1 + 6, tableid);
    psi_set_length(buffer + 1 + 6, 12);
    uref_block_unmap(uref, 0, size);
    assert(uref_block_set_start(uref));
    nb_packets += 2;
    payload_size = 12;
    upipe_input(upipe_ts_psim, uref, NULL);
    assert(!nb_packets);

    upipe_release(upipe_ts_psim);
    upipe_mgr_release(upipe_ts_psim_mgr); // nop

    ts_test_free(upipe_sink);

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_log_free(log);
    uprobe_stdio_free(uprobe_stdio);

    return 0;
}
