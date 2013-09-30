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
 * @short unit tests for trick play pipes
 */

#undef NDEBUG

#include <upipe/urefcount.h>
#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_log.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/upipe.h>
#include <upipe-modules/upipe_trickplay.h>

#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#define UDICT_POOL_DEPTH 10
#define UREF_POOL_DEPTH 10
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG
#define UPIPE_TRICKP_PTS_DELAY (UCLOCK_FREQ / 10)

static unsigned int count_pic = 0;
static unsigned int count_sound = 0;
static unsigned int count_subpic = 0;

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
        case UPROBE_NEW_FLOW_DEF:
        case UPROBE_SOURCE_END:
            break;
    }
    return true;
}

/** helper phony pipe to test upipe_trickp */
struct test_pipe {
    unsigned int *count_p;
    struct upipe upipe;
};

/** helper phony pipe to test upipe_trickp */
static struct upipe *trickp_test_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    struct test_pipe *test_pipe = malloc(sizeof(struct test_pipe));
    assert(test_pipe != NULL);
    upipe_init(&test_pipe->upipe, mgr, uprobe);
    struct uref *flow_def = va_arg(args, struct uref *);
    const char *def;
    assert(uref_flow_get_def(flow_def, &def));
    if (!strcmp(def, "pic."))
        test_pipe->count_p = &count_pic;
    else if (!strcmp(def, "block.pcm_s16le.sound."))
        test_pipe->count_p = &count_sound;
    else
        test_pipe->count_p = &count_subpic;
    return &test_pipe->upipe;
}

/** helper phony pipe to test upipe_trickp */
static void trickp_test_input(struct upipe *upipe, struct uref *uref,
                              struct upump *upump)
{
    struct test_pipe *test_pipe = container_of(upipe, struct test_pipe, upipe);
    assert(uref != NULL);
    uint64_t systime;
    if (uref_clock_get_pts_sys(uref, &systime))
        *test_pipe->count_p += systime - UPIPE_TRICKP_PTS_DELAY;
    if (uref_clock_get_dts_sys(uref, &systime))
        *test_pipe->count_p += systime - UPIPE_TRICKP_PTS_DELAY;
    uref_free(uref);
}

/** helper phony pipe to test upipe_trickp */
static void trickp_test_free(struct upipe *upipe)
{
    struct test_pipe *test_pipe = container_of(upipe, struct test_pipe, upipe);
    upipe_clean(upipe);
    free(test_pipe);
}

/** helper phony pipe to test upipe_trickp */
static struct upipe_mgr trickp_test_mgr = {
    .upipe_alloc = trickp_test_alloc,
    .upipe_input = trickp_test_input,
    .upipe_control = NULL,
    .upipe_free = NULL,

    .upipe_mgr_free = NULL
};

/** helper uclock to test upipe_trickp */
static uint64_t now(struct uclock *unused)
{
    return 42;
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
    struct uref *uref;
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_LEVEL);
    assert(uprobe_stdio != NULL);
    struct uprobe *log = uprobe_log_alloc(uprobe_stdio, UPROBE_LOG_LEVEL);
    assert(log != NULL);

    struct uclock *uclock = malloc(sizeof(struct uclock));
    urefcount_init(&uclock->refcount);
    uclock->uclock_now = now;
    uclock->uclock_free = NULL;

    struct upipe_mgr *upipe_trickp_mgr = upipe_trickp_mgr_alloc();
    assert(upipe_trickp_mgr != NULL);
    struct upipe *upipe_trickp = upipe_void_alloc(upipe_trickp_mgr,
            uprobe_pfx_adhoc_alloc(log, UPROBE_LOG_LEVEL, "trickp"));
    assert(upipe_trickp != NULL);
    assert(upipe_set_uclock(upipe_trickp, uclock));

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    assert(uref_flow_set_def(uref, "pic."));

    struct upipe *upipe_sink_pic = upipe_flow_alloc(&trickp_test_mgr, log,
                                                    uref);
    assert(upipe_sink_pic != NULL);

    struct upipe *upipe_trickp_pic = upipe_flow_alloc_sub(upipe_trickp,
            uprobe_pfx_adhoc_alloc(log, UPROBE_LOG_LEVEL, "trickp pic"), uref);
    assert(upipe_trickp_pic != NULL);
    uref_free(uref);
    assert(upipe_set_output(upipe_trickp_pic, upipe_sink_pic));

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    assert(uref_flow_set_def(uref, "block.pcm_s16le.sound."));

    struct upipe *upipe_sink_sound = upipe_flow_alloc(&trickp_test_mgr, log,
                                                      uref);
    assert(upipe_sink_sound != NULL);

    struct upipe *upipe_trickp_sound = upipe_flow_alloc_sub(upipe_trickp,
            uprobe_pfx_adhoc_alloc(log, UPROBE_LOG_LEVEL, "trickp sound"), uref);
    assert(upipe_trickp_sound != NULL);
    uref_free(uref);
    assert(upipe_set_output(upipe_trickp_sound, upipe_sink_sound));

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    assert(uref_flow_set_def(uref, "pic.sub."));

    struct upipe *upipe_sink_subpic = upipe_flow_alloc(&trickp_test_mgr, log,
                                                    uref);
    assert(upipe_sink_subpic != NULL);

    struct upipe *upipe_trickp_subpic = upipe_flow_alloc_sub(upipe_trickp,
            uprobe_pfx_adhoc_alloc(log, UPROBE_LOG_LEVEL, "trickp subpic"), uref);
    assert(upipe_trickp_subpic != NULL);
    uref_free(uref);
    assert(upipe_set_output(upipe_trickp_subpic, upipe_sink_subpic));

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    uref_clock_set_pts_prog(uref, (uint64_t)UINT32_MAX);
    upipe_input(upipe_trickp_pic, uref, NULL);
    assert(count_pic == 0);
    assert(count_sound == 0);
    assert(count_subpic == 0);

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    uref_clock_set_pts_prog(uref, (uint64_t)UINT32_MAX + 1);
    upipe_input(upipe_trickp_sound, uref, NULL);
    assert(count_pic == 42);
    assert(count_sound == 43);
    assert(count_subpic == 0);
    count_pic = 0;
    count_sound = 0,

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    uref_clock_set_pts_prog(uref, (uint64_t)UINT32_MAX);
    upipe_input(upipe_trickp_subpic, uref, NULL);
    assert(count_pic == 0);
    assert(count_sound == 0);
    assert(count_subpic == 42);
    count_subpic = 0;

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    uref_clock_set_pts_prog(uref, (uint64_t)UINT32_MAX + 2);
    upipe_input(upipe_trickp_pic, uref, NULL);
    assert(count_pic == 44);
    assert(count_sound == 0);
    assert(count_subpic == 0);
    count_pic = 0;

    upipe_release(upipe_trickp);
    upipe_release(upipe_trickp_pic);
    upipe_release(upipe_trickp_sound);
    upipe_release(upipe_trickp_subpic);
    upipe_mgr_release(upipe_trickp_mgr); // nop

    trickp_test_free(upipe_sink_pic);
    trickp_test_free(upipe_sink_sound);
    trickp_test_free(upipe_sink_subpic);

    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);

    uprobe_log_free(log);
    uprobe_stdio_free(uprobe_stdio);
    free(uclock);
    return 0;
}
