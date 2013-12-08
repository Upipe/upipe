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

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uclock.h>
#include <upipe/uclock_std.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe/upipe.h>
#include <upipe-modules/upipe_sine_wave_source.h>
#include <upipe-alsa/upipe_alsa_sink.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#define UDICT_POOL_DEPTH    5
#define UREF_POOL_DEPTH     5
#define UBUF_POOL_DEPTH     5
#define UPUMP_POOL          5
#define UPUMP_BLOCKER_POOL  5
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

/** definition of our uprobe */
static bool catch(struct uprobe *uprobe, struct upipe *upipe, enum uprobe_event event, va_list args)
{
    switch (event) {
        case UPROBE_READY:
        case UPROBE_DEAD:
            break;
        default:
            assert(event & UPROBE_HANDLED_FLAG);
            break;
    }
    return true;
}

int main(int argc, char **argv)
{
    printf("Compiled %s %s - %s\n", __DATE__, __TIME__, __FILE__);

    /* uclock stuff */
    struct uclock *uclock = uclock_std_alloc(0);
    assert(uclock != NULL);

    /* upump management */
    struct ev_loop *loop = ev_default_loop(0);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop, UPUMP_POOL,
                                                     UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);

    /* uref and mem management */
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH, umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0); 
    assert(uref_mgr != NULL);
    struct ubuf_mgr *ubuf_mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH,
                                                         UBUF_POOL_DEPTH,
                                                         umem_mgr, -1, 0);
    assert(ubuf_mgr != NULL);

    /* uprobe stuff */
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_DEBUG);
    assert(uprobe_stdio != NULL);

    /* build sine wave source */
    struct upipe_mgr *upipe_sinesrc_mgr = upipe_sinesrc_mgr_alloc();
    assert(upipe_sinesrc_mgr != NULL);
    struct upipe *sinesrc = upipe_void_alloc(upipe_sinesrc_mgr,
               uprobe_pfx_adhoc_alloc(uprobe_stdio, UPROBE_LOG_LEVEL, "sinesrc"));
    assert(sinesrc != NULL);
    assert(upipe_set_uref_mgr(sinesrc, uref_mgr));
    assert(upipe_set_ubuf_mgr(sinesrc, ubuf_mgr));
    assert(upipe_set_uclock(sinesrc, uclock));
    assert(upipe_set_upump_mgr(sinesrc, upump_mgr));

    /* build alsink pipe */
    struct upipe_mgr *upipe_alsink_mgr = upipe_alsink_mgr_alloc();
    assert(upipe_alsink_mgr != NULL);
    struct upipe *alsink = upipe_void_alloc_output(sinesrc, upipe_alsink_mgr,
               uprobe_pfx_adhoc_alloc(uprobe_stdio, UPROBE_LOG_LEVEL, "alsink"));
    assert(alsink != NULL);
    assert(upipe_set_uclock(alsink, uclock));
    assert(upipe_set_upump_mgr(alsink, upump_mgr));
    assert(upipe_set_uri(alsink, "default"));

    ev_loop(loop, 0);

    /* release pipe */
    upipe_release(alsink);

    /* release managers */
    upipe_mgr_release(upipe_alsink_mgr); // no-op
    upump_mgr_release(upump_mgr);
    uclock_release(uclock);
    ubuf_mgr_release(ubuf_mgr);
    uref_mgr_release(uref_mgr);
    umem_mgr_release(umem_mgr);
    udict_mgr_release(udict_mgr);
    uprobe_stdio_free(uprobe_stdio);

    ev_default_destroy();
    return 0;
}
