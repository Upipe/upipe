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
 * @short unit tests for uprobe_select_programs implementation
 */

#undef NDEBUG

#include <upipe/urefcount.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_log.h>
#include <upipe/uprobe_select_programs.h>
#include <upipe/upipe.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_program_flow.h>
#include <upipe/uref_std.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#define UDICT_POOL_DEPTH 1
#define UREF_POOL_DEPTH 1

static uint64_t add_programs, del_programs;
static uint64_t add_es, del_es;

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
            add_es -= flow_id;
            break;
        }
        case UPROBE_SPLIT_DEL_FLOW: {
            uint64_t flow_id = va_arg(args, uint64_t);
            del_es -= flow_id;
            break;
        }
    }
    return true;
}

struct test_output {
    uint64_t program_number;
    urefcount refcount;
    struct upipe upipe;
};

/** helper phony pipe to test uprobe_select_programs */
static struct upipe *test_output_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe)
{
    struct test_output *test_output =
        malloc(sizeof(struct test_output));
    assert(test_output != NULL);
    upipe_init(&test_output->upipe, mgr, uprobe);
    test_output->program_number = UINT64_MAX;
    urefcount_init(&test_output->refcount);
    upipe_throw_ready(&test_output->upipe);
    return &test_output->upipe;
}

/** helper phony pipe to test uprobe_select_programs */
static bool test_output_control(struct upipe *upipe, enum upipe_command command,
                                va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct test_output *test_output =
                container_of(upipe, struct test_output, upipe);
            assert(test_output->program_number == UINT64_MAX);
            struct uref *flow_def = va_arg(args, struct uref *);
            const char *programs;
            assert(uref_flow_get_program(flow_def, &programs));
            assert(sscanf(programs, "%"PRIu64",",
                          &test_output->program_number) == 1);
            add_programs -= test_output->program_number;
            return true;
        }

        default:
            assert(0);
            return false;
    }
}

/** helper phony pipe to test upipe_avfsrc */
static void test_output_use(struct upipe *upipe)
{
    struct test_output *test_output =
        container_of(upipe, struct test_output, upipe);
    urefcount_use(&test_output->refcount);
}

/** helper phony pipe to test upipe_avfsrc */
static void test_output_release(struct upipe *upipe)
{
    struct test_output *test_output =
        container_of(upipe, struct test_output, upipe);
    if (unlikely(urefcount_release(&test_output->refcount))) {
        upipe_throw_dead(upipe);
        assert(test_output->program_number != UINT64_MAX);
        del_programs -= test_output->program_number;
        upipe_clean(upipe);
        free(test_output);
    }
}

/** helper phony pipe to test uprobe_select_programs */
static struct upipe_mgr test_output_mgr = {
    .upipe_alloc = test_output_alloc,
    .upipe_input = NULL,
    .upipe_control = test_output_control,
    .upipe_use = test_output_use,
    .upipe_release = test_output_release,

    .upipe_mgr_use = NULL,
    .upipe_mgr_release = NULL
};

/** helper phony pipe to test uprobe_select_programs */
static struct upipe *test_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_split_init(upipe, mgr, uprobe, &test_output_mgr);
    return upipe;
}

