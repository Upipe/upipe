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
 * @short unit tests for uprobe_select_flows implementation
 */

#undef NDEBUG

#include <upipe/urefcount.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_log.h>
#include <upipe/uprobe_select_flows.h>
#include <upipe/upipe.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_std.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#define UDICT_POOL_DEPTH 1
#define UREF_POOL_DEPTH 1

static uint64_t add_flows, del_flows;

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
        case UPROBE_SPLIT_ADD_FLOW: {
            uint64_t flow_id = va_arg(args, uint64_t);
            add_flows -= flow_id;
            break;
        }
        case UPROBE_SPLIT_DEL_FLOW: {
            uint64_t flow_id = va_arg(args, uint64_t);
            del_flows -= flow_id;
            break;
        }
    }
    return true;
}

int main(int argc, char **argv)
{
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    assert(uref_mgr != NULL);

    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_DEBUG);
    assert(uprobe_stdio != NULL);
    struct uprobe *log = uprobe_log_alloc(uprobe_stdio, UPROBE_LOG_DEBUG);
    assert(log != NULL);

    struct uprobe *uprobe_selflow = uprobe_selflow_alloc(log,
                                                 UPROBE_SELFLOW_PIC, "auto");
    assert(uprobe_selflow != NULL);

    struct upipe test_pipe;
    test_pipe.uprobe = uprobe_selflow;
    struct upipe *upipe = &test_pipe;

    struct uref *flow_def;
    const char *flows;
    flow_def = uref_sound_flow_alloc_def(uref_mgr, 1, 1);
    assert(flow_def != NULL);
    add_flows = 42;
    del_flows = 0;
    upipe_split_throw_add_flow(upipe, 42, flow_def);
    assert(!add_flows);
    assert(!del_flows);
    uprobe_selflow_get(uprobe_selflow, &flows);
    assert(!strcmp(flows, "auto"));
    uprobe_selflow_list(uprobe_selflow, &flows);
    assert(!strcmp(flows, ""));
    uref_free(flow_def);

    flow_def = uref_pic_flow_alloc_def(uref_mgr, 1);
    assert(flow_def != NULL);
    add_flows = 43;
    upipe_split_throw_add_flow(upipe, 43, flow_def);
    assert(!add_flows);
    assert(!del_flows);
    uprobe_selflow_get(uprobe_selflow, &flows);
    assert(!strcmp(flows, "43,"));
    uprobe_selflow_list(uprobe_selflow, &flows);
    assert(!strcmp(flows, "43,"));
    uref_free(flow_def);

    flow_def = uref_block_flow_alloc_def(uref_mgr, "pic.");
    assert(flow_def != NULL);
    upipe_split_throw_add_flow(upipe, 44, flow_def);
    assert(!add_flows);
    assert(!del_flows);
    uprobe_selflow_get(uprobe_selflow, &flows);
    assert(!strcmp(flows, "43,"));
    uprobe_selflow_list(uprobe_selflow, &flows);
    assert(!strcmp(flows, "43,44,"));
    uref_free(flow_def);

    flow_def = uref_block_flow_alloc_def(uref_mgr, "pic.sub.");
    assert(flow_def != NULL);
    add_flows = 45;
    upipe_split_throw_add_flow(upipe, 45, flow_def);
    assert(!add_flows);
    assert(!del_flows);
    uprobe_selflow_get(uprobe_selflow, &flows);
    assert(!strcmp(flows, "43,"));
    uprobe_selflow_list(uprobe_selflow, &flows);
    assert(!strcmp(flows, "43,44,"));
    uref_free(flow_def);

    add_flows = 44;
    del_flows = 43;
    upipe_split_throw_del_flow(upipe, 43);
    assert(!add_flows);
    assert(!del_flows);
    uprobe_selflow_get(uprobe_selflow, &flows);
    assert(!strcmp(flows, "44,"));
    uprobe_selflow_list(uprobe_selflow, &flows);
    assert(!strcmp(flows, "44,"));

    del_flows = 42;
    upipe_split_throw_del_flow(upipe, 42);
    assert(!add_flows);
    assert(!del_flows);

    flow_def = uref_pic_flow_alloc_def(uref_mgr, 1);
    assert(flow_def != NULL);
    assert(uref_flow_set_lang(flow_def, "eng"));
    upipe_split_throw_add_flow(upipe, 46, flow_def);
    assert(!add_flows);
    assert(!del_flows);
    uprobe_selflow_get(uprobe_selflow, &flows);
    assert(!strcmp(flows, "44,"));
    uprobe_selflow_list(uprobe_selflow, &flows);
    assert(!strcmp(flows, "44,46,"));
    uref_free(flow_def);

    flow_def = uref_pic_flow_alloc_def(uref_mgr, 1);
    assert(flow_def != NULL);
    assert(uref_flow_set_lang(flow_def, "fra"));
    upipe_split_throw_add_flow(upipe, 47, flow_def);
    assert(!add_flows);
    assert(!del_flows);
    uprobe_selflow_get(uprobe_selflow, &flows);
    assert(!strcmp(flows, "44,"));
    uprobe_selflow_list(uprobe_selflow, &flows);
    assert(!strcmp(flows, "44,46,47,"));
    uref_free(flow_def);

    add_flows = 47;
    del_flows = 44;
    uprobe_selflow_set(uprobe_selflow, "47,");
    assert(!add_flows);
    assert(!del_flows);

    add_flows = 44 + 46;
    del_flows = 47;
    uprobe_selflow_set(uprobe_selflow, "44,lang=eng,");
    assert(!add_flows);
    assert(!del_flows);

    add_flows = 47;
    del_flows = 44 + 46;
    uprobe_selflow_set(uprobe_selflow, "lang=fra,88,foo=bar,");
    assert(!add_flows);
    assert(!del_flows);

    add_flows = 44 + 46;
    uprobe_selflow_set(uprobe_selflow, "all");
    assert(!add_flows);
    assert(!del_flows);

    del_flows = 45;
    upipe_split_throw_del_flow(upipe, 45);
    assert(!add_flows);
    assert(!del_flows);

    del_flows = 44 + 46 + 47;
    upipe_split_throw_del_flow(upipe, 44);
    upipe_split_throw_del_flow(upipe, 46);
    upipe_split_throw_del_flow(upipe, 47);
    assert(!add_flows);
    assert(!del_flows);

    uprobe_selflow_free(uprobe_selflow);
    uprobe_log_free(log);
    uprobe_stdio_free(uprobe_stdio);

    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    return 0;
}
