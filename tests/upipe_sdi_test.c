/*
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
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
 * @short unit tests for SDI encoding and decoding modules
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_pic.h>
#include <upipe/ubuf_pic_mem.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_block.h>
#include <upipe/uref_std.h>
#include <upipe/upipe.h>
#include <upipe-hbrmt/upipe_sdi_enc.h>
#include <upipe-hbrmt/upipe_sdi_dec.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UBUF_POOL_DEPTH 0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

#define UBUF_ALIGN 32 /* 256-bits simd */

static bool received_block = false;

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
        case UPROBE_NEW_FLOW_DEF:
            break;
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
    size_t s;
    ubase_assert(uref_block_size(uref, &s));
    upipe_dbg_va(upipe, "frame size %zu", s);
    received_block = true;
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
static struct upipe_mgr test_mgr = {
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

    struct ubuf_mgr *pic_mgr[3]; /* sdienc accepts 3 different formats */

    /* yuv 8 */
    pic_mgr[0] = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
            0, 0, 0, 0, 32, 0);
    assert(pic_mgr[0] != NULL);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(pic_mgr[0], "y8", 1, 1, 1));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(pic_mgr[0], "u8", 2, 1, 1));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(pic_mgr[0], "v8", 2, 1, 1));

    /* yuv 10 */
    pic_mgr[1] = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
            0, 0, 0, 0, 32, 0);
    assert(pic_mgr[1] != NULL);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(pic_mgr[1], "y10l", 1, 1, 2));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(pic_mgr[1], "u10l", 2, 1, 2));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(pic_mgr[1], "v10l", 2, 1, 2));


    /* v210 */
    pic_mgr[2] = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 6,
            0, 0, 0, 0, 32, 0);
    assert(pic_mgr[2] != NULL);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(pic_mgr[2],
                "u10y10v10y10u10y10v10y10u10y10v10y10", 1, 1, 16));

    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_LEVEL);
    assert(uprobe_stdio != NULL);

    uprobe_stdio = uprobe_ubuf_mem_alloc(uprobe_stdio, umem_mgr,
            UBUF_POOL_DEPTH, UBUF_POOL_DEPTH);
    assert(uprobe_stdio != NULL);

    struct upipe_mgr *upipe_sdi_enc_mgr = upipe_sdi_enc_mgr_alloc();
    assert(upipe_sdi_enc_mgr != NULL);
    struct upipe *upipe_sdienc = upipe_sdi_enc_alloc(upipe_sdi_enc_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "sdienc"),
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "sdienc_ttx"));
    assert(upipe_sdienc != NULL);

    /* TODO: actually send audio */
    struct upipe *audio = upipe_void_alloc_sub(upipe_sdienc,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "sdienc audio"));

    struct upipe *sink = upipe_void_alloc(&test_mgr, uprobe_use(uprobe_stdio));
    assert(sink != NULL);
    ubase_assert(upipe_set_output(upipe_sdienc, sink));

    /* */

    static const struct {
        uint64_t w;
        uint64_t h;
        struct urational fps;
    } fmts[] = {
//        { 1920, 1080, { 24, 1 } },
        { 1920, 1080, { 25, 1 } },
        { 1920, 1080, { 50, 1 } },
        { 1920, 1080, { 30000, 1001 } },
        { 1920, 1080, { 60000, 1001 } },
//        { 1920, 1080, { 24000, 1001 } },
        { 1280, 720, { 50, 1 } },

        { 1280, 720, { 60000, 1001 } },
        { 720, 576, { 25, 1 } },
        { 720, 486, { 30000, 1001 } },
    };

    for (int p = 0; p < 3; p++) {
        for (size_t i = 0; i < sizeof(fmts)/sizeof(*fmts); i++) {
            uint64_t w = fmts[i].w;
            const uint64_t h = fmts[i].h;
            struct urational fps = fmts[i].fps;

            struct uref *uref;

            uref = uref_pic_flow_alloc_def(uref_mgr, (p == 2) ? 6 : 1);
            assert(uref != NULL);
            ubase_assert(uref_pic_flow_set_hsize(uref, w));
            ubase_assert(uref_pic_flow_set_vsize(uref, h));
            uref_pic_flow_set_fps(uref, fps);
            switch (p) {
            case 0:
                ubase_assert(uref_pic_flow_add_plane(uref, 1, 1, 1, "y8"));
                ubase_assert(uref_pic_flow_add_plane(uref, 2, 1, 1, "u8"));
                ubase_assert(uref_pic_flow_add_plane(uref, 2, 1, 1, "v8"));
            break;
            case 1:
                ubase_assert(uref_pic_flow_add_plane(uref, 1, 1, 2, "y10l"));
                ubase_assert(uref_pic_flow_add_plane(uref, 2, 1, 2, "u10l"));
                ubase_assert(uref_pic_flow_add_plane(uref, 2, 1, 2, "v10l"));
            break;
            case 2:
                ubase_assert(uref_pic_flow_add_plane(uref, 1, 1, 16,
                    "u10y10v10y10u10y10v10y10u10y10v10y10"));
                if (w % 6) /* 1280 */
                    w += 6 - (w % 6);
            break;
            }

            ubase_assert(upipe_set_flow_def(upipe_sdienc, uref));
            uref_free(uref);


            uref = uref_pic_alloc(uref_mgr, pic_mgr[p], w, h);
            assert(uref);
            ubase_assert(ubuf_pic_clear(uref->ubuf, 0, 0, -1, -1, 1));
            assert(uref != NULL);
            uref_clock_set_pts_sys(uref, UINT32_MAX);

            received_block = false;
            upipe_input(upipe_sdienc, uref, NULL);
            assert(received_block);
        }
    }

    /* */

    upipe_release(audio);
    upipe_release(upipe_sdienc);
    upipe_mgr_release(upipe_sdi_enc_mgr); // nop

    test_free(sink);

    uref_mgr_release(uref_mgr);
    for (int i = 0; i < 3; i++)
        ubuf_mgr_release(pic_mgr[i]);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(uprobe_stdio);

    return 0;
}