/** helper phony pipe to test uprobe_select_programs */
static void test_free(struct upipe *upipe)
{
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe to test uprobe_select_programs */
static struct upipe_mgr test_mgr = {
    .upipe_alloc = NULL,
    .upipe_input = NULL,
    .upipe_control = NULL,
    .upipe_use = NULL,
    .upipe_release = NULL,

    .upipe_mgr_use = NULL,
    .upipe_mgr_release = NULL
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
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_DEBUG);
    assert(uprobe_stdio != NULL);
    struct uprobe *log = uprobe_log_alloc(uprobe_stdio, UPROBE_LOG_DEBUG);
    assert(log != NULL);

    struct uprobe *uprobe_selprog = uprobe_selprog_alloc(log, "auto");
    assert(uprobe_selprog != NULL);

    struct upipe *upipe = test_alloc(&test_mgr, uprobe_selprog);

    struct uref *flow_def;
    const char *programs;
    flow_def = uref_program_flow_alloc_def(uref_mgr);
    assert(flow_def != NULL);
    assert(uref_flow_set_program(flow_def, "12,"));
    assert(uref_program_flow_set_name(flow_def, "A 1"));
    add_programs = 12;
    del_programs = 0;
    add_es = 0;
    del_es = 0;
    upipe_split_throw_add_flow(upipe, 12, flow_def);
    assert(!add_programs);
    assert(!del_programs);
    assert(!add_es);
    assert(!del_es);
    uprobe_selprog_get(uprobe_selprog, &programs);
    assert(!strcmp(programs, "auto"));
    uref_free(flow_def);

    flow_def = uref_program_flow_alloc_def(uref_mgr);
    assert(flow_def != NULL);
    assert(uref_flow_set_program(flow_def, "13,"));
    assert(uref_program_flow_set_name(flow_def, "B 2"));
    add_programs = 13;
    upipe_split_throw_add_flow(upipe, 13, flow_def);
    assert(!add_programs);
    assert(!del_programs);
    assert(!add_es);
    assert(!del_es);
    uprobe_selprog_get(uprobe_selprog, &programs);
    assert(!strcmp(programs, "auto"));
    uref_free(flow_def);

    flow_def = uref_block_flow_alloc_def(uref_mgr, "");
    assert(flow_def != NULL);
    assert(uref_flow_set_program(flow_def, "12,"));
    del_programs = 13;
    add_es = 42;
    upipe_split_throw_add_flow(upipe, 42, flow_def);
    assert(!add_programs);
    assert(!del_programs);
    assert(!add_es);
    assert(!del_es);
    uprobe_selprog_get(uprobe_selprog, &programs);
    assert(!strcmp(programs, "12,"));
    uref_free(flow_def);

    flow_def = uref_block_flow_alloc_def(uref_mgr, "");
    assert(flow_def != NULL);
    assert(uref_flow_set_program(flow_def, "13,"));
    upipe_split_throw_add_flow(upipe, 43, flow_def);
    assert(!add_programs);
    assert(!del_programs);
    assert(!add_es);
    assert(!del_es);
    uprobe_selprog_get(uprobe_selprog, &programs);
    assert(!strcmp(programs, "12,"));
    uprobe_selprog_list(uprobe_selprog, &programs);
    assert(!strcmp(programs, "12,13,"));
    uref_free(flow_def);

    del_es = 42;
    upipe_split_throw_del_flow(upipe, 42);
    assert(!add_programs);
    assert(!del_programs);
    assert(!add_es);
    assert(!del_es);
    add_programs = 13;
    del_programs = 12;
    add_es = 43;
    upipe_split_throw_del_flow(upipe, 12);
    assert(!add_programs);
    assert(!del_programs);
    assert(!add_es);
    assert(!del_es);
    uprobe_selprog_get(uprobe_selprog, &programs);
    assert(!strcmp(programs, "13,"));

    flow_def = uref_program_flow_alloc_def(uref_mgr);
    assert(flow_def != NULL);
    assert(uref_flow_set_program(flow_def, "12,"));
    assert(uref_program_flow_set_name(flow_def, "A 1"));
    upipe_split_throw_add_flow(upipe, 12, flow_def);
    assert(!add_programs);
    assert(!del_programs);
    assert(!add_es);
    assert(!del_es);
    uprobe_selprog_get(uprobe_selprog, &programs);
    assert(!strcmp(programs, "13,"));
    uref_free(flow_def);

    flow_def = uref_block_flow_alloc_def(uref_mgr, "");
    assert(flow_def != NULL);
    assert(uref_flow_set_program(flow_def, "12,"));
    upipe_split_throw_add_flow(upipe, 42, flow_def);
    assert(!add_programs);
    assert(!del_programs);
    assert(!add_es);
    assert(!del_es);
    uprobe_selprog_get(uprobe_selprog, &programs);
    assert(!strcmp(programs, "13,"));
    uref_free(flow_def);

    add_programs = 12;
    del_programs = 13;
    add_es = 42;
    del_es = 43;
    uprobe_selprog_set(uprobe_selprog, "12,");
    assert(!add_programs);
    assert(!del_programs);
    assert(!add_es);
    assert(!del_es);

    add_programs = 13;
    add_es = 43;
    uprobe_selprog_set(uprobe_selprog, "name=B 2,name=A 1,foo=bar,");
    assert(!add_programs);
    assert(!del_programs);
    assert(!add_es);
    assert(!del_es);

    del_programs = 12 + 13;
    del_es = 42 + 43;
    uprobe_selprog_set(uprobe_selprog, "14,");
    assert(!add_programs);
    assert(!del_programs);
    assert(!add_es);
    assert(!del_es);

    add_programs = 12 + 13;
    add_es = 42 + 43;
    uprobe_selprog_set(uprobe_selprog, "all");
    assert(!add_programs);
    assert(!del_programs);
    assert(!add_es);
    assert(!del_es);

    del_programs = 12 + 13;
    del_es = 42 + 43;
    upipe_split_throw_del_flow(upipe, 42);
    upipe_split_throw_del_flow(upipe, 43);
    upipe_split_throw_del_flow(upipe, 12);
    upipe_split_throw_del_flow(upipe, 13);
    assert(!add_programs);
    assert(!del_programs);
    assert(!add_es);
    assert(!del_es);

    test_free(upipe);

    uprobe_selprog_free(uprobe_selprog);
    uprobe_log_free(log);
    uprobe_stdio_free(uprobe_stdio);

    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    return 0;
}
