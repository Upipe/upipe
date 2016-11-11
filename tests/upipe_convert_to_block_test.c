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
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_pic_mem.h>
#include <upipe/ubuf_sound_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_std.h>
#include <upipe/uref_dump.h>
#include <upipe-modules/upipe_convert_to_block.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#define UDICT_POOL_DEPTH    0
#define UREF_POOL_DEPTH     0
#define UBUF_POOL_DEPTH     0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

static struct uref *output = NULL;

static void pic_fill_in(struct ubuf *ubuf)
{
    size_t hsize, vsize;
    uint8_t macropixel;
    ubase_assert(ubuf_pic_size(ubuf, &hsize, &vsize, &macropixel));

    const char *chroma = NULL;
    while (ubase_check(ubuf_pic_plane_iterate(ubuf, &chroma)) &&
           chroma != NULL) {
        size_t stride;
        uint8_t hsub, vsub, macropixel_size;
        ubase_assert(ubuf_pic_plane_size(ubuf, chroma, &stride, &hsub, &vsub,
                                         &macropixel_size));
        int hoctets = hsize * macropixel_size / hsub / macropixel;
        uint8_t *buffer;
        ubase_assert(ubuf_pic_plane_write(ubuf, chroma, 0, 0, -1, -1, &buffer));

        for (int y = 0; y < vsize / vsub; y++) {
            for (int x = 0; x < hoctets; x++)
                buffer[x] = 1 + (y * hoctets) + x;
            buffer += stride;
        }
        ubase_assert(ubuf_pic_plane_unmap(ubuf, chroma, 0, 0, -1, -1));
    }
}

static void sound_fill_in(struct ubuf *ubuf)
{
    size_t size;
    uint8_t sample_size;
    ubase_assert(ubuf_sound_size(ubuf, &size, &sample_size));
    int octets = size * sample_size;

    const char *channel = NULL;
    while (ubase_check(ubuf_sound_plane_iterate(ubuf, &channel)) &&
           channel != NULL) {
        uint8_t *buffer;
        ubase_assert(ubuf_sound_plane_write_uint8_t(ubuf, channel, 0, -1,
                                                    &buffer));

        for (int x = 0; x < octets; x++)
            buffer[x] = (uint8_t)channel[0] + x;
        ubase_assert(ubuf_sound_plane_unmap(ubuf, channel, 0, -1));
    }
}

/** definition of our uprobe */
static int catch(struct uprobe *uprobe, struct upipe *upipe, int event, va_list args)
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
    upipe_dbg(upipe, "===> received input uref");
    uref_dump(uref, upipe->uprobe);
    assert(output == NULL);
    output = uref;
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
    upipe_dbg_va(upipe, "releasing pipe %p", upipe);
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe */
static struct upipe_mgr tblk_test_mgr = {
    .refcount = NULL,
    .signature = 0,

    .upipe_alloc = test_alloc,
    .upipe_input = test_input,
    .upipe_control = test_control
};

int main(int argc, char **argv)
{
    printf("Compiled %s %s - %s\n", __DATE__, __TIME__, __FILE__);

    struct uref *uref;

    /* uref and mem management */
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH, umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0); 
    assert(uref_mgr != NULL);

    /* uprobe stuff */
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_DEBUG);
    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_POOL_DEPTH);
    assert(logger != NULL);

    /* build tblk pipe */
    struct upipe_mgr *upipe_tblk_mgr = upipe_tblk_mgr_alloc();
    struct upipe *tblk = upipe_void_alloc(upipe_tblk_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "tblk"));
    assert(upipe_tblk_mgr);

    struct upipe *tblk_test = upipe_void_alloc(&tblk_test_mgr,
                                               uprobe_use(logger));
    assert(tblk_test != NULL);
    ubase_assert(upipe_set_output(tblk, tblk_test));

    /* pic */
    struct ubuf_mgr *pic_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH,
            UBUF_POOL_DEPTH, umem_mgr, 1, 0, 0, 0, 0, 0, 0);
    assert(pic_mgr);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(pic_mgr, "y8u8v8a8", 1, 1, 4));

    /* set up flow definition packet */
    uref = uref_pic_flow_alloc_def(uref_mgr, 1);
    assert(uref);
    ubase_assert(upipe_set_flow_def(tblk, uref));
    assert(tblk);
    uref_free(uref);
    ubase_assert(upipe_get_flow_def(tblk, &uref));
    const char *def;
    ubase_assert(uref_flow_get_def(uref, &def));
    assert(!strcmp(def, "block."));

    uref = uref_pic_alloc(uref_mgr, pic_mgr, 32, 32);
    assert(uref);
    pic_fill_in(uref->ubuf);

    /* Now send uref */
    upipe_input(tblk, uref, NULL);
    assert(output != NULL);
    size_t size;
    ubase_assert(uref_block_size(output, &size));
    assert(size == 4096);
    const uint8_t *r;
    int size2 = -1;
    ubase_assert(uref_block_read(output, 0, &size2, &r));
    assert(size2 == 4096);
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32 * 4; x++)
            assert(r[x + 32 * 4 * y] == (uint8_t)(1 + (y * 32 * 4) + x));
    uref_block_unmap(output, 0);
    uref_free(output);
    output = NULL;

    /* sound */
    struct ubuf_mgr *sound_mgr = ubuf_sound_mem_mgr_alloc(UBUF_POOL_DEPTH,
            UBUF_POOL_DEPTH, umem_mgr, 4, 0);
    assert(sound_mgr);
    ubase_assert(ubuf_sound_mem_mgr_add_plane(sound_mgr, "lr"));

    /* set up flow definition packet */
    uref = uref_sound_flow_alloc_def(uref_mgr, NULL, 2, 4);
    assert(uref);
    ubase_assert(upipe_set_flow_def(tblk, uref));
    assert(tblk);
    uref_free(uref);
    ubase_assert(upipe_get_flow_def(tblk, &uref));
    ubase_assert(uref_flow_get_def(uref, &def));
    assert(!strcmp(def, "block."));

    uref = uref_sound_alloc(uref_mgr, sound_mgr, 1024);
    assert(uref);
    sound_fill_in(uref->ubuf);

    /* Now send uref */
    upipe_input(tblk, uref, NULL);
    assert(output != NULL);
    ubase_assert(uref_block_size(output, &size));
    assert(size == 4096);
    size2 = -1;
    ubase_assert(uref_block_read(output, 0, &size2, &r));
    assert(size2 == 4096);
    for (int i = 0; i < 4096; i++)
        assert(r[i] == (uint8_t)('l' + i));
    uref_block_unmap(output, 0);
    uref_free(output);

    upipe_release(tblk);
    test_free(tblk_test);

    /* release managers */
    ubuf_mgr_release(pic_mgr);
    ubuf_mgr_release(sound_mgr);
    uref_mgr_release(uref_mgr);
    umem_mgr_release(umem_mgr);
    udict_mgr_release(udict_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe);

    return 0;
}
