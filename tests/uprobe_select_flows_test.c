/*
 * Copyright (C) 2013-2014 OpenHeadend S.A.R.L.
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
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
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
#include <upipe/uref_program_flow.h>
#include <upipe/uref_std.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0

static uint64_t add_flows, del_flows;
static struct uchain flow_defs;

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
        case UPROBE_SPLIT_UPDATE:
            break;
    }
    return UBASE_ERR_NONE;
}

struct test_sub {
    struct urefcount urefcount;
    uint64_t flow_id;
    struct upipe upipe;
};

/** helper phony pipe to test uprobe_select_flows */
static void test_sub_free(struct urefcount *urefcount)
{
    struct test_sub *test_sub = container_of(urefcount, struct test_sub,
                                             urefcount);
    struct upipe *upipe = &test_sub->upipe;
    upipe_throw_dead(upipe);
    assert(test_sub->flow_id != UINT64_MAX);
    del_flows -= test_sub->flow_id;
    upipe_clean(upipe);
    free(test_sub);
}

/** helper phony pipe to test uprobe_select_flows */
static struct upipe *test_sub_alloc(struct upipe_mgr *mgr,
                                    struct uprobe *uprobe,
                                    uint32_t signature, va_list args)
{
    assert(signature == UPIPE_FLOW_SIGNATURE);
    struct uref *flow_def = va_arg(args, struct uref *);
    uint64_t flow_id;
    ubase_assert(uref_flow_get_id(flow_def, &flow_id));
    add_flows -= flow_id;

    struct test_sub *test_sub = malloc(sizeof(struct test_sub));
    assert(test_sub != NULL);
    upipe_init(&test_sub->upipe, mgr, uprobe);
    urefcount_init(&test_sub->urefcount, test_sub_free);
    test_sub->upipe.refcount = &test_sub->urefcount;
    test_sub->flow_id = flow_id;
    upipe_throw_ready(&test_sub->upipe);
    return &test_sub->upipe;
}

/** helper phony pipe to test uprobe_select_flows */
static struct upipe_mgr test_sub_mgr = {
    .refcount = NULL,
    .upipe_alloc = test_sub_alloc,
    .upipe_input = NULL,
    .upipe_control = NULL
};

/** helper phony pipe to test uprobe_select_flows */
static struct upipe *test_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_init(upipe, mgr, uprobe);
    upipe->refcount = NULL;
    ulist_init(&flow_defs);
    return upipe;
}

