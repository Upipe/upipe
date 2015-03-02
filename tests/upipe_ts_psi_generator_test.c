/*
 * Copyright (C) 2013-2015 OpenHeadend S.A.R.L.
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
 * @short unit tests for TS psig module
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/uclock.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_std.h>
#include <upipe/upipe.h>
#include <upipe-ts/uref_ts_flow.h>
#include <upipe-ts/upipe_ts_psi_generator.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>

#include <bitstream/mpeg/psi.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UBUF_POOL_DEPTH 0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

static uint64_t psi_cr = UINT32_MAX;
static bool pat = true;
static uint8_t program = 1;

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
    uint64_t cr;
    ubase_assert(uref_clock_get_cr_sys(uref, &cr));
    assert(cr == psi_cr);
    const uint8_t *buffer;
    int size = -1;
    ubase_assert(uref_block_read(uref, 0, &size, &buffer));
    upipe_dbg_va(upipe, "received table %"PRIu8, psi_get_tableid(buffer));
    assert(psi_get_length(buffer) + PSI_HEADER_SIZE == size);
    assert(psi_validate(buffer));
    assert(psi_check_crc(buffer));
    if (pat) {
        assert(pat_validate(buffer));
        const uint8_t *program = pat_get_program((uint8_t *)buffer, 0);
        assert(program != NULL);
        if (psi_cr != UINT32_MAX + UCLOCK_FREQ * 12) {
            assert(patn_get_program(program) == 1);
            assert(patn_get_pid(program) == 66);
            program = pat_get_program((uint8_t *)buffer, 1);
            assert(program != NULL);
        }
        assert(patn_get_program(program) == 2);
        assert(patn_get_pid(program) == 1500);
        program = pat_get_program((uint8_t *)buffer, 2);
        assert(program == NULL);
        pat = false;
    } else {
        assert(pmt_validate(buffer));
        if (program == 1) {
            assert(pmt_get_pcrpid(buffer) == 67);
            assert(pmt_get_desclength(buffer) == 0);
            const uint8_t *es = pmt_get_es((uint8_t *)buffer, 0);
            assert(es != NULL);
            if (psi_cr != UINT32_MAX + UCLOCK_FREQ * 11) {
                assert(pmtn_get_streamtype(es) == PMT_STREAMTYPE_VIDEO_MPEG2);
                assert(pmtn_get_pid(es) == 67);
                assert(pmtn_get_desclength(es) == 0);
                es = pmt_get_es((uint8_t *)buffer, 1);
                assert(es != NULL);
            }
            assert(pmtn_get_streamtype(es) == PMT_STREAMTYPE_AUDIO_MPEG2);
            assert(pmtn_get_pid(es) == 68);
            assert(pmtn_get_desclength(es) ==
                   DESC0A_HEADER_SIZE + DESC0A_LANGUAGE_SIZE);
            const uint8_t *desc =
                descs_get_desc(pmtn_get_descs((uint8_t *)es), 0);
            assert(desc != NULL);
            assert(desc0a_validate(desc));
            const uint8_t *descn = desc0a_get_language((uint8_t *)desc, 0);
            assert(descn != NULL);
            assert(!strncmp((const char *)desc0an_get_code(descn), "eng", 3));
            assert(desc0an_get_audiotype(descn) == DESC0A_TYPE_CLEAN);
            assert(desc0a_get_language((uint8_t *)desc, 1) == NULL);
            assert(descs_get_desc(pmtn_get_descs((uint8_t *)es), 1) == NULL);
            assert(pmt_get_es((uint8_t *)buffer, 2) == NULL);
            program = 2;
        } else {
            assert(pmt_get_pcrpid(buffer) == 8191);
            assert(pmt_get_desclength(buffer) == 0);
            const uint8_t *es = pmt_get_es((uint8_t *)buffer, 0);
            assert(es != NULL);
            assert(pmtn_get_streamtype(es) == PMT_STREAMTYPE_AUDIO_ADTS);
            assert(pmtn_get_pid(es) == 1501);
            assert(pmtn_get_desclength(es) == 0);
            assert(pmt_get_es((uint8_t *)buffer, 1) == NULL);
            program = 0;
        }
    }
    uref_block_unmap(uref, 0);
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
static struct upipe_mgr ts_test_mgr = {
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
    struct ubuf_mgr *ubuf_mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH,
                                                         UBUF_POOL_DEPTH,
                                                         umem_mgr, -1, 0);
    assert(ubuf_mgr != NULL);
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_LEVEL);
    assert(logger != NULL);
    logger = uprobe_uref_mgr_alloc(logger, uref_mgr);
    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_POOL_DEPTH);
    assert(logger != NULL);

    struct uref *uref;
    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_flow_set_def(uref, "void."));
    ubase_assert(uref_flow_set_id(uref, 42));

    struct upipe_mgr *upipe_ts_psig_mgr = upipe_ts_psig_mgr_alloc();
    assert(upipe_ts_psig_mgr != NULL);
    struct upipe *upipe_ts_psig = upipe_void_alloc(upipe_ts_psig_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "ts psig"));
    assert(upipe_ts_psig != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_psig, uref));
    uref_free(uref);

    ubase_assert(upipe_get_flow_def(upipe_ts_psig, &uref));

    struct upipe *upipe_sink = upipe_void_alloc(&ts_test_mgr,
                                                uprobe_use(logger));
    assert(upipe_sink != NULL);
    ubase_assert(upipe_set_output(upipe_ts_psig, upipe_sink));
    ubase_assert(upipe_ts_mux_set_pat_interval(upipe_ts_psig,
                                               UCLOCK_FREQ * 10));

    /* programs */
    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_flow_set_def(uref, "void."));
    ubase_assert(uref_flow_set_id(uref, 1));
    ubase_assert(uref_ts_flow_set_pid(uref, 66));
    struct upipe *upipe_ts_psig_program1 = upipe_void_alloc_sub(upipe_ts_psig,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                                   "ts psig program1"));
    assert(upipe_ts_psig_program1 != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_psig_program1, uref));
    ubase_assert(upipe_set_output(upipe_ts_psig_program1, upipe_sink));
    ubase_assert(upipe_ts_psig_program_set_pcr_pid(upipe_ts_psig_program1, 67));
    ubase_assert(upipe_ts_mux_set_pmt_interval(upipe_ts_psig_program1,
                                               UCLOCK_FREQ * 10));

    ubase_assert(uref_flow_set_id(uref, 2));
    ubase_assert(uref_ts_flow_set_pid(uref, 1500));
    struct upipe *upipe_ts_psig_program2 = upipe_void_alloc_sub(upipe_ts_psig,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "ts psig program2"));
    assert(upipe_ts_psig_program2 != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_psig_program2, uref));
    ubase_assert(upipe_set_output(upipe_ts_psig_program2, upipe_sink));
    ubase_assert(upipe_ts_mux_set_pmt_interval(upipe_ts_psig_program2,
                                               UCLOCK_FREQ * 10));
    uref_free(uref);

    /* flows */
    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_flow_set_def(uref, "void."));
    ubase_assert(uref_ts_flow_set_pid(uref, 67));
    ubase_assert(uref_ts_flow_set_stream_type(uref, PMT_STREAMTYPE_VIDEO_MPEG2));
    struct upipe *upipe_ts_psig_flow67 =
        upipe_void_alloc_sub(upipe_ts_psig_program1,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "ts psig flow67"));
    assert(upipe_ts_psig_flow67 != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_psig_flow67, uref));
    uref_free(uref);

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_flow_set_def(uref, "void."));
    ubase_assert(uref_ts_flow_set_pid(uref, 68));
    ubase_assert(uref_ts_flow_set_stream_type(uref, PMT_STREAMTYPE_AUDIO_MPEG2));
    uint8_t desc[DESC0A_HEADER_SIZE + DESC0A_LANGUAGE_SIZE];
    desc0a_init(desc);
    desc_set_length(desc, DESC0A_LANGUAGE_SIZE);
    uint8_t *descn = desc0a_get_language(desc, 0);
    desc0an_set_code(descn, (const uint8_t *)"eng");
    desc0an_set_audiotype(descn, DESC0A_TYPE_CLEAN);
    assert(desc0a_validate(desc));
    ubase_assert(uref_ts_flow_add_descriptor(uref, desc, sizeof(desc)));
    struct upipe *upipe_ts_psig_flow68 =
        upipe_void_alloc_sub(upipe_ts_psig_program1,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "ts psig flow68"));
    assert(upipe_ts_psig_flow68 != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_psig_flow68, uref));
    uref_free(uref);

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_flow_set_def(uref, "void."));
    ubase_assert(uref_ts_flow_set_pid(uref, 1501));
    ubase_assert(uref_ts_flow_set_stream_type(uref, PMT_STREAMTYPE_AUDIO_ADTS));
    struct upipe *upipe_ts_psig_flow1501 =
        upipe_void_alloc_sub(upipe_ts_psig_program2,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "ts psig flow1501"));
    assert(upipe_ts_psig_flow1501 != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_psig_flow1501, uref));
    uref_free(uref);

    upipe_dbg(upipe_ts_psig, "preparing PAT and 2 PMTs");
    ubase_assert(upipe_ts_psig_prepare(upipe_ts_psig, UINT32_MAX));
    assert(pat == false);
    assert(program == 0);

    pat = true;
    program = 1;
    upipe_dbg(upipe_ts_psig, "preparing nothing");
    ubase_assert(upipe_ts_psig_prepare(upipe_ts_psig,
                                       UINT32_MAX + UCLOCK_FREQ));
    assert(pat == true);
    assert(program == 1);

    psi_cr = UINT32_MAX + 10 * UCLOCK_FREQ;
    upipe_dbg(upipe_ts_psig, "preparing PAT and 2 PMTs");
    ubase_assert(upipe_ts_psig_prepare(upipe_ts_psig,
                                       UINT32_MAX + 10 * UCLOCK_FREQ));
    assert(pat == false);
    assert(program == 0);

    upipe_release(upipe_ts_psig_flow67);
    program = 1;
    psi_cr = UINT32_MAX + 11 * UCLOCK_FREQ;
    upipe_dbg(upipe_ts_psig, "preparing 1 PMT");
    ubase_assert(upipe_ts_psig_prepare(upipe_ts_psig,
                                       UINT32_MAX + 11 * UCLOCK_FREQ));
    assert(pat == false);
    assert(program == 2);

    upipe_release(upipe_ts_psig_flow68);
    upipe_release(upipe_ts_psig_program1);
    pat = true;
    program = 0;
    psi_cr = UINT32_MAX + 12 * UCLOCK_FREQ;
    upipe_dbg(upipe_ts_psig, "preparing PAT");
    ubase_assert(upipe_ts_psig_prepare(upipe_ts_psig,
                                       UINT32_MAX + 12 * UCLOCK_FREQ));
    assert(pat == false);
    assert(program == 0);

    upipe_release(upipe_ts_psig_flow1501);

    upipe_release(upipe_ts_psig_program2);

    upipe_release(upipe_ts_psig);
    upipe_mgr_release(upipe_ts_psig_mgr); // nop

    test_free(upipe_sink);

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe);

    return 0;
}
