/*
 * Copyright (C) 2017 Open Broadcast Systems Ltd
 *
 * Authors: Judah Rand
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
#include <upipe/ubuf_block_mem.h>
#include <upipe/ubuf_pic_mem.h>
#include <upipe/ubuf_sound_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_std.h>
#include <upipe/uref_dump.h>
#include <upipe-modules/upipe_block_to_sound.h>

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

static void block_fill_in(struct ubuf *ubuf)
{
    size_t size;
    ubase_assert(ubuf_block_size(ubuf, &size));

    int block_size = -1;
    uint8_t *buffer;
    ubase_assert(ubuf_block_write(ubuf, 0, &block_size, &buffer));

    for (int x = 0; x < size; x++)
        buffer[x] = x;

    ubase_assert(ubuf_block_unmap(ubuf, 0));
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
static struct upipe_mgr block_to_sound_test_mgr = {
    .refcount = NULL,
    .signature = 0,

    .upipe_alloc = test_alloc,
    .upipe_input = test_input,
    .upipe_control = test_control
};

int main(int argc, char **argv)
{
    struct uref *uref;
    int block_size = 256;

    uint8_t sample_size = 8;
    uint8_t channels = 2;


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

    /* set up sound flow definition config packet */
    uref = uref_sound_flow_alloc_def(uref_mgr, "s32.", channels, sample_size);

    uref_sound_flow_set_planes(uref, 0);
    uref_sound_flow_add_plane(uref, "lr");
    uref_sound_flow_set_raw_sample_size(uref, 20);
    assert(uref);

    /* set up sound block flow definition packet */
    struct uref *flow_def = uref_block_flow_alloc_def(uref_mgr, "");
    assert(flow_def);

    /* build block_to_sound pipe */
    struct upipe_mgr *upipe_block_to_sound_mgr = upipe_block_to_sound_mgr_alloc();
    assert(upipe_block_to_sound_mgr);

    struct upipe *upipe_block_to_sound = upipe_flow_alloc(upipe_block_to_sound_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "block_to_sound"), uref);
    assert(upipe_block_to_sound);
    uref_free(uref);

    struct upipe *block_to_sound_test = upipe_void_alloc(&block_to_sound_test_mgr,
                                               uprobe_use(logger));
    assert(block_to_sound_test != NULL);
    ubase_assert(upipe_set_output(upipe_block_to_sound, block_to_sound_test));

    /* send flow def to block_to_sound */
    ubase_assert(upipe_set_flow_def(upipe_block_to_sound, flow_def));
    uref_free(flow_def);
    const char *def;
    ubase_assert(upipe_get_flow_def(upipe_block_to_sound, &uref));
    ubase_assert(uref_flow_get_def(uref, &def));
    assert(!ubase_ncmp(def, "sound."));

    /* block */
    struct ubuf_mgr *block_mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH,
            UBUF_POOL_DEPTH, umem_mgr, 0, 0, 0, 0);
    assert(block_mgr);

    uref = uref_block_alloc(uref_mgr, block_mgr, block_size);
    assert(uref);
    block_fill_in(uref->ubuf);

    /* Now send uref */
    upipe_input(upipe_block_to_sound, uref, NULL);
    assert(output != NULL);
    int no_samples = block_size/ sample_size;
    size_t size;
    ubase_assert(ubuf_sound_size(output->ubuf, &size, &sample_size));
    assert(size == no_samples);
    assert(sample_size == 8);

    const int32_t *r;
    int size2 = -1;
    ubase_assert(uref_sound_plane_read_int32_t(output, "lr", 0, size2, &r));

    block_size = no_samples * sample_size;

    for (int x = 0 ; x < no_samples; x++) {
        int32_t s = (4*x) | ((4*x+1) << 8) | ((4*x+2) << 16) | ((4*x+3) << 24);
        assert(s == r[x]);
    }
    uref_sound_plane_unmap(output, "lr", 0, -1);
    uref_free(output);

    upipe_release(upipe_block_to_sound);
    test_free(block_to_sound_test);

    /* release managers */
    ubuf_mgr_release(block_mgr);
    uref_mgr_release(uref_mgr);
    umem_mgr_release(umem_mgr);
    udict_mgr_release(udict_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe);

    return 0;
}