/** helper phony pipe to test uprobe_select_flows */
static int test_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_GET_SUB_MGR: {
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);
            assert(p != NULL);
            *p = &test_sub_mgr;
            return UBASE_ERR_NONE;
        }
        case UPIPE_SPLIT_ITERATE: {
            struct uref **p = va_arg(args, struct uref **);
            assert(p != NULL);
            struct uchain *uchain;
            if (*p != NULL)
                uchain = uref_to_uchain(*p);
            else
                uchain = &flow_defs;
            if (ulist_is_last(&flow_defs, uchain)) {
                *p = NULL;
                return UBASE_ERR_NONE;
            }
            *p = uref_from_uchain(uchain->next);
            return UBASE_ERR_NONE;
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** helper phony pipe to test uprobe_select_flows */
static void test_free(struct upipe *upipe)
{
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe to test uprobe_select_flows */
static struct upipe_mgr test_mgr = {
    .refcount = NULL,
    .upipe_alloc = NULL,
    .upipe_input = NULL,
    .upipe_control = test_control
};

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
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_DEBUG);
    assert(logger != NULL);

    /* programs */

    struct uprobe *uprobe_selflow = uprobe_selflow_alloc(uprobe_use(logger),
                                                 uprobe_use(logger),
                                                 UPROBE_SELFLOW_VOID, "auto");
    assert(uprobe_selflow != NULL);
    struct upipe *upipe = test_alloc(&test_mgr, uprobe_use(uprobe_selflow));

    struct uref *flow_def;
    const char *flows;

    flow_def = uref_program_flow_alloc_def(uref_mgr);
    assert(flow_def != NULL);
    ubase_assert(uref_flow_set_id(flow_def, 12));
    ubase_assert(uref_program_flow_set_name(flow_def, "A 1"));
    ulist_add(&flow_defs, uref_to_uchain(flow_def));
    add_flows = 12;
    del_flows = 0;
    upipe_split_throw_update(upipe);
    assert(!add_flows);
    assert(!del_flows);
    uprobe_selflow_get(uprobe_selflow, &flows);
    assert(!strcmp(flows, "12,"));

    flow_def = uref_program_flow_alloc_def(uref_mgr);
    assert(flow_def != NULL);
    ubase_assert(uref_flow_set_id(flow_def, 13));
    ubase_assert(uref_program_flow_set_name(flow_def, "B 2"));
    ulist_add(&flow_defs, uref_to_uchain(flow_def));
    upipe_split_throw_update(upipe);
    assert(!add_flows);
    assert(!del_flows);
    uprobe_selflow_get(uprobe_selflow, &flows);
    assert(!strcmp(flows, "12,"));

    add_flows = 13;
    del_flows = 12;
    uprobe_selflow_set(uprobe_selflow, "13,");
    assert(!add_flows);
    assert(!del_flows);
    uprobe_selflow_get(uprobe_selflow, &flows);
    assert(!strcmp(flows, "13,"));

    add_flows = 12;
    uprobe_selflow_set(uprobe_selflow, "name=B 2,name=A 1,foo=bar,");
    assert(!add_flows);
    assert(!del_flows);
    uprobe_selflow_get(uprobe_selflow, &flows);
    assert(!strcmp(flows, "name=B 2,name=A 1,foo=bar,"));

    del_flows = 12 + 13;
    uprobe_selflow_set(uprobe_selflow, "14");
    assert(!add_flows);
    assert(!del_flows);
    uprobe_selflow_get(uprobe_selflow, &flows);
    assert(!strcmp(flows, "14,"));

    add_flows = 12 + 13;
    uprobe_selflow_set(uprobe_selflow, "all");
    assert(!add_flows);
    assert(!del_flows);
    uprobe_selflow_get(uprobe_selflow, &flows);
    assert(!strcmp(flows, "all"));

    del_flows = 13;
    uprobe_selflow_set(uprobe_selflow, "auto");
    assert(!add_flows);
    assert(!del_flows);
    uprobe_selflow_get(uprobe_selflow, &flows);
    assert(!strcmp(flows, "12,"));

    del_flows = 12;
    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach (&flow_defs, uchain, uchain_tmp) {
        struct uref *flow_def = uref_from_uchain(uchain);
        ulist_delete(uchain);
        uref_free(flow_def);
    }
    upipe_split_throw_update(upipe);
    assert(!add_flows);
    assert(!del_flows);
    uprobe_selflow_get(uprobe_selflow, &flows);
    assert(!strcmp(flows, "auto"));

    uprobe_release(uprobe_selflow);
    uprobe_release(upipe->uprobe);

    /* pictures */

    uprobe_selflow = uprobe_selflow_alloc(uprobe_use(logger), uprobe_use(logger), UPROBE_SELFLOW_PIC, "auto");
    assert(uprobe_selflow != NULL);
    upipe->uprobe = uprobe_use(uprobe_selflow);

    flow_def = uref_sound_flow_alloc_def(uref_mgr, "s16.", 1, 1);
    assert(flow_def != NULL);
    ubase_assert(uref_flow_set_id(flow_def, 42));
    ulist_add(&flow_defs, uref_to_uchain(flow_def));
    add_flows = 0;
    del_flows = 0;
    upipe_split_throw_update(upipe);
    assert(!add_flows);
    assert(!del_flows);
    uprobe_selflow_get(uprobe_selflow, &flows);
    assert(!strcmp(flows, "auto"));

    flow_def = uref_pic_flow_alloc_def(uref_mgr, 1);
    assert(flow_def != NULL);
    ubase_assert(uref_flow_set_id(flow_def, 43));
    ulist_add(&flow_defs, uref_to_uchain(flow_def));
    add_flows = 43;
    upipe_split_throw_update(upipe);
    assert(!add_flows);
    assert(!del_flows);
    uprobe_selflow_get(uprobe_selflow, &flows);
    assert(!strcmp(flows, "43,"));

    flow_def = uref_block_flow_alloc_def(uref_mgr, "pic.");
    assert(flow_def != NULL);
    ubase_assert(uref_flow_set_id(flow_def, 44));
    ulist_add(&flow_defs, uref_to_uchain(flow_def));
    upipe_split_throw_update(upipe);
    assert(!add_flows);
    assert(!del_flows);
    uprobe_selflow_get(uprobe_selflow, &flows);
    assert(!strcmp(flows, "43,"));

    flow_def = uref_block_flow_alloc_def(uref_mgr, "pic.sub.");
    assert(flow_def != NULL);
    ubase_assert(uref_flow_set_id(flow_def, 45));
    ulist_add(&flow_defs, uref_to_uchain(flow_def));
    upipe_split_throw_update(upipe);
    assert(!add_flows);
    assert(!del_flows);
    uprobe_selflow_get(uprobe_selflow, &flows);
    assert(!strcmp(flows, "43,"));

    uchain = ulist_pop(&flow_defs);
    flow_def = uref_from_uchain(uchain);
    uref_free(flow_def);
    upipe_split_throw_update(upipe);
    assert(!add_flows);
    assert(!del_flows);

    uchain = ulist_pop(&flow_defs);
    flow_def = uref_from_uchain(uchain);
    uref_free(flow_def);
    add_flows = 44;
    del_flows = 43;
    upipe_split_throw_update(upipe);
    assert(!add_flows);
    assert(!del_flows);
    uprobe_selflow_get(uprobe_selflow, &flows);
    assert(!strcmp(flows, "44,"));

    flow_def = uref_pic_flow_alloc_def(uref_mgr, 1);
    assert(flow_def != NULL);
    ubase_assert(uref_flow_set_languages(flow_def, 1));
    ubase_assert(uref_flow_set_language(flow_def, "eng", 0));
    ubase_assert(uref_flow_set_id(flow_def, 46));
    ulist_add(&flow_defs, uref_to_uchain(flow_def));
    upipe_split_throw_update(upipe);
    assert(!add_flows);
    assert(!del_flows);
    uprobe_selflow_get(uprobe_selflow, &flows);
    assert(!strcmp(flows, "44,"));

    flow_def = uref_pic_flow_alloc_def(uref_mgr, 1);
    assert(flow_def != NULL);
    ubase_assert(uref_flow_set_languages(flow_def, 1));
    ubase_assert(uref_flow_set_language(flow_def, "fra", 0));
    ubase_assert(uref_flow_set_id(flow_def, 47));
    ulist_add(&flow_defs, uref_to_uchain(flow_def));
    upipe_split_throw_update(upipe);
    assert(!add_flows);
    assert(!del_flows);
    uprobe_selflow_get(uprobe_selflow, &flows);
    assert(!strcmp(flows, "44,"));

    add_flows = 47;
    del_flows = 44;
    uprobe_selflow_set(uprobe_selflow, "47,");
    assert(!add_flows);
    assert(!del_flows);
    uprobe_selflow_get(uprobe_selflow, &flows);
    assert(!strcmp(flows, "47,"));

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

    ulist_delete_foreach (&flow_defs, uchain, uchain_tmp) {
        struct uref *flow_def = uref_from_uchain(uchain);
        ulist_delete(uchain);
        uref_free(flow_def);
    }
    del_flows = 44 + 46 + 47;
    upipe_split_throw_update(upipe);
    assert(!add_flows);
    assert(!del_flows);

    test_free(upipe);

    uprobe_release(uprobe_selflow);
    uprobe_release(logger);
    uprobe_clean(&uprobe);

    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    return 0;
}
