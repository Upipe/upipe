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
 * @short unit tests for TS PSI split module
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
#include <upipe-ts/uref_ts_flow.h>
#include <upipe-ts/upipe_ts_psi_split.h>

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

/** definition of our uprobe */
static bool catch(struct uprobe *uprobe, struct upipe *upipe,
                  enum uprobe_event event, va_list args)
{
    switch (event) {
        default:
            assert(event & UPROBE_HANDLED_FLAG);
            break;
        case UPROBE_READY:
        case UPROBE_DEAD:
            break;
        case UPROBE_NEW_FLOW_DEF: {
            struct uref *uref = va_arg(args, struct uref *);
            const uint8_t *filter;
            const uint8_t *mask;
            size_t size;
            assert(uref_ts_flow_get_psi_filter(uref, &filter, &mask, &size));
            assert(size == PSI_HEADER_SIZE_SYNTAX1);
            assert(psi_get_tableid(mask) == 0xff);
            break;
        }
    }
    return true;
}

struct ts_test {
    uint16_t table_id;
    unsigned int nb_packets;
    struct upipe upipe;
};

/** helper phony pipe to test upipe_ts_psi_split */
static struct upipe *ts_test_alloc(struct upipe_mgr *mgr,
                                   struct uprobe *uprobe, uint32_t signature,
                                   va_list args)
{
    struct ts_test *ts_test = malloc(sizeof(struct ts_test));
    assert(ts_test != NULL);
    upipe_init(&ts_test->upipe, mgr, uprobe);
    ts_test->table_id = 0;
    ts_test->nb_packets = 0;
    return &ts_test->upipe;
}

/** helper phony pipe to test upipe_ts_psi_split */
static void ts_test_set_table(struct upipe *upipe, uint16_t table_id)
{
    struct ts_test *ts_test = container_of(upipe, struct ts_test, upipe);
    ts_test->table_id = table_id;
}

/** helper phony pipe to test upipe_ts_psi_split */
static void ts_test_input(struct upipe *upipe, struct uref *uref,
                          struct upump *upump)
{
    struct ts_test *ts_test = container_of(upipe, struct ts_test, upipe);
    assert(uref != NULL);
    ts_test->nb_packets++;
    const uint8_t *buffer;
    int size = -1;
    assert(uref_block_read(uref, 0, &size, &buffer));
    assert(size == PSI_MAX_SIZE);
    assert(psi_get_tableid(buffer) == ts_test->table_id);
    if (ts_test->table_id == 69) {
        assert(psi_get_tableidext(buffer) == ts_test->table_id);
    }
    uref_block_unmap(uref, 0);
    uref_free(uref);
}

/** helper phony pipe to test upipe_ts_psi_split */
static void ts_test_free(struct upipe *upipe)
{
    struct ts_test *ts_test = container_of(upipe, struct ts_test, upipe);
    assert(ts_test->nb_packets == 1);
    upipe_clean(upipe);
    free(ts_test);
}

