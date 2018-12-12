/*
 * Copyright (C) 2012-2017 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
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
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_pic.h>
#include <upipe/ubuf_pic_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_std.h>
#include <upipe/upipe.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe-gl/upipe_glx_sink.h>
#include <upipe-gl/uprobe_gl_sink_cube.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#define UPUMP_POOL 1
#define UPUMP_BLOCKER_POOL 1
#define UDICT_POOL_DEPTH    5
#define UREF_POOL_DEPTH     5
#define UBUF_POOL_DEPTH     5
#define UBUF_PREPEND        0
#define UBUF_APPEND         0
#define UBUF_ALIGN          0 // glx_sink needs contigous data (for glTexImage2D)
#define UBUF_ALIGN_HOFFSET  0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

#define SINK_NUM            4
#define WIDTH               720
#define HEIGHT              576
#define LIMIT               120

static struct ubuf_mgr *ubuf_mgr;
static struct uref_mgr *uref_mgr;
static struct upipe *glx_sink[SINK_NUM];
static struct upump *idlerpump;

static void sigint_cb(struct upump *upump)
{
    printf("sigint received, exiting.\n");
    upump_stop(idlerpump);
}

static void keyhandler(struct upipe *upipe, unsigned long key)
{
    switch (key) {
        case 27:
        case 'q': {
            upipe_notice_va(upipe, "exit key pressed (%lu), exiting", key);
            exit(0);
        }
        default:
            upipe_dbg_va(upipe, "key pressed (%lu)", key);
            break;
    }
}

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
        case UPROBE_NEED_UPUMP_MGR:
        case UPROBE_GLX_SINK_KEYRELEASE: // don't care about release
            break;
        case UPROBE_GLX_SINK_KEYPRESS: {
            va_arg(args, unsigned int);
            unsigned long key = va_arg(args, unsigned long);
            keyhandler(upipe, key);
            break;
        }
    }
    return UBASE_ERR_NONE;
}

static void idler_cb(struct upump *upump)
{
    static int counter = 0;
    int x, y, i;
    uint8_t *buf, macropixel = 0;
    size_t stride = 0;
    struct uref *pic;

    printf("(idler) Sending pic %d\n", counter);

    for (i=0; i < SINK_NUM; i++) {
        pic = uref_pic_alloc(uref_mgr, ubuf_mgr, WIDTH, HEIGHT);
        assert(pic);
        uref_pic_plane_write(pic, "r8g8b8", 0, 0, -1, -1, &buf);
        uref_pic_plane_size(pic, "r8g8b8", &stride, NULL, NULL, &macropixel);

        for (y=0; y < HEIGHT; y++) {
            for (x=0; x < WIDTH; x++) {
                buf[macropixel * x] = x + y + counter * 3;
                buf[macropixel * x + 1] = x + y + counter * 3 * 10;
                buf[macropixel * x + 2] = 50 * i + x + y + counter * 3 * 10;
            }
            buf += stride;
        }

        uref_pic_plane_unmap(pic, "r8g8b8", 0, 0, -1, -1);
        upipe_input(glx_sink[i], pic, NULL);
    }

    counter++;
    if (counter > LIMIT) {
        upump_stop(upump);
    }
}

int main(int argc, char **argv)
{
    printf("Compiled %s %s (%s)\n", __DATE__, __TIME__, __FILE__);
    int i;

    /* upipe env */
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_LEVEL);
    assert(logger != NULL);
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH, umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    assert(uref_mgr != NULL);
    /* r8g8b8 */
    ubuf_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                      UBUF_PREPEND, UBUF_APPEND,
                                      UBUF_PREPEND, UBUF_APPEND,
                                      UBUF_ALIGN, UBUF_ALIGN_HOFFSET);
    assert(ubuf_mgr);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(ubuf_mgr, "r8g8b8", 1, 1, 3));

    /* ev loop */
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc_default(UPUMP_POOL,
            UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);

    struct uref *flow_def = uref_pic_flow_alloc_def(uref_mgr, 1);
    assert(flow_def != NULL);
    ubase_assert(uref_pic_flow_add_plane(flow_def, 1, 1, 3, "r8g8b8"));

    /* glx sink */
    struct upipe_mgr *glx_mgr = upipe_glx_sink_mgr_alloc();
    for (i=0; i < SINK_NUM; i++) {
        glx_sink[i] = upipe_void_alloc(glx_mgr, uprobe_gl_sink_cube_alloc(
                uprobe_pfx_alloc_va(uprobe_use(logger), UPROBE_LOG_LEVEL,
                                    "glx %d", i)));
        assert(glx_sink[i]);
        ubase_assert(upipe_set_flow_def(glx_sink[i], flow_def));
        upipe_glx_sink_init(glx_sink[i], 0, 0, 640, 480);
    }
    uref_free(flow_def);

    /* idler */
    idlerpump = upump_alloc_idler(upump_mgr, idler_cb, NULL, NULL);
    upump_start(idlerpump);

    /* sigint handler */
    struct upump *sigint_pump = upump_alloc_signal(upump_mgr, sigint_cb, NULL,
                                                   NULL, SIGINT);
    upump_set_status(sigint_pump, false);
    upump_start(sigint_pump);

    // Fire
    upump_mgr_run(upump_mgr, NULL);

    // Clean - release
    for (i=0; i < SINK_NUM; i++) {
        upipe_release(glx_sink[i]);
    }
    upump_free(idlerpump);
    upump_stop(sigint_pump);
    upump_free(sigint_pump);
    upump_mgr_release(upump_mgr);
    upipe_mgr_release(glx_mgr); // noop
    ubuf_mgr_release(ubuf_mgr);
    uref_mgr_release(uref_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    return 0;
}
