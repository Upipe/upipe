/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
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
 * @short unit tests for TS sig module
 */

#undef NDEBUG

#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/uprobe_uclock.h>
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
#include <upipe/uref_event.h>
#include <upipe/uref_block.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_std.h>
#include <upipe/upipe.h>
#include <upipe-ts/uref_ts_flow.h>
#include <upipe-ts/uref_ts_event.h>
#include <upipe-ts/upipe_ts_si_generator.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <iconv.h>
#include <assert.h>

#include <bitstream/mpeg/psi.h>
#include <bitstream/dvb/si.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UBUF_POOL_DEPTH 0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

static bool nit = false;
static bool sdt = false;
static bool eit = false;
static bool tdt = false;

static const char *psz_native_encoding = "UTF-8";
static const char *psz_current_encoding = "";
static iconv_t iconv_handle = (iconv_t)-1;

/** iconv wrapper from biTStream */
static char *iconv_append_null(const char *p_string, size_t i_length)
{
    char *psz_string = malloc(i_length + 1);
    memcpy(psz_string, p_string, i_length);
    psz_string[i_length] = '\0';
    return psz_string;
}

static char *iconv_wrapper(void *_unused, const char *psz_encoding,
                           char *p_string, size_t i_length)
{
    char *psz_string, *p;
    size_t i_out_length;

    if (!strcmp(psz_encoding, psz_native_encoding))
        return iconv_append_null(p_string, i_length);

    if (iconv_handle != (iconv_t)-1 &&
        strcmp(psz_encoding, psz_current_encoding)) {
        iconv_close(iconv_handle);
        iconv_handle = (iconv_t)-1;
    }

    if (iconv_handle == (iconv_t)-1)
        iconv_handle = iconv_open(psz_native_encoding, psz_encoding);
    if (iconv_handle == (iconv_t)-1) {
        fprintf(stderr, "couldn't convert from %s to %s (%m)\n", psz_encoding,
                psz_native_encoding);
        return iconv_append_null(p_string, i_length);
    }
    psz_current_encoding = psz_encoding;

    /* converted strings can be up to six times larger */
    i_out_length = i_length * 6;
    p = psz_string = malloc(i_out_length);
    if (iconv(iconv_handle, &p_string, &i_length, &p, &i_out_length) == -1) {
        fprintf(stderr, "couldn't convert from %s to %s (%m)\n", psz_encoding,
                psz_native_encoding);
        free(psz_string);
        return iconv_append_null(p_string, i_length);
    }
    if (i_length)
        fprintf(stderr, "partial conversion from %s to %s\n", psz_encoding,
                psz_native_encoding);

    *p = '\0';
    return psz_string;
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
        case UPROBE_NEW_FLOW_DEF:
        case UPROBE_NEED_OUTPUT:
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
    assert(cr == UINT32_MAX);
    const uint8_t *buffer;
    int size = -1;
    ubase_assert(uref_block_read(uref, 0, &size, &buffer));
    upipe_dbg_va(upipe, "received table %"PRIu8, psi_get_tableid(buffer));
    assert(psi_validate(buffer));
    if (!nit || !sdt || !eit) {
        assert(psi_get_length(buffer) + PSI_HEADER_SIZE == size);
        assert(psi_check_crc(buffer));
        assert(psi_get_section(buffer) == 0);
        assert(psi_get_lastsection(buffer) == 0);
    }
    if (!nit) {
        assert(nit_validate(buffer));
        assert(nit_get_nid(buffer) == 43);
        assert(nit_get_desclength(buffer) == DESC40_HEADER_SIZE + 3);

        const uint8_t *desc =
            descs_get_desc(nit_get_descs((uint8_t *)buffer), 0);
        assert(desc != NULL);
        assert(desc40_validate(desc));
        uint8_t networkname_length;
        const uint8_t *networkname =
            desc40_get_networkname(desc, &networkname_length);
        char *networkname_string =
            dvb_string_get(networkname, networkname_length,
                           iconv_wrapper, NULL);
        assert(!strncmp(networkname_string, "ga", networkname_length));
        free(networkname_string);
        const uint8_t *nith = nit_get_header2((uint8_t *)buffer);
        assert(nith_get_tslength(nith) == NIT_TS_SIZE);
        const uint8_t *ts = nit_get_ts((uint8_t *)buffer, 0);
        assert(nitn_get_tsid(ts) == 45);
        assert(nitn_get_onid(ts) == 46);
        assert(nitn_get_desclength(ts) == 0);
        nit = true;

    } else if (!sdt) {
        assert(sdt_validate(buffer));
        assert(sdt_get_tsid(buffer) == 42);
        assert(sdt_get_onid(buffer) == 44);

        const uint8_t *service = sdt_get_service((uint8_t *)buffer, 0);
        assert(service != NULL);
        assert(sdtn_get_sid(service) == 47);
        assert(sdtn_get_eitpresent(service));
        assert(!sdtn_get_eitschedule(service));
        assert(sdtn_get_running(service) == 5);
        assert(!sdtn_get_ca(service));
        assert(sdtn_get_desclength(service) == DESC48_HEADER_SIZE + 8);

        const uint8_t *desc =
            descs_get_desc(sdtn_get_descs((uint8_t *)service), 0);
        uint8_t provider_length, service_length;
        const uint8_t *provider_name =
            desc48_get_provider(desc, &provider_length);
        const uint8_t *service_name =
            desc48_get_service(desc, &service_length);
        char *provider_string =
            dvb_string_get(provider_name, provider_length, iconv_wrapper, NULL);
        char *service_string =
            dvb_string_get(service_name, service_length, iconv_wrapper, NULL);
        assert(!strncmp(service_string, "bu", service_length));
        assert(!strncmp(provider_string, "zo", provider_length));
        free(service_string);
        free(provider_string);
        assert(desc48_get_type(desc) == 1);
        sdt = true;

    } else if (!eit) {
        assert(eit_validate(buffer));
        assert(eit_get_tsid(buffer) == 42);
        assert(eit_get_onid(buffer) == 44);
        assert(eit_get_last_table_id(buffer) == EIT_TABLE_ID_PF_ACTUAL);
        assert(eit_get_segment_last_sec_number(buffer) == 0);

        const uint8_t *event = eit_get_event((uint8_t *)buffer, 0);
        assert(eitn_get_event_id(event) == 1);
        assert(eitn_get_start_time(event) == UINT64_C(0xC079124500));
        assert(eitn_get_duration_bcd(event) == UINT32_C(0x014530));
        assert(eitn_get_running(event) == 5);
        assert(!eitn_get_ca(event));
        assert(eitn_get_desclength(event) == DESC4D_HEADER_SIZE + 12);

        const uint8_t *desc =
            descs_get_desc(eitn_get_descs((uint8_t *)event), 0);
        assert(!strncmp((const char *)desc4d_get_lang(desc), "unk", 3));
        uint8_t event_name_length, text_length;
        const uint8_t *event_name =
            desc4d_get_event_name(desc, &event_name_length);
        const uint8_t *text =
            desc4d_get_text(desc, &text_length);
        char *event_name_str =
            dvb_string_get(event_name, event_name_length, iconv_wrapper, NULL);
        char *text_str =
            dvb_string_get(text, text_length, iconv_wrapper, NULL);
        assert(!strncmp(event_name_str, "meuh", event_name_length));
        assert(!strncmp(text_str, "gaga", text_length));
        free(event_name_str);
        free(text_str);
        eit = true;

    } else if (!tdt) {
        assert(tdt_validate(buffer));
        assert(tdt_get_utc(buffer) == UINT64_C(0xC079124500));
        tdt = true;

    } else
        assert(0);

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

/** helper uclock to test upipe_ts_sig */
static uint64_t test_to_real(struct uclock *uclock, uint64_t cr_sys)
{
    assert(cr_sys == UINT32_MAX);
    struct tm tm;
    tm.tm_year = 93;
    tm.tm_mon = 10 - 1;
    tm.tm_mday = 13;
    tm.tm_hour = 12;
    tm.tm_min = 45;
    tm.tm_sec = 0;
    tm.tm_isdst = 0;
    return mktime(&tm) * UCLOCK_FREQ;
}

int main(int argc, char *argv[])
{
    setenv("TZ", "UTC", 1);
    tzset();

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

    struct uclock uclock;
    uclock.refcount = NULL;
    uclock.uclock_now = NULL;
    uclock.uclock_to_real = test_to_real;
    logger = uprobe_uclock_alloc(logger, &uclock);
    assert(logger != NULL);

    /* super pipe */
    struct uref *uref;
    uref = uref_alloc_control(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_flow_set_def(uref, "void."));
    ubase_assert(uref_flow_set_id(uref, 42));
    ubase_assert(uref_ts_flow_set_nid(uref, 43));
    ubase_assert(uref_ts_flow_set_network_name(uref, "ga"));
    ubase_assert(uref_ts_flow_set_onid(uref, 44));
    ubase_assert(uref_ts_flow_set_nit_ts(uref, 1));
    ubase_assert(uref_ts_flow_set_nit_ts_tsid(uref, 45, 0));
    ubase_assert(uref_ts_flow_set_nit_ts_onid(uref, 46, 0));

    struct upipe_mgr *upipe_ts_sig_mgr = upipe_ts_sig_mgr_alloc();
    assert(upipe_ts_sig_mgr != NULL);
    struct upipe *upipe_ts_sig = upipe_ts_sig_alloc(upipe_ts_sig_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "ts sig"),
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "ts sig nit"),
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "ts sig sdt"),
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "ts sig eit"),
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "ts sig tdt"));
    assert(upipe_ts_sig != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_sig, uref));
    ubase_assert(upipe_ts_mux_set_nit_interval(upipe_ts_sig, UCLOCK_FREQ));
    ubase_assert(upipe_ts_mux_set_sdt_interval(upipe_ts_sig, UCLOCK_FREQ));
    ubase_assert(upipe_ts_mux_set_tdt_interval(upipe_ts_sig, UCLOCK_FREQ));
    uref_free(uref);

    /* services */
    uref = uref_alloc_control(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_flow_set_def(uref, "void."));
    ubase_assert(uref_flow_set_id(uref, 47));
    ubase_assert(uref_ts_flow_set_pid(uref, 48));
    ubase_assert(uref_ts_flow_set_service_type(uref, 1));
    ubase_assert(uref_ts_flow_set_eit(uref));
    ubase_assert(uref_ts_flow_set_running_status(uref, 5));
    ubase_assert(uref_flow_set_name(uref, "bu"));
    ubase_assert(uref_ts_flow_set_provider_name(uref, "zo"));
    ubase_assert(uref_ts_flow_set_service_type(uref, 1));
    ubase_assert(uref_event_set_events(uref, 1));
    ubase_assert(uref_event_set_id(uref, 1, 0));
    struct tm tm;
    tm.tm_year = 93;
    tm.tm_mon = 10 - 1;
    tm.tm_mday = 13;
    tm.tm_hour = 12;
    tm.tm_min = 45;
    tm.tm_sec = 0;
    tm.tm_isdst = 0;
    ubase_assert(uref_event_set_start(uref, (uint64_t)mktime(&tm) * UCLOCK_FREQ,
                0));
    ubase_assert(uref_event_set_duration(uref, (uint64_t)6330 * UCLOCK_FREQ,
                0));
    ubase_assert(uref_ts_event_set_running_status(uref, 5, 0));
    ubase_assert(uref_event_set_language(uref, "unk", 0));
    ubase_assert(uref_event_set_name(uref, "meuh", 0));
    ubase_assert(uref_event_set_description(uref, "gaga", 0));

    struct upipe *upipe_ts_sig_service1 = upipe_void_alloc_sub(upipe_ts_sig,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                                   "ts sig service1"));
    assert(upipe_ts_sig_service1 != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_sig_service1, uref));
    ubase_assert(upipe_ts_mux_set_eit_interval(upipe_ts_sig_service1,
                                               UCLOCK_FREQ));
    uref_free(uref);

    struct upipe *upipe_sink = upipe_void_alloc(&ts_test_mgr,
                                                uprobe_use(logger));
    assert(upipe_sink != NULL);
    struct upipe *output;
    ubase_assert(upipe_ts_sig_get_nit_sub(upipe_ts_sig, &output));
    assert(output);
    ubase_assert(upipe_set_output(output, upipe_sink));
    ubase_assert(upipe_ts_sig_get_sdt_sub(upipe_ts_sig, &output));
    assert(output);
    ubase_assert(upipe_set_output(output, upipe_sink));
    ubase_assert(upipe_ts_sig_get_eit_sub(upipe_ts_sig, &output));
    assert(output);
    ubase_assert(upipe_set_output(output, upipe_sink));
    ubase_assert(upipe_ts_sig_get_tdt_sub(upipe_ts_sig, &output));
    assert(output);
    ubase_assert(upipe_set_output(output, upipe_sink));

    ubase_assert(upipe_ts_mux_prepare(upipe_ts_sig, UINT32_MAX, 0));
    assert(tdt);

    upipe_release(upipe_ts_sig_service1);

    upipe_release(upipe_ts_sig);
    upipe_mgr_release(upipe_ts_sig_mgr); // nop

    test_free(upipe_sink);

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe);
    if (iconv_handle != (iconv_t)-1)
        iconv_close(iconv_handle);

    return 0;
}