/** helper phony pipe to test upipe_ts_psi_split */
static struct upipe_mgr ts_test_mgr = {
    .refcount = NULL,
    .upipe_alloc = ts_test_alloc,
    .upipe_input = ts_test_input,
    .upipe_control = NULL
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
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_LEVEL);
    assert(uprobe_stdio != NULL);
    struct uprobe *log = uprobe_log_alloc(uprobe_stdio, UPROBE_LOG_LEVEL);
    assert(log != NULL);

    struct uref *uref;
    uref = uref_block_flow_alloc_def(uref_mgr, "mpegtspsi.");
    assert(uref != NULL);

    struct upipe_mgr *upipe_ts_psi_split_mgr = upipe_ts_psi_split_mgr_alloc();
    assert(upipe_ts_psi_split_mgr != NULL);
    struct upipe *upipe_ts_psi_split = upipe_void_alloc(upipe_ts_psi_split_mgr,
            uprobe_pfx_adhoc_alloc(log, UPROBE_LOG_LEVEL, "ts psi split"));
    assert(upipe_ts_psi_split != NULL);
    assert(upipe_set_flow_def(upipe_ts_psi_split, uref));

    uint8_t filter[PSI_HEADER_SIZE_SYNTAX1];
    memset(filter, 0, PSI_HEADER_SIZE_SYNTAX1);
    filter[1] = 0x80;
    uint8_t mask[PSI_HEADER_SIZE_SYNTAX1];
    memset(mask, 0, PSI_HEADER_SIZE_SYNTAX1);
    mask[1] = 0x80;
    psi_set_tableid(mask, 0xff);

    psi_set_tableid(filter, 68);
    assert(uref_ts_flow_set_psi_filter(uref, filter, mask,
                                       PSI_HEADER_SIZE_SYNTAX1));
    struct upipe *upipe_sink68 = upipe_void_alloc(&ts_test_mgr, log);
    assert(upipe_sink68 != NULL);
    ts_test_set_table(upipe_sink68, 68);

    struct upipe *upipe_ts_psi_split_output68 =
        upipe_flow_alloc_sub(upipe_ts_psi_split,
                uprobe_pfx_adhoc_alloc(log, UPROBE_LOG_LEVEL,
                                       "ts psi split output 68"), uref);
    assert(upipe_ts_psi_split_output68 != NULL);
    assert(upipe_set_output(upipe_ts_psi_split_output68, upipe_sink68));

    psi_set_tableid(filter, 69);
    psi_set_tableidext(mask, 0xff);
    psi_set_tableidext(filter, 69);
    assert(uref_ts_flow_set_psi_filter(uref, filter, mask,
                                       PSI_HEADER_SIZE_SYNTAX1));
    struct upipe *upipe_sink69 = upipe_void_alloc(&ts_test_mgr, log);
    assert(upipe_sink69 != NULL);
    ts_test_set_table(upipe_sink69, 69);

    struct upipe *upipe_ts_psi_split_output69 =
        upipe_flow_alloc_sub(upipe_ts_psi_split,
                uprobe_pfx_adhoc_alloc(log, UPROBE_LOG_LEVEL,
                                       "ts psi split output 69"), uref);
    assert(upipe_ts_psi_split_output69 != NULL);
    assert(upipe_set_output(upipe_ts_psi_split_output69, upipe_sink69));
    uref_free(uref);

    uint8_t *buffer;
    int size;
    uref = uref_block_alloc(uref_mgr, ubuf_mgr, PSI_MAX_SIZE);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == PSI_MAX_SIZE);
    psi_init(buffer, 1);
    psi_set_tableid(buffer, 68);
    psi_set_tableidext(buffer, 12);
    uref_block_unmap(uref, 0);
    upipe_input(upipe_ts_psi_split, uref, NULL);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, PSI_MAX_SIZE);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == PSI_MAX_SIZE);
    psi_init(buffer, 1);
    psi_set_tableid(buffer, 69);
    psi_set_tableidext(buffer, 12);
    uref_block_unmap(uref, 0);
    upipe_input(upipe_ts_psi_split, uref, NULL);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, PSI_MAX_SIZE);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == PSI_MAX_SIZE);
    psi_init(buffer, 1);
    psi_set_tableid(buffer, 69);
    psi_set_tableidext(buffer, 69);
    uref_block_unmap(uref, 0);
    upipe_input(upipe_ts_psi_split, uref, NULL);

    upipe_release(upipe_ts_psi_split_output68);
    upipe_release(upipe_ts_psi_split_output69);
    upipe_release(upipe_ts_psi_split);
    upipe_mgr_release(upipe_ts_psi_split_mgr); // nop

    ts_test_free(upipe_sink68);
    ts_test_free(upipe_sink69);

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_log_free(log);
    uprobe_stdio_free(uprobe_stdio);

    return 0;
}
