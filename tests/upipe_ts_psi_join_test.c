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
 * @short unit tests for TS PSI join module
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
#include <upipe-ts/uref_ts_flow.h>
#include <upipe-ts/upipe_ts_psi_join.h>
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

static uint8_t received = 0;
static uint64_t octetrate = 0;
static uint64_t section_interval = 0;
static uint64_t latency = 0;

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
        case UPROBE_NEW_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            ubase_assert(uref_flow_match_def(flow_def, "block.mpegtspsi."));
            ubase_assert(uref_block_flow_get_octetrate(flow_def, &octetrate));
            ubase_assert(uref_ts_flow_get_psi_section_interval(flow_def,
                        &section_interval));
            ubase_assert(uref_clock_get_latency(flow_def, &latency));
            break;
        }
    }
    return UBASE_ERR_NONE;
}

/** helper phony pipe */
static struct upipe *test_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe,
                                uint32_t signature, va_list args)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_init(upipe, mgr, uprobe);
    return upipe;
}

/** helper phony pipe */
static void test_input(struct upipe *upipe, struct uref *uref,
                       struct upump **upump_p)
{
    assert(uref != NULL);
    const uint8_t *buffer;
    int size = -1;
    ubase_assert(uref_block_read(uref, 0, &size, &buffer));
    assert(size == PSI_HEADER_SIZE); //because of the way we allocated it
    received = psi_get_tableid(buffer);
    uref_block_unmap(uref, 0);
    uref_free(uref);
}

/** helper phony pipe */
static int test_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF:
            return UBASE_ERR_NONE;
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *urequest = va_arg(args, struct urequest *);
            return upipe_throw_provide_request(upipe, urequest);
        }
        case UPIPE_UNREGISTER_REQUEST:
            return UBASE_ERR_NONE;
        default:
            assert(0);
            return UBASE_ERR_UNHANDLED;
    }
}

/** helper phony pipe */
static void test_free(struct upipe *upipe)
{
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe */
static struct upipe_mgr ts_test_mgr = {
    .refcount = NULL,
    .upipe_alloc = test_alloc,
    .upipe_input = test_input,
    .upipe_control = test_control
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
                                                         umem_mgr, -1, 0);
    assert(ubuf_mgr != NULL);
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_LEVEL);
    assert(logger != NULL);

    struct upipe_mgr *upipe_ts_psi_join_mgr = upipe_ts_psi_join_mgr_alloc();
    assert(upipe_ts_psi_join_mgr != NULL);

    struct uref *uref = uref_block_flow_alloc_def(uref_mgr, "mpegtspsi.");
    assert(uref != NULL);
    ubase_assert(uref_block_flow_set_octetrate(uref, 1));
    ubase_assert(uref_ts_flow_set_psi_section_interval(uref, UCLOCK_FREQ));
    ubase_assert(uref_clock_set_latency(uref, 1));

    struct upipe *upipe_ts_psi_join = upipe_flow_alloc(upipe_ts_psi_join_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "ts join"),
            uref);
    assert(upipe_ts_psi_join != NULL);

    struct upipe *upipe_sink = upipe_void_alloc(&ts_test_mgr,
                                                uprobe_use(logger));
    assert(upipe_sink != NULL);
    ubase_assert(upipe_set_output(upipe_ts_psi_join, upipe_sink));

    struct upipe *upipe_ts_psi_join_input1 =
        upipe_void_alloc_sub(upipe_ts_psi_join,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                                   "ts join input 1"));
    assert(upipe_ts_psi_join_input1 != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_psi_join_input1, uref));
    assert(octetrate == 1);
    assert(section_interval == UCLOCK_FREQ);
    assert(latency == 1);

    octetrate = 0;
    section_interval = 0;
    latency = 0;
    struct upipe *upipe_ts_psi_join_input2 =
        upipe_void_alloc_sub(upipe_ts_psi_join,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                                   "ts join input 2"));
    assert(upipe_ts_psi_join_input2 != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_psi_join_input2, uref));
    assert(octetrate == 2);
    assert(section_interval == UCLOCK_FREQ / 2);
    assert(latency == 1);
    uref_free(uref);

    uint8_t *buffer;
    int size;
    uref = uref_block_alloc(uref_mgr, ubuf_mgr, PSI_HEADER_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == PSI_HEADER_SIZE);
    psi_init(buffer, false);
    psi_set_tableid(buffer, 1);
    psi_set_length(buffer, 0);
    uref_block_unmap(uref, 0);
    received = 0;
    upipe_input(upipe_ts_psi_join_input1, uref, NULL);
    assert(received == 1);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, PSI_HEADER_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == PSI_HEADER_SIZE);
    psi_init(buffer, false);
    psi_set_tableid(buffer, 2);
    psi_set_length(buffer, 0);
    uref_block_unmap(uref, 0);
    received = 0;
    upipe_input(upipe_ts_psi_join_input2, uref, NULL);
    assert(received == 2);

    upipe_release(upipe_ts_psi_join_input2);
    upipe_release(upipe_ts_psi_join_input1);

    upipe_release(upipe_ts_psi_join);
    upipe_mgr_release(upipe_ts_psi_join_mgr); // nop

    test_free(upipe_sink);

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe);

    return 0;
}
