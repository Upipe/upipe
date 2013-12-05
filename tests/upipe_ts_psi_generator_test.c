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
 * @short unit tests for TS psig module
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_log.h>
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
#include <upipe-ts/uprobe_ts_log.h>
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

static bool pat = true;
static uint8_t program = 0;

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
            break;
    }
    return true;
}

/** helper phony pipe to test upipe_ts_psig */
static struct upipe *ts_test_alloc(struct upipe_mgr *mgr,
                                   struct uprobe *uprobe, uint32_t signature,
                                   va_list args)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_init(upipe, mgr, uprobe);
    return upipe;
}

/** helper phony pipe to test upipe_ts_psig */
static void ts_test_input(struct upipe *upipe, struct uref *uref,
                          struct upump *upump)
{
    assert(uref != NULL);
    uint64_t cr;
    assert(uref_clock_get_cr_sys(uref, &cr));
    const uint8_t *buffer;
    int size = -1;
    assert(uref_block_read(uref, 0, &size, &buffer));
    assert(psi_get_length(buffer) + PSI_HEADER_SIZE == size);
    assert(psi_validate(buffer));
    assert(psi_check_crc(buffer));
    if (pat) {
        assert(cr == UCLOCK_FREQ);
        assert(pat_validate(buffer));
        const uint8_t *program = pat_get_program((uint8_t *)buffer, 0);
        assert(program != NULL);
        assert(patn_get_program(program) == 1);
        assert(patn_get_pid(program) == 66);
        program = pat_get_program((uint8_t *)buffer, 1);
        assert(program != NULL);
        assert(patn_get_program(program) == 2);
        assert(patn_get_pid(program) == 1500);
        program = pat_get_program((uint8_t *)buffer, 2);
        assert(program == NULL);
        pat = false;
    } else {
        assert(pmt_validate(buffer));
        if (program == 1) {
            assert(cr == UCLOCK_FREQ * 2);
            assert(pmt_get_pcrpid(buffer) == 67);
            assert(pmt_get_desclength(buffer) == 0);
            const uint8_t *es = pmt_get_es((uint8_t *)buffer, 0);
            assert(es != NULL);
            assert(pmtn_get_streamtype(es) == PMT_STREAMTYPE_VIDEO_MPEG2);
            assert(pmtn_get_pid(es) == 67);
            assert(pmtn_get_desclength(es) == 0);
            es = pmt_get_es((uint8_t *)buffer, 1);
            assert(es != NULL);
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
        } else {
            assert(cr == UCLOCK_FREQ * 3);
            assert(pmt_get_pcrpid(buffer) == 8191);
            assert(pmt_get_desclength(buffer) == 0);
            const uint8_t *es = pmt_get_es((uint8_t *)buffer, 0);
            assert(es != NULL);
            assert(pmtn_get_streamtype(es) == PMT_STREAMTYPE_AUDIO_ADTS);
            assert(pmtn_get_pid(es) == 1501);
            assert(pmtn_get_desclength(es) == 0);
            assert(pmt_get_es((uint8_t *)buffer, 1) == NULL);
        }
        program = 0;
    }
    uref_block_unmap(uref, 0);
    uref_free(uref);
}

