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
 * @short unit tests for TS split module
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
#include <upipe-ts/uref_ts_flow.h>
#include <upipe-ts/upipe_ts_split.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>

#include <bitstream/mpeg/ts.h>

#define UDICT_POOL_DEPTH 10
#define UREF_POOL_DEPTH 10
#define UBUF_POOL_DEPTH 10
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

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
        case UPROBE_TS_SPLIT_ADD_PID: {
            unsigned int signature = va_arg(args, unsigned int);
            unsigned int pid = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_SPLIT_SIGNATURE);
            assert(pid == 68 || pid == 69);
            break;
        }
        case UPROBE_TS_SPLIT_DEL_PID: {
            unsigned int signature = va_arg(args, unsigned int);
            unsigned int pid = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_SPLIT_SIGNATURE);
            assert(pid == 68 || pid == 69);
            break;
        }
    }
    return true;
}

struct ts_test {
    uint16_t pid;
    bool got_flow_def;
    bool got_packet;
    struct upipe upipe;
};

/** helper phony pipe to test upipe_ts_split */
static struct upipe *ts_test_alloc(struct upipe_mgr *mgr,
                                   struct uprobe *uprobe)
{
    struct ts_test *ts_test = malloc(sizeof(struct ts_test));
    assert(ts_test != NULL);
    upipe_init(&ts_test->upipe, mgr, uprobe);
    ts_test->got_flow_def = false;
    ts_test->got_packet = false;
    return &ts_test->upipe;
}

/** helper phony pipe to test upipe_ts_split */
static void ts_test_input(struct upipe *upipe, struct uref *uref,
                          struct upump *upump)
{
    struct ts_test *ts_test = container_of(upipe, struct ts_test, upipe);
    assert(uref != NULL);
    const char *def;
    if (uref_flow_get_def(uref, &def)) {
        assert(!ts_test->got_flow_def);
        ts_test->got_flow_def = true;
        uint64_t pid;
        assert(uref_ts_flow_get_pid(uref, &pid));
        ts_test->pid = pid;
        uref_free(uref);
        return;
    }

    assert(ts_test->got_flow_def);
    ts_test->got_packet = true;
    const uint8_t *buffer;
    int size = -1;
    assert(uref_block_read(uref, 0, &size, &buffer));
    assert(size == TS_SIZE); //because of the way we allocated it
    assert(ts_validate(buffer));
    assert(ts_get_pid(buffer) == ts_test->pid);
    uref_block_unmap(uref, 0, size);
    uref_free(uref);
}

/** helper phony pipe to test upipe_ts_split */
static void ts_test_free(struct upipe *upipe)
{
    struct ts_test *ts_test = container_of(upipe, struct ts_test, upipe);
    assert(ts_test->got_packet);
    upipe_clean(upipe);
    free(ts_test);
}

/** helper phony pipe to test upipe_ts_split */
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
    struct uprobe *uprobe_ts_log = uprobe_ts_log_alloc(log, UPROBE_LOG_DEBUG);
    assert(uprobe_ts_log != NULL);

    struct upipe *upipe_sink68 = upipe_alloc(&ts_test_mgr, log);
    assert(upipe_sink68 != NULL);

    struct upipe *upipe_sink69 = upipe_alloc(&ts_test_mgr, log);
    assert(upipe_sink69 != NULL);

    struct upipe_mgr *upipe_ts_split_mgr = upipe_ts_split_mgr_alloc();
    assert(upipe_ts_split_mgr != NULL);
    struct upipe *upipe_ts_split = upipe_alloc(upipe_ts_split_mgr,
            uprobe_pfx_adhoc_alloc(uprobe_ts_log, UPROBE_LOG_LEVEL,
                                   "ts split"));
    assert(upipe_ts_split != NULL);

    struct uref *uref;
    uref = uref_block_flow_alloc_def(uref_mgr, "mpegts.");
    assert(uref != NULL);

    assert(uref_ts_flow_set_pid(uref, 68));
    struct upipe *upipe_ts_split_output68 = upipe_alloc_output(upipe_ts_split,
            uprobe_pfx_adhoc_alloc(uprobe_ts_log, UPROBE_LOG_LEVEL,
                                   "ts split output 68"));
    assert(upipe_ts_split_output68 != NULL);
    assert(upipe_set_flow_def(upipe_ts_split_output68, uref));
    assert(upipe_set_output(upipe_ts_split_output68, upipe_sink68));

    assert(uref_ts_flow_set_pid(uref, 69));
    struct upipe *upipe_ts_split_output69 = upipe_alloc_output(upipe_ts_split,
            uprobe_pfx_adhoc_alloc(uprobe_ts_log, UPROBE_LOG_LEVEL,
                                   "ts split output 69"));
    assert(upipe_ts_split_output69 != NULL);
    assert(upipe_set_flow_def(upipe_ts_split_output69, uref));
    assert(upipe_set_output(upipe_ts_split_output69, upipe_sink69));
    uref_free(uref);

    uref = uref_block_flow_alloc_def(uref_mgr, "mpegts.");
    assert(uref != NULL);
    upipe_input(upipe_ts_split, uref, NULL);

    uint8_t *buffer;
    int size;
    uref = uref_block_alloc(uref_mgr, ubuf_mgr, TS_SIZE);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == TS_SIZE);
    ts_pad(buffer);
    ts_set_pid(buffer, 68);
    uref_block_unmap(uref, 0, size);
    upipe_input(upipe_ts_split, uref, NULL);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, TS_SIZE);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == TS_SIZE);
    ts_pad(buffer);
    ts_set_pid(buffer, 69);
    uref_block_unmap(uref, 0, size);
    upipe_input(upipe_ts_split, uref, NULL);

    upipe_release(upipe_ts_split_output68);
    upipe_release(upipe_ts_split_output69);
    upipe_release(upipe_ts_split);
    upipe_mgr_release(upipe_ts_split_mgr); // nop

    ts_test_free(upipe_sink68);
    ts_test_free(upipe_sink69);

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_log_free(log);
    uprobe_ts_log_free(uprobe_ts_log);
    uprobe_stdio_free(uprobe_stdio);

    return 0;
}