/** helper phony pipe to test upipe_ts_psig */
static void ts_test_free(struct upipe *upipe)
{
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe to test upipe_ts_psig */
static struct upipe_mgr ts_test_mgr = {
    .upipe_alloc = ts_test_alloc,
    .upipe_input = ts_test_input,
    .upipe_control = NULL,
    .upipe_free = NULL,

    .upipe_mgr_free = NULL
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
                                                         umem_mgr, -1, -1,
                                                         -1, 0);
    assert(ubuf_mgr != NULL);
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_LEVEL);
    assert(uprobe_stdio != NULL);
    struct uprobe *log = uprobe_log_alloc(uprobe_stdio, UPROBE_LOG_LEVEL);
    assert(log != NULL);
    struct uprobe *uprobe_ts_log = uprobe_ts_log_alloc(log, UPROBE_LOG_DEBUG);
    assert(uprobe_ts_log != NULL);

    struct uref *uref;
    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    assert(uref_flow_set_def(uref, "void."));
    assert(uref_flow_set_id(uref, 42));

    struct upipe_mgr *upipe_ts_psig_mgr = upipe_ts_psig_mgr_alloc();
    assert(upipe_ts_psig_mgr != NULL);
    struct upipe *upipe_ts_psig = upipe_void_alloc(upipe_ts_psig_mgr,
            uprobe_pfx_adhoc_alloc(uprobe_ts_log, UPROBE_LOG_LEVEL, "ts psig"));
    assert(upipe_ts_psig != NULL);
    assert(upipe_set_flow_def(upipe_ts_psig, uref));
    uref_free(uref);
    assert(upipe_set_ubuf_mgr(upipe_ts_psig, ubuf_mgr));

    assert(upipe_get_flow_def(upipe_ts_psig, &uref));

    struct upipe *upipe_sink = upipe_void_alloc(&ts_test_mgr, log);
    assert(upipe_sink != NULL);
    assert(upipe_set_output(upipe_ts_psig, upipe_sink));

    /* programs */
    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    assert(uref_flow_set_def(uref, "void."));
    assert(uref_flow_set_id(uref, 1));
    assert(uref_ts_flow_set_pid(uref, 66));
    struct upipe *upipe_ts_psig_program1 = upipe_void_alloc_sub(upipe_ts_psig,
            uprobe_pfx_adhoc_alloc(uprobe_ts_log, UPROBE_LOG_LEVEL,
                                   "ts psig program1"));
    assert(upipe_ts_psig_program1 != NULL);
    assert(upipe_set_flow_def(upipe_ts_psig_program1, uref));
    assert(upipe_set_output(upipe_ts_psig_program1, upipe_sink));
    assert(upipe_ts_psig_program_set_pcr_pid(upipe_ts_psig_program1, 67));

    assert(uref_flow_set_id(uref, 2));
    assert(uref_ts_flow_set_pid(uref, 1500));
    struct upipe *upipe_ts_psig_program2 = upipe_void_alloc_sub(upipe_ts_psig,
            uprobe_pfx_adhoc_alloc(uprobe_ts_log, UPROBE_LOG_LEVEL,
                                   "ts psig program2"));
    assert(upipe_ts_psig_program2 != NULL);
    assert(upipe_set_flow_def(upipe_ts_psig_program2, uref));
    assert(upipe_set_output(upipe_ts_psig_program2, upipe_sink));
    uref_free(uref);

    /* flows */
    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    assert(uref_flow_set_def(uref, "void."));
    assert(uref_ts_flow_set_pid(uref, 67));
    assert(uref_ts_flow_set_stream_type(uref, PMT_STREAMTYPE_VIDEO_MPEG2));
    struct upipe *upipe_ts_psig_flow67 =
        upipe_void_alloc_sub(upipe_ts_psig_program1,
            uprobe_pfx_adhoc_alloc(uprobe_ts_log, UPROBE_LOG_LEVEL,
                                   "ts psig flow67"));
    assert(upipe_ts_psig_flow67 != NULL);
    assert(upipe_set_flow_def(upipe_ts_psig_flow67, uref));
    uref_free(uref);

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    assert(uref_flow_set_def(uref, "void."));
    assert(uref_ts_flow_set_pid(uref, 68));
    assert(uref_ts_flow_set_stream_type(uref, PMT_STREAMTYPE_AUDIO_MPEG2));
    uint8_t desc[DESC0A_HEADER_SIZE + DESC0A_LANGUAGE_SIZE];
    desc0a_init(desc);
    desc_set_length(desc, DESC0A_LANGUAGE_SIZE);
    uint8_t *descn = desc0a_get_language(desc, 0);
    desc0an_set_code(descn, (const uint8_t *)"eng");
    desc0an_set_audiotype(descn, DESC0A_TYPE_CLEAN);
    assert(desc0a_validate(desc));
    assert(uref_ts_flow_set_descriptors(uref, desc, sizeof(desc)));
    struct upipe *upipe_ts_psig_flow68 =
        upipe_void_alloc_sub(upipe_ts_psig_program1,
            uprobe_pfx_adhoc_alloc(uprobe_ts_log, UPROBE_LOG_LEVEL,
                                   "ts psig flow68"));
    assert(upipe_ts_psig_flow68 != NULL);
    assert(upipe_set_flow_def(upipe_ts_psig_flow68, uref));
    uref_free(uref);

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    assert(uref_flow_set_def(uref, "void."));
    assert(uref_ts_flow_set_pid(uref, 1501));
    assert(uref_ts_flow_set_stream_type(uref, PMT_STREAMTYPE_AUDIO_ADTS));
    struct upipe *upipe_ts_psig_flow1501 =
        upipe_void_alloc_sub(upipe_ts_psig_program2,
            uprobe_pfx_adhoc_alloc(uprobe_ts_log, UPROBE_LOG_LEVEL,
                                   "ts psig flow1501"));
    assert(upipe_ts_psig_flow1501 != NULL);
    assert(upipe_set_flow_def(upipe_ts_psig_flow1501, uref));
    uref_free(uref);

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    uref_clock_set_cr_sys(uref, UCLOCK_FREQ);
    uref_clock_set_cr_prog(uref, UCLOCK_FREQ);
    upipe_input(upipe_ts_psig, uref, NULL);
    assert(pat == false);

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    uref_clock_set_cr_sys(uref, UCLOCK_FREQ * 2);
    uref_clock_set_cr_orig(uref, UCLOCK_FREQ * 2);
    program = 1;
    upipe_input(upipe_ts_psig_program1, uref, NULL);
    assert(program == 0);

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    uref_clock_set_cr_sys(uref, UCLOCK_FREQ * 3);
    uref_clock_set_cr_orig(uref, UCLOCK_FREQ * 3);
    program = 2;
    upipe_input(upipe_ts_psig_program2, uref, NULL);
    assert(program == 0);

    upipe_release(upipe_ts_psig_flow67);
    upipe_release(upipe_ts_psig_flow68);
    upipe_release(upipe_ts_psig_flow1501);

    upipe_release(upipe_ts_psig_program1);
    upipe_release(upipe_ts_psig_program2);

    upipe_release(upipe_ts_psig);
    upipe_mgr_release(upipe_ts_psig_mgr); // nop

    ts_test_free(upipe_sink);

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_log_free(log);
    uprobe_ts_log_free(uprobe_ts_log);
    uprobe_stdio_free(uprobe_stdio);

    return 0;
}
